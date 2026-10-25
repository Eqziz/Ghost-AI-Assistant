# Project: GhostAI Resilient Network Mechanism

## Architecture
GhostAI is a native Windows C++17 application designed for stealth screenshot capture and AI-driven visual query analysis.
The network architecture is organized into decoupled layers:
1. **Configuration & Security Layer (`config.hpp`, `config.cpp`, `crypto.hpp`)**:
   - Manages user settings in JSON format (`config.json` / `.ghost_ai_config.json`).
   - Protects sensitive credentials (API keys, proxy passwords) using Windows DPAPI / XOR stored under `_secrets`.
2. **Setup UI Layer (`setup_ui.hpp`, `setup_ui.cpp`)**:
   - Native Win32 modal dialog providing dark-themed controls for configuring primary, fallback, proxy, and local offline settings.
3. **Diagnostic Logging Layer (`network_logger.hpp`, `network_logger.cpp`)**:
   - Thread-safe file logger recording millisecond-accurate timestamps, connection attempts, error codes (12029, timeouts, 5xx), and failover events to `network.log` and `ghost_startup.log`.
4. **Transport & Proxy Layer (`vision_client.hpp`, `vision_client.cpp`)**:
   - WinHttp client supporting URL parsing, proxy credentials (`WINHTTP_OPTION_PROXY_USERNAME` / `WINHTTP_OPTION_PROXY_PASSWORD`), HTTP 407 handling, realistic browser headers, and SSL options.
5. **Cascading Fallback Dispatcher (`vision_client.hpp`, `vision_client.cpp`, `ghost_assistant.cpp`)**:
   - Implements 3-tier cascade:
     - Tier 1: Primary Cloud API (OpenRouter / OpenAI / Anthropic / Custom)
     - Tier 2: Backup VPS Relay (Alternative domain / port 443)
     - Tier 3: Local Offline Ollama / LM Studio (`http://127.0.0.1:11434` or local port)
   - Dispatches requests on background thread `m_workerThread` while providing real-time non-blocking status updates (`"Подключение к резервному серверу..."`) to `GhostUI::log`.
6. **Presentation & Overlay Layer (`ghost_ui.hpp`, `ghost_ui.cpp`)**:
   - Win32 layered, transparent, click-through overlay executing the main UI message pump.

## Feature Inventory
| # | Feature | Description | Milestone | Source |
|---|---------|-------------|-----------|--------|
| F1 | Extended AppConfig Schema | Added fields for `fallback_url`, `fallback_api_key`, `fallback_model`, `enable_local_fallback`, `proxy_url`, `proxy_user`, `proxy_pass` | M1 | Survey 1, 3 |
| F2 | DPAPI / XOR Secret Encryption | Encrypt `fallback_api_key` and `proxy_pass` inside `_secrets` dictionary in config file | M1 | Survey 3 |
| F3 | SetupUI Fallback & Proxy Controls | Native Win32 dialog controls for backup URL, backup key, offline toggle, and proxy host/port/credentials | M1 | Survey 3 |
| F4 | Diagnostic Network Logging | Thread-safe logging of timestamps, gateway failovers, error codes, and endpoints to `network.log` and `ghost_startup.log` | M1 | Survey 1, 3 |
| F5 | Build System Cache Self-Healing | Prevent stale `CMakeCache.txt` directory mismatch in `build.bat` / build scripts | M1 | Survey 1 |
| F6 | Proxy URL & Credential Parsing | Support `user:password@host:port` syntax and separate config fields for proxy configuration | M2 | Survey 1, 2 |
| F7 | WinHttp Authenticated Proxy | Apply credentials via `WINHTTP_OPTION_PROXY_USERNAME` and `WINHTTP_OPTION_PROXY_PASSWORD` and handle HTTP 407 challenge | M2 | Survey 1, 2 |
| F8 | Browser Masking Headers | Inject standard browser headers (`User-Agent`, `Accept`, `Connection: keep-alive`, `Accept-Language`) | M2 | Survey 1 |
| F9 | 3-Tier Cascading Fallback Engine | Multi-endpoint failover: Tier 1 (Primary Cloud) -> Tier 2 (Backup VPS Relay) -> Tier 3 (Local Offline Ollama) | M3 | Survey 1, 2, 3 |
| F10 | Error Detection & Interception | Detect WinHttp 12029 (ERROR_WINHTTP_CANNOT_CONNECT), timeout 12002, 12030, and HTTP 5xx codes | M3 | Survey 1, 2 |
| F11 | Multi-Format Payload Adapters | OpenAI-compatible payload for Cloud and Relay vs Ollama `/api/generate` payload for local offline | M3 | Survey 2, 3 |
| F12 | Non-Blocking UI Status Reporting | Thread-safe status callback from background worker thread to overlay (`GhostUI::log`) without blocking Win32 message pump | M3 | Survey 2 |
| F13 | Standalone Binary Verification | Verify Release build produces standalone `GhostAI.exe` with static runtime `/MT` and zero external DLLs | M4 | Survey 1 |
| F14 | Comprehensive E2E Verification | Complete verification against E2E test suite (Tiers 1-4) and Tier 5 Adversarial Coverage Hardening | M4 | ORIGINAL_REQUEST |

## Milestones
| # | Name | Scope | Dependencies | Status |
|---|------|-------|-------------|--------|
| 1 | M1: Config, UI & Diagnostic Logging | AppConfig fields, DPAPI secret encryption, SetupUI controls, `network.log` logging engine, build cache fix | none | DONE (Clean build, 170/170 E2E pass, 1,177 stress pass) |
| 2 | M2: Proxy Authentication & Stealth Protocol | Proxy URL parsing, `WINHTTP_OPTION_PROXY_USERNAME`/`PASSWORD`, 407 auth handling, browser masking headers | M1 | DONE (Clean build, 170/170 E2E pass, 13,410 stress pass, 157 adversarial pass) |
| 3 | M3: 3-Tier Cascade Fallback & Async Dispatcher | 3-tier cascade failover engine, error interception (12029, timeout, 5xx), payload adapters, thread-safe UI status callback | M2 | PLANNED |
| 4 | M4: Final Acceptance & Hardening | Pass 100% of E2E Test Suite (Tiers 1-4), Tier 5 Adversarial Coverage Hardening, standalone binary check, Forensic Audit | M3, E2E-READY | PLANNED |

## Interface Contracts

### AppConfig ↔ VisionClient / SetupUI
- **Config Fields**:
  ```cpp
  struct AppConfig {
      // Existing
      std::string backend;              // "openrouter" or "ollama"
      std::string ollama_url;           // e.g. "http://127.0.0.1:11434"
      std::string ollama_model;         // e.g. "llava:latest"
      std::string openrouter_api_key;
      std::string openrouter_model;
      std::string proxy_url;
      // New resilient network fields
      std::string fallback_url;         // Tier 2 VPS relay URL (e.g. "https://backup-relay.example.com/v1/chat/completions")
      std::string fallback_api_key;     // Tier 2 API key
      std::string fallback_model;       // Tier 2 model name (optional, defaults to openrouter_model)
      bool enable_local_fallback;       // Tier 3 toggle for local Ollama fallback
      std::string proxy_user;           // Optional explicit proxy username
      std::string proxy_pass;           // Optional explicit proxy password
      // Methods
      void save();
      static AppConfig load();
      nlohmann::json toJson() const;
      void fromJson(const nlohmann::json& j);
  };
  ```

### NetworkLogger ↔ VisionClient / GhostAssistant
- **Logger API**:
  ```cpp
  class NetworkLogger {
  public:
      static void log(const std::string& message);
      static void logAttempt(int tier, const std::string& endpointName, const std::string& url);
      static void logError(int tier, const std::string& endpointName, DWORD winHttpError, int httpStatusCode, const std::string& detail);
      static void logSuccess(int tier, const std::string& endpointName, long long elapsedMs);
      static void logTransition(int fromTier, int toTier, const std::string& reason);
  };
  ```
- **Log Format**: `[YYYY-MM-DD HH:MM:SS.mmm] [TIER-X: Endpoint] Event message...` written to `network.log` and mirroring critical events to `ghost_startup.log`.

### VisionClient ↔ GhostAssistant
- **Status Callback & Cascade API**:
  ```cpp
  using StatusCallback = std::function<void(const std::string& statusMessage)>;

  class VisionClient {
  public:
      explicit VisionClient(const AppConfig& config, StatusCallback statusCb = nullptr);
      void setStatusCallback(StatusCallback cb);
      std::string analyze(const std::string& imageBase64, const std::string& userPrompt = "");
      void updateConfig(const AppConfig& config);
  private:
      std::string executeCascade(const std::string& imageBase64, const std::string& userPrompt);
      std::string httpPost(const std::string& url, const std::string& jsonPayload, const std::string& authHeader, const std::string& proxyUrl, const std::string& proxyUser, const std::string& proxyPass, int timeoutSec = 30);
  };
  ```

## Code Layout
- `include/config.hpp` & `src/config.cpp`: AppConfig definition, serialization, and DPAPI secret management.
- `include/setup_ui.hpp` & `src/setup_ui.cpp`: SetupUI Win32 dialog controls and message handling.
- `include/network_logger.hpp` & `src/network_logger.cpp`: Structured logging to `network.log` and `ghost_startup.log`.
- `include/proxy_utils.hpp` & `src/proxy_utils.cpp`: Proxy URL parsing, credential extraction, header generators.
- `include/vision_client.hpp` & `src/vision_client.cpp`: WinHttp transport, 3-tier cascade engine, error interception, payload adapters.
- `src/ghost_assistant.cpp`: Background worker thread invocation and `GhostUI::log` status integration.
- `tests/e2e/`: Requirement-driven E2E test suite (runner, mock HTTP server, test cases Tiers 1-4).
