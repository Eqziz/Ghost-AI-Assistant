#include "ghost_assistant.hpp"
#include "screen_capture.hpp"
#include "license_manager.hpp"
#include "driver_client.hpp"
#include <iostream>

GhostAssistant::GhostAssistant(AppConfig config, const std::string& mode)
    : m_config(config), m_mode(mode), m_mainThreadId(GetCurrentThreadId())
{
    if (m_mode == "hard") {
        m_config.system_prompt =
            "Ты экзаменационный солвер. Твоя задача — дать ТОЛЬКО финальный ответ. "
            "Выведи исключительно букву правильного варианта (A, B, C, D), конкретное число "
            "или термин. Любые пояснения, вводные фразы и оформление СТРОГО запрещены. "
            "Ответ должен состоять максимум из 1-3 слов.";
        m_config.user_prompt = "Найди текущий вопрос на экране и напиши только ответ.";
    }

    m_ui = std::make_unique<GhostUI>(
        m_config.window_alpha,
        m_config.alpha_min,
        m_config.alpha_max,
        m_mode
    );

    if (m_mode == "hard" && m_ui->isFollowing()) {
        m_ui->toggleFollow(); // В Hard mode окно фиксируется у вопроса, а не мечется за мышью
    }

    m_memory = std::make_unique<ConversationMemory>(
        m_config.max_context_pairs,
        m_config.persist_context
    );

    m_client = std::make_unique<VisionClient>(
        m_config,
        *m_memory,
        [this](const std::string& msg) {
            if (m_ui) {
                m_ui->log(msg);
            }
        }
    );
    if (m_mode != "hard") {
        m_binder = std::make_unique<KeyBinder>();
    }
    m_capture = std::make_unique<CaptureAreaController>(
        m_config.screenshot_offset,
        m_config.capture_step
    );

    m_capture->setResizeCallback([this](int w, int h) {
        m_ui->applySize(w, h);
    });

    setupHotkeys();
    startPositionLoop();

    std::string ctxStatus = m_memory->isEnabled() ?
        (std::to_string(m_memory->size()) + "/" + std::to_string(m_config.max_context_pairs) + " пар") : "выключен";

    if (m_mode == "hard") {
        m_ui->log("👻 Готов [HARD]\n[F2] Скриншот  |  [End] Выход");
    } else {
        std::string scKey = m_config.hotkeys.count("screenshot") ? m_config.hotkeys.at("screenshot") : "esc";
        std::string invKey = m_config.visibility_keys.count("toggle_invisible") ? m_config.visibility_keys.at("toggle_invisible") : "ctrl+shift+h";
        m_ui->log("👻 Готов [LIGHT]\n📸 " + scKey + " — Скриншот\n👻 " + invKey + " — Скрыть/показать\nКонтекст: " + ctxStatus);
    }
}

GhostAssistant::~GhostAssistant() {
    shutdown();
}

void GhostAssistant::setupHotkeys() {
    if (m_mode == "hard") {
        startHardwarePolling();
    } else {
        // Light mode
        m_binder->registerHotkey("screenshot", m_config.hotkeys["screenshot"], [this]() {
            if (m_workerThread.joinable()) {
                m_workerThread.join();
            }
            m_workerThread = std::thread(&GhostAssistant::processScreen, this);
        });

        m_binder->registerHotkey("toggle_follow", m_config.hotkeys["toggle_follow"], [this]() {
            m_ui->toggleFollow();
        });

        m_binder->registerHotkey("reset", m_config.hotkeys["reset"], [this]() {
            m_capture->reset();
        });

        // Клавиши размера окна
        for (const auto& [action, key] : m_config.capture_keys) {
            if (key.empty()) continue;
            if (action == "expand_right") m_binder->registerHotkey(action, key, [this]() { m_capture->expandRight(); });
            else if (action == "shrink_left") m_binder->registerHotkey(action, key, [this]() { m_capture->shrinkLeft(); });
            else if (action == "expand_down") m_binder->registerHotkey(action, key, [this]() { m_capture->expandDown(); });
            else if (action == "shrink_up") m_binder->registerHotkey(action, key, [this]() { m_capture->shrinkUp(); });
            else if (action == "expand_all") m_binder->registerHotkey(action, key, [this]() { m_capture->expandAll(); });
            else if (action == "shrink_all") m_binder->registerHotkey(action, key, [this]() { m_capture->shrinkAll(); });
            else if (action == "reset_capture") m_binder->registerHotkey(action, key, [this]() {
                m_capture->resetCapture();
            });
        }

        // Клавиши видимости
        for (const auto& [action, key] : m_config.visibility_keys) {
            if (key.empty()) continue;
            if (action == "increase_alpha") m_binder->registerHotkey(action, key, [this]() {
                m_ui->adjustAlpha(+m_config.alpha_step);
            });
            else if (action == "decrease_alpha") m_binder->registerHotkey(action, key, [this]() {
                m_ui->adjustAlpha(-m_config.alpha_step);
            });
            else if (action == "toggle_clickthrough") m_binder->registerHotkey(action, key, [this]() {
                m_ui->toggleClickthrough();
            });
            else if (action == "toggle_invisible") m_binder->registerHotkey(action, key, [this]() {
                m_ui->toggleInvisible();
            });
        }

        // Сброс контекста и Kill switch
        if (!m_config.reset_context_key.empty()) {
            m_binder->registerHotkey("reset_context", m_config.reset_context_key, [this]() {
                m_memory->clear();
            });
        }

        if (!m_config.kill_key.empty()) {
            m_binder->registerHotkey("kill", m_config.kill_key, [this]() {
                killApp();
            });
        }
    }
}

void GhostAssistant::startHardwarePolling() {
    m_hardwarePollThread = std::thread([this]() {
        const int VK_F2_KEY = 0x71; // F2
        const int VK_END_KEY = 0x23; // End

        while (m_running) {
            if (GetAsyncKeyState(VK_F2_KEY) & 0x8000) {
                if (m_workerThread.joinable()) {
                    m_workerThread.join();
                }
                m_workerThread = std::thread(&GhostAssistant::processScreen, this);
                while ((GetAsyncKeyState(VK_F2_KEY) & 0x8000) && m_running) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
                }
            }

            if (GetAsyncKeyState(VK_END_KEY) & 0x8000) {
                killApp();
                break;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    });
}

void GhostAssistant::startPositionLoop() {
    m_positionThread = std::thread([this]() {
        while (m_running) {
            if (m_ui && m_ui->isFollowing() && !m_ui->isInvisible()) {
                POINT pt;
                GetCursorPos(&pt);
                m_ui->setPosition(pt.x + 15, pt.y + 15);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    });
}

void GhostAssistant::processScreen() {
    bool expected = false;
    if (!m_isProcessing.compare_exchange_strong(expected, true)) {
        return;
    }

    struct ProcessingGuard {
        std::atomic<bool>& flag;
        ~ProcessingGuard() { flag = false; }
    } guard{ m_isProcessing };

    // Open Source mode: свободный доступ без проверки срока действия лицензии

    POINT captureCursorPos;
    GetCursorPos(&captureCursorPos);

    if (m_mode == "light") {
        m_ui->log("Анализирую...");
        m_ui->setState(WindowState::PROCESSING);
    } else {
        m_memory->clear();
    }

    try {
        std::string imageB64;
        if (m_mode == "hard") {
            // Снимок всего экрана целиком
            imageB64 = ScreenCapture::captureToBase64Jpeg(-1, -1, -1, -1, 85);
            m_ui->setPosition(captureCursorPos.x + 15, captureCursorPos.y + 15);
        } else {
            // Light Mode: захват области вокруг окна
            int x, y, w, h;
            m_ui->getGeometry(x, y, w, h);
            imageB64 = ScreenCapture::captureToBase64Jpeg(x, y, w + m_config.screenshot_offset, h + m_config.screenshot_offset, 85);
        }

        std::string answer = m_client->analyze(imageB64);

        if (m_mode == "hard") {
            m_ui->setPosition(captureCursorPos.x + 15, captureCursorPos.y + 15);
            m_ui->showHardResponse(answer, 6.0);
        } else {
            m_ui->log(answer);
            m_ui->setState(WindowState::IDLE);
        }
    } catch (const std::exception& e) {
        m_ui->log(std::string("[Ошибка: ") + e.what() + "]");
        m_ui->setState(WindowState::ERROR_STATE);
    }
}

void GhostAssistant::killApp() {
    m_running = false;
    try {
        auto& driver = DriverClient::instance();
        if (driver.isConnected()) {
            driver.unprotectProcess();
            driver.disconnect();
        }
    } catch (...) {}

    HWND hwnd = m_ui ? m_ui->getHwnd() : NULL;
    if (hwnd && IsWindow(hwnd)) {
        PostMessage(hwnd, WM_CLOSE, 0, 0);
    } else if (m_mainThreadId != 0) {
        PostThreadMessage(m_mainThreadId, WM_QUIT, 0, 0);
    }
}

void GhostAssistant::shutdown() {
    m_running = false;
    if (m_hardwarePollThread.joinable()) {
        m_hardwarePollThread.join();
    }
    if (m_positionThread.joinable()) {
        m_positionThread.join();
    }
    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }
    if (m_binder) {
        m_binder->stopPollingLoop();
        m_binder->unregisterAll();
        KeyBinder::uninstallHook();
        m_binder.reset();
    }
    if (m_ui) {
        m_ui->destroy();
    }
}

void GhostAssistant::run() {
    if (m_ui) {
        m_ui->runMessageLoop();
    }
}
