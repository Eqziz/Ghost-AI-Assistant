#pragma once

#include <functional>
#include <mutex>
#include <algorithm>

struct SizeRect {
    int width = 350;
    int height = 200;

    SizeRect clamp(int minVal = 100, int maxVal = 4000) const {
        return SizeRect{
            (std::max)(minVal, (std::min)(width, maxVal)),
            (std::max)(minVal, (std::min)(height, maxVal))
        };
    }
};

class CaptureAreaController {
public:
    static constexpr int DEFAULT_W = 350;
    static constexpr int DEFAULT_H = 200;

    CaptureAreaController(int base_offset = 50, int step = 25);

    void setResizeCallback(std::function<void(int, int)> callback);
    SizeRect get();
    void reset();
    void set(int width, int height);

    void expandRight();
    void shrinkLeft();
    void expandDown();
    void shrinkUp();
    void expandAll();
    void shrinkAll();
    void resetCapture();

private:
    void modify(int dw, int dh);

    int m_step;
    SizeRect m_rect;
    std::mutex m_mutex;
    std::function<void(int, int)> m_resizeCallback;
};
