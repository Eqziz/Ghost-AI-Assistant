# GhostAI Resilient Network E2E Test Infrastructure Specification

## 1. Overview & Testing Philosophy

GhostAI is designed to operate in stealth and restricted network environments (e.g., behind strict corporate firewalls, proctoring software, or intermittent internet connections). The resilient network mechanism guarantees continuous operation via:
- Multi-tier cascading fallback (Primary Cloud -> Backup VPS Relay -> Local Offline Ollama).
- Resilient WinHttp error interception (WinHttp 12029 `ERROR_WINHTTP_CANNOT_CONNECT`, timeout 12002, 12030, HTTP 5xx).
- Secure authenticated proxy handling with HTTP 407 challenge-response (`WINHTTP_OPTION_PROXY_USERNAME` / `WINHTTP_OPTION_PROXY_PASSWORD`).
- Browser header masking (`User-Agent`, `Accept`, `Connection`, `Accept-Language`).
- DPAPI / XOR secret encryption for sensitive keys in `_secrets`.
- Non-blocking asynchronous UI status reporting.

### Testing Principles
1. **Opaque-Box / Black-Box Verification**: The test infrastructure treats the GhostAI client as a black-box executable or decoupled contract subsystem, observing external effects: HTTP/Proxy requests, responses, status callbacks, `network.log` chronologies, and configuration persistence.
2. **Deterministic Fault Simulation**: All network conditions (connection refused, timeouts, proxy challenges, 5xx server errors, corrupted payloads) are deterministically simulated using a dedicated mock server engine.
3. **Progressive Testability**: The test suite can run in progressive modes:
   - *Standalone Contract Mode*: Verifies mock server behaviors, payload converters, config schemas, and logging contracts independently.
   - *Subsystem Harness Mode*: Tests individual modules (Config, Crypto, Proxy, Logger, VisionClient) via dedicated test entrypoints.
   - *End-to-End Binary Mode*: Executes against the compiled `GhostAI.exe` or test binary in simulated test network environments.
4. **Zero-Flake Concurrency**: Port assignments are dynamic or reserved per test run, socket listeners properly clean up with graceful timeouts, and tests execute isolated from external internet access.

---

## 2. Directory Layout & Test Suite Structure

All test components are located under `tests/e2e/` inside the `ghost_cpp` repository:

```
ghost_cpp/
├── tests/
│   └── e2e/
│       ├── __init__.py
│       ├── mock_server.py            # High-fidelity mock HTTP/HTTPS, Proxy, and Port-refusal engine
│       ├── test_harness.py           # Shared test fixtures, config helpers, and log parsers
│       ├── test_tier1_features.py    # Tier 1: Feature Coverage (>=5 test cases per feature F1-F14)
│       ├── test_tier2_boundaries.py  # Tier 2: Boundary & Corner Cases (>=5 test cases per feature)
│       ├── test_tier3_combinations.py# Tier 3: Cross-Feature Pairwise & Interaction Scenarios
│       ├── test_tier4_scenarios.py   # Tier 4: Real-World Failover & Resiliency Scenarios
│       ├── run_tests.py              # Master test runner with structured JSON/Console reporting
│       └── run_tests.bat             # Batch launcher for automated Windows execution
├── include/                          # GhostAI headers
├── src/                              # GhostAI source files
└── CMakeLists.txt                    # Build definitions
```

---

## 3. Mock Server Architecture

The standalone mock server (`mock_server.py`) provides simulated services listening on separate loopback ports (or paths) on `127.0.0.1`:

```
+---------------------------------------------------------------------------------------+
|                              GhostAI Client (Application)                             |
+---------------------------------------------------------------------------------------+
       |                                |                             |
       | Tier 1: Cloud API              | Tier 2: VPS Relay           | Tier 3: Local Ollama
       v                                v                             v
+------------------+             +------------------+          +------------------------+
| Mock Cloud Server|             | Mock Relay Server|          | Mock Local Ollama      |
| Port: DYNAMIC_1  |             | Port: DYNAMIC_2  |          | Port: 11434 (or DYN_3) |
| (OpenAI format)  |             | (OpenAI format)  |          | (/api/generate format) |
+------------------+             +------------------+          +------------------------+
       ^                                ^                             ^
       |                                |                             |
       +--------------------------------+-----------------------------+
                                        |
                          +---------------------------+
                          |   Mock HTTP Forward Proxy |
                          |   Port: DYNAMIC_PROXY     |
                          |   - HTTP 407 Challenge    |
                          |   - Basic Auth Validator  |
                          |   - Header Inspector      |
                          +---------------------------+
                                        |
                          +---------------------------+
                          | Mock Fault Injector       |
                          | - Port Refusal (TCP RST)  |
                          | - Sleep / Timeout Engine  |
                          | - HTTP 500/502/503/504    |
                          +---------------------------+
```

### Mock Components:
1. **Cloud API Endpoint (`/v1/chat/completions`)**:
   - OpenAI compatible chat completions schema.
   - Validates Bearer token in `Authorization: Bearer <key>`.
   - Inspects stealth headers: `User-Agent`, `Accept`, `Connection`.
   - Returns valid completion JSON: `{"choices":[{"message":{"content":"Mock Cloud Answer"}}]}`.

2. **VPS Relay Endpoint (`/v1/chat/completions` or custom path)**:
   - OpenAI compatible schema representing a secondary self-hosted VPS relay.
   - Validates fallback token and custom model headers.
   - Returns valid completion JSON: `{"choices":[{"message":{"content":"Mock Relay Answer"}}]}`.

3. **Local Offline Ollama Endpoint (`/api/generate`)**:
   - Ollama schema: `{"model":"llava:latest","prompt":"...","stream":false}`.
   - Validates image base64 presence in `images` array.
   - Returns valid completion JSON: `{"response":"Mock Ollama Answer","done":true}`.

4. **Authenticating Forward Proxy**:
   - Listens for HTTP proxy requests (`POST http://...` or `CONNECT host:port`).
   - If unauthenticated, returns `HTTP/1.1 407 Proxy Authentication Required` with header `Proxy-Authenticate: Basic realm="GhostProxy"`.
   - If credentials match `user:pass` encoded in `Proxy-Authorization: Basic <base64>`, forwards the request to the upstream target and returns `HTTP 200`.
   - If credentials invalid, returns `HTTP/1.1 407 Proxy Authentication Required` or `HTTP 403 Forbidden`.

5. **Fault Injection Server & Port Refusal (Simulating WinHttp 12029)**:
   - *Closed Port*: Returns immediate TCP connection reset/refusal (WinHttp error `12029: ERROR_WINHTTP_CANNOT_CONNECT`).
   - *Hanging Socket / Timeout*: Accepts TCP connection but sleeps longer than client timeout (WinHttp error `12002: ERROR_WINHTTP_TIMEOUT`).
   - *Server Errors*: Returns customizable HTTP error codes (`500 Internal Server Error`, `502 Bad Gateway`, `503 Service Unavailable`, `504 Gateway Timeout`).
   - *Corrupted Responses*: Truncated JSON, non-JSON HTML, or empty 200 responses to test parser resilience.

6. **Inspection & Control API (`/__mock/control`)**:
   - `POST /__mock/control/set_behavior`: Dynamically switches endpoint mode (e.g. `{"mode": "error", "code": 502}`).
   - `GET /__mock/control/requests`: Retrieves full log of received HTTP requests, headers, and payloads for verification assertions.
   - `POST /__mock/control/reset`: Clears recorded request history.

---

## 4. Feature Inventory Coverage Mapping (F1 - F14)

The test suite systematically maps to the 14 features defined in `PROJECT.md`:

| Feature ID | Feature Name | Tier 1 (Coverage >=5) | Tier 2 (Boundary >=5) | Tier 3 (Cross-Cutting) | Tier 4 (Real-World) |
|---|---|---|---|---|---|
| **F1** | Extended AppConfig Schema | `test_f1_01` to `test_f1_05` | `test_f1_b01` to `test_f1_b05` | Comb. with F2, F3, F9 | Config migration scenario |
| **F2** | DPAPI / XOR Secret Encryption | `test_f2_01` to `test_f2_05` | `test_f2_b01` to `test_f2_b05` | Comb. with F1, F6, F7 | Unattended reboot & key load |
| **F3** | SetupUI Fallback & Proxy Controls | `test_f3_01` to `test_f3_05` | `test_f3_b01` to `test_f3_b05` | Comb. with F1, F4 | SetupUI config save & reload |
| **F4** | Diagnostic Network Logging | `test_f4_01` to `test_f4_05` | `test_f4_b01` to `test_f4_b05` | Comb. with F9, F10 | Forensic audit of failovers |
| **F5** | Build System Cache Self-Healing | `test_f5_01` to `test_f5_05` | `test_f5_b01` to `test_f5_b05` | Comb. with F13 | Clean build from stale cache |
| **F6** | Proxy URL & Credential Parsing | `test_f6_01` to `test_f6_05` | `test_f6_b01` to `test_f6_b05` | Comb. with F1, F7 | Complex proxy URL formats |
| **F7** | WinHttp Authenticated Proxy | `test_f7_01` to `test_f7_05` | `test_f7_b01` to `test_f7_b05` | Comb. with F6, F9, F10 | Corporate Squid/CCProxy fail |
| **F8** | Browser Masking Headers | `test_f8_01` to `test_f8_05` | `test_f8_b01` to `test_f8_b05` | Comb. with F7, F9 | DPI firewall inspection evasion |
| **F9** | 3-Tier Cascading Fallback Engine | `test_f9_01` to `test_f9_05` | `test_f9_b01` to `test_f9_b05` | Comb. with F4, F10, F11, F12 | Cloud -> Relay -> Ollama |
| **F10** | Error Detection & Interception | `test_f10_01` to `test_f10_05` | `test_f10_b01` to `test_f10_b05` | Comb. with F4, F9 | Rapid recovery on 12029 |
| **F11** | Multi-Format Payload Adapters | `test_f11_01` to `test_f11_05` | `test_f11_b01` to `test_f11_b05` | Comb. with F9 | Cloud payload -> Ollama payload |
| **F12** | Non-Blocking UI Status Reporting | `test_f12_01` to `test_f12_05` | `test_f12_b01` to `test_f12_b05` | Comb. with F9 | Status callback under cascade |
| **F13** | Standalone Binary Verification | `test_f13_01` to `test_f13_05` | `test_f13_b01` to `test_f13_b05` | Comb. with F5 | Static MT runtime, 0 DLLs |
| **F14** | Comprehensive E2E Verification | `test_f14_01` to `test_f14_05` | `test_f14_b01` to `test_f14_b05` | Comb. with all features | Full suite verification pass |

---

## 5. Test Tier Definitions

### Tier 1: Feature Coverage (Isolation)
Tests each feature independently against its specification contract. Minimum 5 distinct test cases per feature (total >= 70 test cases).
- Ensures that basic capabilities work as documented without cross-dependencies.

### Tier 2: Boundary & Corner Cases
Tests extreme boundaries, invalid inputs, edge cases, malformed strings, oversized payloads, network timeouts, and error handling. Minimum 5 distinct test cases per feature (total >= 70 test cases).
- Empty strings, maximum string lengths, unicode characters, control chars.
- Port numbers 0, 65535, 70000; URLs without scheme, with invalid paths, with IPv6.
- Corrupted encrypted blobs, wrong DPAPI scopes, missing JSON fields.
- Immediate connection drops, half-open connections, high latency.

### Tier 3: Cross-Feature Combinations
Tests pairwise and multi-feature interactions to ensure system harmony.
- **C1: Proxy + 12029 Fallback**: Proxy fails with connection refused -> Client falls back to Relay -> Relay succeeds.
- **C2: Proxy + HTTP 407 Challenge + Cloud API**: Proxy demands authentication -> Client responds with Basic Auth -> Upstream Cloud API returns 200.
- **C3: Secret Encryption + Config Persistence + Cascade Dispatcher**: Encrypted keys in config loaded, decrypted into memory, used in Authorization headers across cascade tiers.
- **C4: Cascade Engine + Multi-Format Payload Adapter + Status Callback**: Primary Cloud (OpenAI format) fails with 502 -> Fallback Relay (OpenAI format) fails with 12029 -> Fallback Ollama (Ollama format) succeeds -> UI receives intermediate status updates.
- **C5: Diagnostic Logging + Failover Chronology**: Verifies exact timestamped sequences and tier transition logs in `network.log` for a complete failover run.

### Tier 4: Real-World Scenarios
High-fidelity simulations of actual end-user operational environments:
- **Scenario S1: Proctor Firewall Lockdown**: External outbound HTTPS ports to known AI domains (openrouter.ai, openai.com) blocked by firewall (12029). Client automatically and seamlessly routes to local offline Ollama (`127.0.0.1:11434`) without UI hang or crash.
- **Scenario S2: Cloud Provider Outage (HTTP 503 Service Unavailable)**: Cloud provider returns 503 overloaded. Client transitions to VPS relay within milliseconds.
- **Scenario S3: VPS Relay Failure & Cascade to Local Ollama**: Primary cloud has 12029, VPS relay has 502 Bad Gateway. Client seamlessly cascades down to Tier 3 Ollama.
- **Scenario S4: Corporate Authenticated Proxy Transit**: User is behind corporate proxy requiring Basic credentials. Requests pass through proxy with proper headers, masking, and valid response.
- **Scenario S5: Proxy Down with Local Fallback**: Configured proxy refuses connection (12029). System cascades to local offline Ollama which bypasses proxy.

---

## 6. Execution & Verification Guide

### Prerequisites
- Windows 10/11 x64
- Python 3.8+ (Available: Python 3.14)
- MSVC C++17 build tools (`vcvarsall.bat x64`, CMake)

### Running the Test Suite
From the repository root or test directory:
```powershell
# Run master test suite via Python runner:
python ghost_cpp/tests/e2e/run_tests.py

# Or run via Windows batch launcher:
ghost_cpp\tests\e2e\run_tests.bat

# Run specific tiers:
python ghost_cpp/tests/e2e/run_tests.py --tier 1
python ghost_cpp/tests/e2e/run_tests.py --tier 2
python ghost_cpp/tests/e2e/run_tests.py --tier 3
python ghost_cpp/tests/e2e/run_tests.py --tier 4

# Run with verbose output and export test report:
python ghost_cpp/tests/e2e/run_tests.py --verbose --report test_report.json
```

### Verification Criteria
- All tests must execute deterministically without flakes.
- Test runner must return exit code 0 when all tests pass.
- Detailed pass/fail matrix is summarized in `TEST_READY.md`.
