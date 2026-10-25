#include "license_manager.hpp"
#include "crypto.hpp"
#include "obfuscator.hpp"
#include "anti_analysis.hpp"
#include <windows.h>
#include <shlobj.h>
#include <ctime>
#include <fstream>
#include <sstream>
#include <iomanip>

namespace {
    inline std::string getKeyHash() {
        return _X("f3d77924997275d9");
    }

    inline std::string getMarker() {
        return _X("ghost_ai_default_build_marker_2024");
    }

    inline std::string getAppObfHex() {
        return _X("bf33b4113b6110ccc4b2d3c01eb6460096376972809bcc980d354c473dc5f54c");
    }

    std::wstring getLicenseFilePathW() {
        // 1. Проверяем рядом с исполняемым файлом (портативный режим)
        wchar_t exePath[MAX_PATH] = { 0 };
        if (GetModuleFileNameW(NULL, exePath, MAX_PATH) > 0) {
            std::wstring dir(exePath);
            size_t slash = dir.find_last_of(L"\\/");
            if (slash != std::wstring::npos) {
                std::wstring localLic = dir.substr(0, slash + 1) + _XW(L".ghost_ai_license.json");
                if (GetFileAttributesW(localLic.c_str()) != INVALID_FILE_ATTRIBUTES) {
                    return localLic;
                }
            }
        }

        // 2. Fallback в профиль пользователя (%USERPROFILE%\.ghost_ai_license.json)
        wchar_t userProfile[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_PROFILE, NULL, 0, userProfile))) {
            return std::wstring(userProfile) + _XW(L"\\.ghost_ai_license.json");
        }
        return _XW(L".ghost_ai_license.json");
    }

    std::string getLicenseFilePath() {
        std::wstring wp = getLicenseFilePathW();
        int len = WideCharToMultiByte(CP_UTF8, 0, wp.c_str(), -1, NULL, 0, NULL, NULL);
        if (len <= 1) return _X(".ghost_ai_license.json");
        std::string s(len - 1, 0);
        WideCharToMultiByte(CP_UTF8, 0, wp.c_str(), -1, &s[0], len, NULL, NULL);
        return s;
    }
}

bool LicenseData::isExpired() const {
    return std::time(nullptr) > expires_at;
}

int64_t LicenseData::daysLeft() const {
    int64_t now = std::time(nullptr);
    return (expires_at > now) ? ((expires_at - now) / 86400) : 0;
}

std::string LicenseData::expiresStr() const {
    std::time_t t = static_cast<std::time_t>(expires_at);
    std::tm tm;
    localtime_s(&tm, &t);
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tm);
    return std::string(buf);
}

namespace LicenseManager {

std::vector<uint8_t> getLicenseSecret() {
    AntiAnalysis::runGuard();

    // 1. Двойной SHA256 маркера
    auto h1 = Crypto::sha256(getMarker());
    auto unobf_v1 = Crypto::sha256(h1.data(), h1.size());

    // 2. Проверка маркера
    std::string keyHash = Crypto::sha256Hex(unobf_v1.data(), unobf_v1.size());
    if (keyHash.substr(0, 16) != getKeyHash()) {
        return std::vector<uint8_t>(32, 0xAA);
    }

    // 3. XOR с обфусцированными данными
    auto raw = Crypto::hexToBytes(getAppObfHex());
    return Crypto::xorCipher(raw, unobf_v1);
}

std::string getHWID() {
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, _XC("SOFTWARE\\Microsoft\\Cryptography"), 0, KEY_READ | KEY_WOW64_64KEY, &hKey) == ERROR_SUCCESS) {
        char guid[256] = { 0 };
        DWORD guidSize = sizeof(guid) - 1;
        if (RegQueryValueExA(hKey, _XC("MachineGuid"), nullptr, nullptr, (LPBYTE)guid, &guidSize) == ERROR_SUCCESS) {
            RegCloseKey(hKey);
            guid[255] = '\0';
            return std::string(guid);
        }
        RegCloseKey(hKey);
    }

    // Резервный вариант
    char node[MAX_COMPUTERNAME_LENGTH + 1] = { 0 };
    DWORD nodeSize = sizeof(node);
    GetComputerNameA(node, &nodeSize);
    node[MAX_COMPUTERNAME_LENGTH] = '\0';
    return std::string(node) + _X("-default-hwid");
}

std::string getHWIDHash() {
    return Crypto::sha256Hex(getHWID());
}

std::optional<LicenseData> verifyKey(const std::string& key) {
    AntiAnalysis::runGuard();

    if (key.empty()) return std::nullopt;
    size_t dotPos = key.rfind('.');
    if (dotPos == std::string::npos) return std::nullopt;

    std::string payload_b64 = key.substr(0, dotPos);
    std::string sig = key.substr(dotPos + 1);

    auto secret = getLicenseSecret();
    std::string expectedSig = Crypto::hmacSha256Hex(secret, payload_b64);

    if (!Crypto::constantTimeCompare(sig, expectedSig)) {
        return std::nullopt;
    }

    auto decoded = Crypto::urlsafeBase64Decode(payload_b64);
    if (decoded.empty()) return std::nullopt;

    try {
        std::string jsonStr(decoded.begin(), decoded.end());
        auto j = nlohmann::json::parse(jsonStr);

        std::string uidKey = _X("uid");
        std::string expKey = _X("exp");
        std::string hwidKeyStr = _X("hwid");

        if (!j.contains(uidKey) || !j.contains(expKey)) {
            return std::nullopt;
        }

        std::string currentHwid = getHWIDHash();
        if (j.contains(hwidKeyStr) && j[hwidKeyStr].is_string()) {
            std::string boundHwid = j[hwidKeyStr].get<std::string>();
            if (!boundHwid.empty()) {
                std::string currentSub = currentHwid.substr(0, boundHwid.length());
                if (boundHwid != currentSub) {
                    return std::nullopt; // Ключ привязан к другому ПК!
                }
            }
        }

        LicenseData data;
        data.telegram_id = j[uidKey].get<int64_t>();
        data.expires_at = j[expKey].get<int64_t>();
        data.hwid_hash = currentHwid;
        return data;
    } catch (...) {
        return std::nullopt;
    }
}

bool saveLicense(const std::string& key) {
    AntiAnalysis::runGuard();

    auto optData = verifyKey(key);
    if (!optData) return false;

    nlohmann::json payload;
    payload[_XC("uid")] = optData->telegram_id;
    payload[_XC("exp")] = optData->expires_at;
    payload[_XC("hwid")] = optData->hwid_hash;

    std::string rawJson = payload.dump();
    std::vector<uint8_t> rawBytes(rawJson.begin(), rawJson.end());

    auto hwidKey = Crypto::hexToBytes(optData->hwid_hash);
    auto encrypted = Crypto::xorCipher(rawBytes, hwidKey);

    std::wstring path = getLicenseFilePathW();
    std::ofstream ofs(path, std::ios::binary);
    if (!ofs.is_open()) return false;

    ofs.write(reinterpret_cast<const char*>(encrypted.data()), encrypted.size());
    return true;
}

std::optional<LicenseData> loadLicense() {
    AntiAnalysis::runGuard();

    std::string hwid_hash = getHWIDHash();
    LicenseData openData;
    openData.telegram_id = 0;
    openData.expires_at = 253402300799LL; // Permanent Open Source
    openData.hwid_hash = hwid_hash;

    std::wstring path = getLicenseFilePathW();
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs.is_open()) return openData;

    std::vector<uint8_t> encrypted((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    if (encrypted.empty()) return openData;

    auto hwidKey = Crypto::hexToBytes(hwid_hash);

    auto decrypted = Crypto::xorCipher(encrypted, hwidKey);
    try {
        std::string jsonStr(decrypted.begin(), decrypted.end());
        auto j = nlohmann::json::parse(jsonStr);

        std::string uidKey = _X("uid");
        std::string expKey = _X("exp");

        LicenseData data;
        data.telegram_id = j.value(uidKey, 0LL);
        data.expires_at = j.value(expKey, 0LL);
        data.hwid_hash = hwid_hash;

        if (data.isExpired()) {
            return openData;
        }
        return data;
    } catch (...) {
        return openData;
    }
}

void clearLicense() {
    std::wstring path = getLicenseFilePathW();
    DeleteFileW(path.c_str());
}

std::string formatLicenseInfo(const LicenseData& data) {
    std::ostringstream oss;
    oss << _X("👻 Лицензия активна\n")
        << _X("Telegram ID: ") << data.telegram_id << "\n"
        << _X("Действует до: ") << data.expiresStr() << "\n"
        << _X("Осталось дней: ") << data.daysLeft();
    return oss.str();
}

} // namespace LicenseManager
