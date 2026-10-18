#pragma once

#include <string>
#include <array>
#include <cstdint>

namespace Obfuscation {

// Compile-time Linear Congruential Generator for pseudo-random keys
constexpr uint32_t compileTimeSeed(uint32_t n) {
    return (n * 1664525u + 1013904223u);
}

constexpr uint8_t randomByte(uint32_t seedVal, size_t index) {
    uint32_t val = seedVal;
    for (size_t i = 0; i <= index; ++i) {
        val = compileTimeSeed(val);
    }
    return static_cast<uint8_t>((val >> 16) & 0xFF);
}

constexpr uint32_t getTimeSeed() {
    return static_cast<uint32_t>(__TIME__[7] - '0') * 1 +
           static_cast<uint32_t>(__TIME__[6] - '0') * 10 +
           static_cast<uint32_t>(__TIME__[4] - '0') * 60 +
           static_cast<uint32_t>(__TIME__[3] - '0') * 600 +
           static_cast<uint32_t>(__TIME__[1] - '0') * 3600 +
           static_cast<uint32_t>(__TIME__[0] - '0') * 36000;
}

template <size_t N, uint32_t Seed>
class ObfStringA {
public:
    constexpr ObfStringA(const char (&str)[N]) {
        for (size_t i = 0; i < N; ++i) {
            uint8_t k = randomByte(Seed, i);
            if (k == 0) k = 0x5A; // Prevent null-key
            data_[i] = static_cast<char>(str[i] ^ k);
        }
    }

    std::string decrypt() const {
        std::string res;
        res.resize(N - 1);
        for (size_t i = 0; i < N - 1; ++i) {
            uint8_t k = randomByte(Seed, i);
            if (k == 0) k = 0x5A;
            res[i] = static_cast<char>(data_[i] ^ k);
        }
        return res;
    }

private:
    std::array<char, N> data_{};
};

template <size_t N, uint32_t Seed>
class ObfStringW {
public:
    constexpr ObfStringW(const wchar_t (&str)[N]) {
        for (size_t i = 0; i < N; ++i) {
            uint8_t k = randomByte(Seed, i);
            if (k == 0) k = 0x5A;
            wchar_t kw = static_cast<wchar_t>(k | (k << 8));
            data_[i] = static_cast<wchar_t>(str[i] ^ kw);
        }
    }

    std::wstring decrypt() const {
        std::wstring res;
        res.resize(N - 1);
        for (size_t i = 0; i < N - 1; ++i) {
            uint8_t k = randomByte(Seed, i);
            if (k == 0) k = 0x5A;
            wchar_t kw = static_cast<wchar_t>(k | (k << 8));
            res[i] = static_cast<wchar_t>(data_[i] ^ kw);
        }
        return res;
    }

private:
    std::array<wchar_t, N> data_{};
};

} // namespace Obfuscation

// Encrypted string macros: returns temporary std::string or std::wstring
#define _X(s) ([]() -> std::string { \
    constexpr uint32_t k = (Obfuscation::getTimeSeed() ^ (__LINE__ * 2654435761u)); \
    constexpr Obfuscation::ObfStringA<sizeof(s), k> obf(s); \
    return obf.decrypt(); \
}())

#define _XW(s) ([]() -> std::wstring { \
    constexpr uint32_t k = (Obfuscation::getTimeSeed() ^ (__LINE__ * 2654435761u)); \
    constexpr Obfuscation::ObfStringW<sizeof(s) / sizeof(wchar_t), k> obf(s); \
    return obf.decrypt(); \
}())

// Shorthand for const char* / const wchar_t* in expressions
#define _XC(s) _X(s).c_str()
#define _XWC(s) _XW(s).c_str()

