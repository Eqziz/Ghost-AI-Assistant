#include "network_logger.hpp"
#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <regex>
#include <windows.h>

void testCaseSensitivityAndCyrillic() {
    std::cout << "\n=== Running Edge Case & Robustness Probe ===" << std::endl;

    // 1. Case-insensitive query parameters
    std::string uppercaseKey = "https://api.openai.com/v1/chat?API_KEY=sk-upper12345&Token=myToken";
    std::string sanitizedUpper = NetworkLogger::sanitizeUrl(uppercaseKey);
    bool upperKeyMasked = (sanitizedUpper.find("sk-upper12345") == std::string::npos);
    std::cout << "Uppercase query param ?API_KEY=: "
              << (upperKeyMasked ? "[PASS] Masked" : "[FAIL] Leaked plain secret: " + sanitizedUpper)
              << std::endl;

    // 2. Unicode / Cyrillic path support
    char tempPath[MAX_PATH];
    GetTempPathA(MAX_PATH, tempPath);
    std::string cyrillicDir = std::string(tempPath) + "ghost_логи_тест";
    int wlen = MultiByteToWideChar(CP_UTF8, 0, cyrillicDir.c_str(), -1, NULL, 0);
    std::wstring wDir(wlen > 1 ? wlen - 1 : 0, 0);
    MultiByteToWideChar(CP_UTF8, 0, cyrillicDir.c_str(), -1, &wDir[0], wlen);
    CreateDirectoryW(wDir.c_str(), NULL);

    NetworkLogger::setLogDirectory(cyrillicDir);
    NetworkLogger::log("Cyrillic directory logging test");

    std::wstring testLogPath = wDir + L"\\network.log";
    std::ifstream ifs(testLogPath.c_str());
    bool cyrillicWritten = ifs.is_open() && ifs.good();
    std::cout << "Logging to Cyrillic path: "
              << (cyrillicWritten ? "[PASS] Succeeded" : "[FAIL] Failed to open/write")
              << std::endl;

    // 3. Sequential monotonicity (single thread)
    bool seqMonotonic = true;
    std::string lastTs = "";
    int seqInversions = 0;
    for (int i = 0; i < 5000; ++i) {
        std::string ts = NetworkLogger::getTimestamp();
        if (!lastTs.empty() && ts < lastTs) {
            seqMonotonic = false;
            seqInversions++;
        }
        lastTs = ts;
    }
    std::cout << "Sequential timestamp monotonicity (single thread, 5000 iterations): "
              << (seqMonotonic ? "[PASS] 0 inversions" : "[FAIL] " + std::to_string(seqInversions) + " inversions")
              << std::endl;

    DeleteFileW(testLogPath.c_str());
    RemoveDirectoryW(wDir.c_str());
}

int main() {
    testCaseSensitivityAndCyrillic();
    return 0;
}
