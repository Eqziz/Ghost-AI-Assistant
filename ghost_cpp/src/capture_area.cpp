#include "capture_area.hpp"

CaptureAreaController::CaptureAreaController(int base_offset, int step)
    : m_step(step)
{
    m_rect = SizeRect{ DEFAULT_W + base_offset, DEFAULT_H + base_offset }.clamp();
}

void CaptureAreaController::setResizeCallback(std::function<void(int, int)> callback) {
    m_resizeCallback = callback;
}

SizeRect CaptureAreaController::get() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_rect;
}

void CaptureAreaController::reset() {
    set(DEFAULT_W, DEFAULT_H);
}

void CaptureAreaController::set(int width, int height) {
    SizeRect r;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_rect = SizeRect{ width, height }.clamp();
        r = m_rect;
    }
    if (m_resizeCallback) {
        m_resizeCallback(r.width, r.height);
    }
}

void CaptureAreaController::modify(int dw, int dh) {
    SizeRect r;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_rect = SizeRect{ m_rect.width + dw, m_rect.height + dh }.clamp();
        r = m_rect;
    }
    if (m_resizeCallback) {
        m_resizeCallback(r.width, r.height);
    }
}

void CaptureAreaController::expandRight()  { modify(+m_step, 0); }
void CaptureAreaController::shrinkLeft()   { modify(-m_step, 0); }
void CaptureAreaController::expandDown()   { modify(0, +m_step); }
void CaptureAreaController::shrinkUp()     { modify(0, -m_step); }
void CaptureAreaController::expandAll()    { modify(+m_step, +m_step); }
void CaptureAreaController::shrinkAll()    { modify(-m_step, -m_step); }
void CaptureAreaController::resetCapture() { reset(); }
