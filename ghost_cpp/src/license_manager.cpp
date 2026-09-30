#include "license_manager.hpp"
#include "crypto.hpp"
#include <windows.h>
#include <shlobj.h>
#include <ctime>
#include <fstream>
#include <sstream>
#include <iomanip>

namespace {
    const char* _KEY_HASH = "f3d77924997275d9";
    const char* _MARKER = "ghost_ai_default_build_marker_2024";
    const char* _APP_OBF_HEX = "bf33b4113b6110ccc4b2d3c01eb6460096376972809bcc980d354c473dc5f54c";

    std::string getLicenseFilePath() {
        char userProfile[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_PROFILE, NULL, 0, userProfile))) {
            return std::string(userProfile) + "\\.ghost_ai_license.json";
        }
        return ".ghost_ai_license.json";
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
    // 1. Двойной SHA256 маркера
    auto h1 = Crypto::sha256(_MARKER);
    auto unobf_v1 = Crypto::sha256(h1.data(), h1.size());

    // 2. Проверка маркера
    std::string keyHash = Crypto::sha256Hex(unobf_v1.data(), unobf_v1.size());
    if (keyHash.substr(0, 16) != _KEY_HASH) {
        // Возвращаем пустой/случайный буфер при несовпадении
        return std::vector<uint8_t>(32, 0xAA);
    }

    // 3. XOR с обфусцированными данными
    auto raw = Crypto::hexToBytes(_APP_OBF_HEX);
    return Crypto::xorCipher(raw, unobf_v1);
}

std::string getHWID() {
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Cryptography", 0, KEY_READ | KEY_WOW64_64KEY, &hKey) == ERROR_SUCCESS) {
        char guid[256] = { 0 };
        DWORD guidSize = sizeof(guid);
        if (RegQueryValueExA(hKey, "MachineGuid", nullptr, nullptr, (LPBYTE)guid, &guidSize) == ERROR_SUCCESS) {
            RegCloseKey(hKey);
            return std::string(guid);
        }
        RegCloseKey(hKey);
    }

    // Резервный вариант
    char node[MAX_COMPUTERNAME_LENGTH + 1] = { 0 };
    DWORD nodeSize = sizeof(node);
    GetComputerNameA(node, &nodeSize);
    return std::string(node) + "-default-hwid";
}

std::string getHWIDHash() {
    return Crypto::sha256Hex(getHWID());
}

std::optional<LicenseData> verifyKey(const std::string& key) {
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

        if (!j.contains("uid") || !j.contains("exp")) {
            return std::nullopt;
        }

        LicenseData data;
        data.telegram_id = j["uid"].get<int64_t>();
        data.expires_at = j["exp"].get<int64_t>();
        data.hwid_hash = getHWIDHash();
        return data;
    } catch (...) {
        return std::nullopt;
    }
}

bool saveLicense(const std::string& key) {
    auto optData = verifyKey(key);
    if (!optData) return false;

    nlohmann::json payload;
    payload["uid"] = optData->telegram_id;
    payload["exp"] = optData->expires_at;
    payload["hwid"] = optData->hwid_hash;

    std::string rawJson = payload.dump();
    std::vector<uint8_t> rawBytes(rawJson.begin(), rawJson.end());

    auto hwidKey = Crypto::hexToBytes(optData->hwid_hash);
    auto encrypted = Crypto::xorCipher(rawBytes, hwidKey);

    std::string path = getLicenseFilePath();
    std::ofstream ofs(path, std::ios::binary);
    if (!ofs.is_open()) return false;

    ofs.write(reinterpret_cast<const char*>(encrypted.data()), encrypted.size());
    return true;
}

std::optional<LicenseData> loadLicense() {
    std::string path = getLicenseFilePath();
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs.is_open()) return std::nullopt;

    std::vector<uint8_t> encrypted((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    if (encrypted.empty()) return std::nullopt;

    std::string hwid_hash = getHWIDHash();
    auto hwidKey = Crypto::hexToBytes(hwid_hash);

    auto decrypted = Crypto::xorCipher(encrypted, hwidKey);
    try {
        std::string jsonStr(decrypted.begin(), decrypted.end());
        auto j = nlohmann::json::parse(jsonStr);

        LicenseData data;
        data.telegram_id = j.value("uid", 0LL);
        data.expires_at = j.value("exp", 0LL);
        data.hwid_hash = hwid_hash;

        if (data.isExpired()) {
            return std::nullopt;
        }
        return data;
    } catch (...) {
        return std::nullopt;
    }
}

void clearLicense() {
    std::string path = getLicenseFilePath();
    DeleteFileA(path.c_str());
}

std::string formatLicenseInfo(const LicenseData& data) {
    std::ostringstream oss;
    oss << "👻 Лицензия активна\n"
        << "Telegram ID: " << data.telegram_id << "\n"
        << "Действует до: " << data.expiresStr() << "\n"
        << "Осталось дней: " << data.daysLeft();
    return oss.str();
}

} // namespace LicenseManager
