#pragma once

#include <string>
#include <vector>
#include "json.hpp"

namespace Crypto {
    // Вычисление SHA256 хэша от данных (возвращает бинарные байты)
    std::vector<uint8_t> sha256(const uint8_t* data, size_t size);
    std::vector<uint8_t> sha256(const std::string& str);
    std::string sha256Hex(const std::string& str);
    std::string sha256Hex(const uint8_t* data, size_t size);

    // Вычисление HMAC-SHA256 (возвращает hex-строку)
    std::string hmacSha256Hex(const std::vector<uint8_t>& key, const std::string& message);

    // Кодирование и декодирование Base64
    std::string base64Encode(const uint8_t* data, size_t size);
    std::string base64Encode(const std::vector<uint8_t>& data);
    std::vector<uint8_t> base64Decode(const std::string& input);

    // URL-safe Base64 декодирование (с поддержкой '-' вместо '+', '_' вместо '/' и паддинга)
    std::vector<uint8_t> urlsafeBase64Decode(const std::string& input);

    // XOR шифрование/дешифрование с ключом
    std::vector<uint8_t> xorCipher(const std::vector<uint8_t>& data, const std::vector<uint8_t>& key);

    // Ключ машины (SHA256 от node|machine|processor)
    std::vector<uint8_t> getMachineKey();

    // DPAPI шифрование и дешифрование
    bool dpapiEncrypt(const std::vector<uint8_t>& input, std::vector<uint8_t>& output);
    bool dpapiDecrypt(const std::vector<uint8_t>& input, std::vector<uint8_t>& output);

    // Шифрование и дешифрование словарей JSON (формат v1:dpapi:... или v1:xor:...)
    std::string encryptDict(const nlohmann::json& dict);
    bool decryptDict(const std::string& token, nlohmann::json& output);

    // Побайтовое сравнение в константное время
    bool constantTimeCompare(const std::string& a, const std::string& b);

    // Преобразование hex-строки в байты и обратно
    std::vector<uint8_t> hexToBytes(const std::string& hex);
    std::string bytesToHex(const uint8_t* data, size_t size);
    std::string bytesToHex(const std::vector<uint8_t>& data);
}
