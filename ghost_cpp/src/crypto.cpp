#include "crypto.hpp"
#include <windows.h>
#include <wincrypt.h>
#include <bcrypt.h>
#include <sstream>
#include <iomanip>
#include <algorithm>

#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "bcrypt.lib")

namespace Crypto {

std::string bytesToHex(const uint8_t* data, size_t size) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (size_t i = 0; i < size; ++i) {
        oss << std::setw(2) << static_cast<int>(data[i]);
    }
    return oss.str();
}

std::string bytesToHex(const std::vector<uint8_t>& data) {
    return bytesToHex(data.data(), data.size());
}

std::vector<uint8_t> hexToBytes(const std::string& hex) {
    std::vector<uint8_t> bytes;
    for (size_t i = 0; i + 1 < hex.length(); i += 2) {
        std::string byteString = hex.substr(i, 2);
        uint8_t byte = static_cast<uint8_t>(strtol(byteString.c_str(), nullptr, 16));
        bytes.push_back(byte);
    }
    return bytes;
}

std::vector<uint8_t> sha256(const uint8_t* data, size_t size) {
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCRYPT_HASH_HANDLE hHash = nullptr;
    std::vector<uint8_t> hash(32, 0);

    if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0) {
        if (BCryptCreateHash(hAlg, &hHash, nullptr, 0, nullptr, 0, 0) >= 0) {
            BCryptHashData(hHash, (PUCHAR)data, (ULONG)size, 0);
            BCryptFinishHash(hHash, hash.data(), (ULONG)hash.size(), 0);
            BCryptDestroyHash(hHash);
        }
        BCryptCloseAlgorithmProvider(hAlg, 0);
    }
    return hash;
}

std::vector<uint8_t> sha256(const std::string& str) {
    return sha256(reinterpret_cast<const uint8_t*>(str.data()), str.size());
}

std::string sha256Hex(const std::string& str) {
    auto h = sha256(str);
    return bytesToHex(h);
}

std::string sha256Hex(const uint8_t* data, size_t size) {
    auto h = sha256(data, size);
    return bytesToHex(h);
}

std::string hmacSha256Hex(const std::vector<uint8_t>& key, const std::string& message) {
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCRYPT_HASH_HANDLE hHash = nullptr;
    std::vector<uint8_t> hash(32, 0);

    if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG) >= 0) {
        if (BCryptCreateHash(hAlg, &hHash, nullptr, 0, (PUCHAR)key.data(), (ULONG)key.size(), 0) >= 0) {
            BCryptHashData(hHash, (PUCHAR)message.data(), (ULONG)message.size(), 0);
            BCryptFinishHash(hHash, hash.data(), (ULONG)hash.size(), 0);
            BCryptDestroyHash(hHash);
        }
        BCryptCloseAlgorithmProvider(hAlg, 0);
    }
    return bytesToHex(hash);
}

static const char b64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64Encode(const uint8_t* data, size_t size) {
    std::string out;
    if (!data || size == 0) return out;
    out.reserve(((size + 2) / 3) * 4);

    uint32_t val = 0;
    int valb = -6;
    for (size_t i = 0; i < size; ++i) {
        val = (val << 8) + data[i];
        valb += 8;
        while (valb >= 0) {
            out.push_back(b64_table[(val >> valb) & 0x3F]);
            valb -= 6;
        }
        val &= 0x3F;
    }
    if (valb > -6) {
        out.push_back(b64_table[((val << 8) >> (valb + 8)) & 0x3F]);
    }
    while (out.size() % 4) {
        out.push_back('=');
    }
    return out;
}

std::string base64Encode(const std::vector<uint8_t>& data) {
    return base64Encode(data.data(), data.size());
}

std::vector<uint8_t> base64Decode(const std::string& input) {
    std::vector<int> T(256, -1);
    for (int i = 0; i < 64; i++) T[static_cast<unsigned char>(b64_table[i])] = i;

    std::vector<uint8_t> out;
    out.reserve(input.size() * 3 / 4);

    uint32_t val = 0;
    int valb = -8;
    for (unsigned char c : input) {
        if (c == ' ' || c == '\r' || c == '\n' || c == '\t') continue;
        if (c == '=') break;
        if (T[c] == -1) continue;

        val = (val << 6) + T[c];
        valb += 6;
        if (valb >= 0) {
            out.push_back(static_cast<uint8_t>((val >> valb) & 0xFF));
            valb -= 8;
            val &= 0x3F;
        }
    }
    return out;
}

std::vector<uint8_t> urlsafeBase64Decode(const std::string& input) {
    std::string s = input;
    for (char& c : s) {
        if (c == '-') c = '+';
        else if (c == '_') c = '/';
    }
    while (s.size() % 4 != 0) {
        s.push_back('=');
    }
    return base64Decode(s);
}

std::vector<uint8_t> xorCipher(const std::vector<uint8_t>& data, const std::vector<uint8_t>& key) {
    if (key.empty()) return data;
    std::vector<uint8_t> out(data.size());
    for (size_t i = 0; i < data.size(); ++i) {
        out[i] = data[i] ^ key[i % key.size()];
    }
    return out;
}

std::vector<uint8_t> getMachineKey() {
    char node[MAX_COMPUTERNAME_LENGTH + 1] = { 0 };
    DWORD nodeSize = sizeof(node);
    GetComputerNameA(node, &nodeSize);
    node[MAX_COMPUTERNAME_LENGTH] = '\0';

    SYSTEM_INFO si;
    GetNativeSystemInfo(&si);
    std::string arch = (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64) ? "AMD64" : "x86";

    char proc[256] = { 0 };
    DWORD procSize = sizeof(proc) - 1;
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        RegQueryValueExA(hKey, "ProcessorNameString", nullptr, nullptr, (LPBYTE)proc, &procSize);
        RegCloseKey(hKey);
    }
    proc[255] = '\0';

    std::string machineStr = std::string(node) + "|" + arch + "|" + std::string(proc);
    return sha256(machineStr);
}

bool dpapiEncrypt(const std::vector<uint8_t>& input, std::vector<uint8_t>& output) {
    DATA_BLOB inBlob;
    inBlob.cbData = static_cast<DWORD>(input.size());
    inBlob.pbData = const_cast<BYTE*>(input.data());

    DATA_BLOB outBlob;
    if (CryptProtectData(&inBlob, nullptr, nullptr, nullptr, nullptr, 0x01, &outBlob)) {
        output.assign(outBlob.pbData, outBlob.pbData + outBlob.cbData);
        LocalFree(outBlob.pbData);
        return true;
    }
    return false;
}

bool dpapiDecrypt(const std::vector<uint8_t>& input, std::vector<uint8_t>& output) {
    DATA_BLOB inBlob;
    inBlob.cbData = static_cast<DWORD>(input.size());
    inBlob.pbData = const_cast<BYTE*>(input.data());

    DATA_BLOB outBlob;
    if (CryptUnprotectData(&inBlob, nullptr, nullptr, nullptr, nullptr, 0x01, &outBlob)) {
        output.assign(outBlob.pbData, outBlob.pbData + outBlob.cbData);
        LocalFree(outBlob.pbData);
        return true;
    }
    return false;
}

std::string encryptDict(const nlohmann::json& dict) {
    std::string jsonStr = dict.dump();
    std::vector<uint8_t> raw(jsonStr.begin(), jsonStr.end());

    std::vector<uint8_t> enc;
    if (dpapiEncrypt(raw, enc)) {
        return "v1:dpapi:" + base64Encode(enc);
    }

    auto xorEnc = xorCipher(raw, getMachineKey());
    return "v1:xor:" + base64Encode(xorEnc);
}

bool decryptDict(const std::string& token, nlohmann::json& output) {
    if (token.empty()) return false;
    size_t first = token.find(':');
    if (first == std::string::npos) return false;
    size_t second = token.find(':', first + 1);
    if (second == std::string::npos) return false;

    std::string version = token.substr(0, first);
    std::string method = token.substr(first + 1, second - first - 1);
    std::string payload = token.substr(second + 1);

    auto raw = base64Decode(payload);
    std::vector<uint8_t> dec;

    if (method == "dpapi") {
        if (!dpapiDecrypt(raw, dec)) return false;
    } else if (method == "xor") {
        dec = xorCipher(raw, getMachineKey());
    } else {
        return false;
    }

    try {
        std::string jsonStr(dec.begin(), dec.end());
        output = nlohmann::json::parse(jsonStr);
        return true;
    } catch (...) {
        return false;
    }
}

bool constantTimeCompare(const std::string& a, const std::string& b) {
    if (a.length() != b.length()) return false;
    unsigned char result = 0;
    for (size_t i = 0; i < a.length(); ++i) {
        result |= (a[i] ^ b[i]);
    }
    return result == 0;
}

} // namespace Crypto
