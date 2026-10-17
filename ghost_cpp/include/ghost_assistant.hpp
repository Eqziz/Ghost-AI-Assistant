#pragma once

#include "config.hpp"
#include "conversation.hpp"
#include "capture_area.hpp"
#include "vision_client.hpp"
#include "keybinder.hpp"
#include "ghost_ui.hpp"
#include <string>
#include <atomic>
#include <thread>
#include <memory>

class GhostAssistant {
public:
    GhostAssistant(AppConfig config, const std::string& mode = "light");
    ~GhostAssistant();

    void run();
    void shutdown();
    void processScreen();
    void killApp();

private:
    void setupHotkeys();
    void startHardwarePolling();
    void startPositionLoop();

    AppConfig m_config;
    std::string m_mode;
    std::atomic<bool> m_running{ true };
    std::atomic<bool> m_isProcessing{ false };

    std::unique_ptr<GhostUI> m_ui;
    std::unique_ptr<ConversationMemory> m_memory;
    std::unique_ptr<VisionClient> m_client;
    std::unique_ptr<KeyBinder> m_binder;
    std::unique_ptr<CaptureAreaController> m_capture;

    std::thread m_hardwarePollThread;
    std::thread m_positionThread;
    std::thread m_workerThread;
};
