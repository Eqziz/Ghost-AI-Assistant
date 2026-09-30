#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace ScreenCapture {
    void initialize();
    void shutdown();

    // Захват экрана или области (x, y, w, h) в формате JPEG с качеством quality (по умолчанию 85)
    // Если w <= 0 или h <= 0, захватывает весь виртуальный экран
    std::string captureToBase64Jpeg(int x = -1, int y = -1, int width = -1, int height = -1, int quality = 85);
}
