"""
Standalone High-Fidelity Mock Server for GhostAI E2E Resilient Network Testing.

Simulates:
1. OpenAI-compatible Cloud API endpoint (/v1/chat/completions)
2. OpenAI-compatible VPS Relay endpoint (/v1/chat/completions)
3. Ollama local offline endpoint (/api/generate, /api/tags)
4. Authenticating Forward Proxy (HTTP 407 challenge, Basic Auth validation)
5. Network failure modes:
   - WinHttp 12029 (Connection Refused / Closed port)
   - Timeout (delayed socket read/response)
   - HTTP 500, 502, 503, 504 server errors
   - Malformed JSON / Corrupted response bodies
6. Request recording and inspection API (/__mock/...)
"""

import sys
import os
import time
import json
import socket
import base64
import threading
from http.server import HTTPServer, BaseHTTPRequestHandler
from urllib.parse import urlparse, parse_qs

# Configuration & State Container
class MockServerState:
    def __init__(self):
        self.lock = threading.Lock()
        # Endpoints behavior: "normal", "error_500", "error_502", "error_503", "error_504",
        # "timeout", "refuse", "corrupt_json", "empty_200"
        self.cloud_behavior = "normal"
        self.cloud_error_code = 500
        self.cloud_timeout_sec = 10.0

        self.relay_behavior = "normal"
        self.relay_error_code = 502
        self.relay_timeout_sec = 10.0

        self.ollama_behavior = "normal"
        self.ollama_error_code = 500
        self.ollama_timeout_sec = 10.0

        # Proxy credentials
        self.proxy_require_auth = True
        self.proxy_user = "ghost_user"
        self.proxy_pass = "ghost_secret_pass"
        self.proxy_behavior = "normal"  # "normal", "reject_407_always", "refuse"

        # Recorded requests: list of dicts {endpoint, method, path, headers, body, timestamp}
        self.recorded_requests = []

    def record_request(self, endpoint, method, path, headers, body):
        with self.lock:
            self.recorded_requests.append({
                "endpoint": endpoint,
                "method": method,
                "path": path,
                "headers": dict(headers),
                "body": body,
                "timestamp": time.time()
            })

    def get_requests(self, endpoint=None):
        with self.lock:
            if endpoint:
                return [r for r in self.recorded_requests if r["endpoint"] == endpoint]
            return list(self.recorded_requests)

    def clear_requests(self):
        with self.lock:
            self.recorded_requests.clear()

    def reset_behaviors(self):
        with self.lock:
            self.cloud_behavior = "normal"
            self.cloud_error_code = 500
            self.relay_behavior = "normal"
            self.relay_error_code = 502
            self.ollama_behavior = "normal"
            self.ollama_error_code = 500
            self.proxy_behavior = "normal"
            self.proxy_require_auth = True
            self.recorded_requests.clear()


class UnifiedMockRequestHandler(BaseHTTPRequestHandler):
    """
    Handles requests for Cloud, Relay, Ollama, and Mock Control.
    Differentiates endpoint by path or Host header if running on unified port,
    or can run on separate ports.
    """
    server_state: MockServerState = None
    server_mode: str = "cloud"  # "cloud", "relay", "ollama", or "proxy"

    def log_message(self, format, *args):
        # Silence default stderr logging to keep test output clean
        pass

    def _send_json(self, status_code, data, extra_headers=None):
        body = json.dumps(data).encode("utf-8")
        try:
            self.send_response(status_code)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Server", "GhostAI-MockServer/1.0")
            if extra_headers:
                for k, v in extra_headers.items():
                    self.send_header(k, v)
            self.end_headers()
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError, OSError):
            pass

    def _read_body(self):
        length = int(self.headers.get("Content-Length", 0))
        if length > 0:
            return self.rfile.read(length).decode("utf-8", errors="replace")
        return ""

    def do_GET(self):
        # Control API
        if self.path == "/__mock/requests":
            self._send_json(200, {"requests": self.server_state.get_requests()})
            return
        elif self.path == "/__mock/health":
            self._send_json(200, {"status": "ok", "mode": self.server_mode})
            return
        elif self.path == "/api/tags":
            # Ollama tags check
            self._send_json(200, {"models": [{"name": "llava:latest"}]})
            return

        self._send_json(404, {"error": "Not Found"})

    def do_POST(self):
        body = self._read_body()
        endpoint = self.server_mode

        # Check for control requests
        if self.path == "/__mock/control/set_behavior":
            try:
                cmd = json.loads(body)
                target = cmd.get("target", "cloud")
                behavior = cmd.get("behavior", "normal")
                code = cmd.get("code", 500)
                timeout_sec = cmd.get("timeout_sec", 10.0)

                with self.server_state.lock:
                    if target == "cloud":
                        self.server_state.cloud_behavior = behavior
                        self.server_state.cloud_error_code = code
                        self.server_state.cloud_timeout_sec = timeout_sec
                    elif target == "relay":
                        self.server_state.relay_behavior = behavior
                        self.server_state.relay_error_code = code
                        self.server_state.relay_timeout_sec = timeout_sec
                    elif target == "ollama":
                        self.server_state.ollama_behavior = behavior
                        self.server_state.ollama_error_code = code
                        self.server_state.ollama_timeout_sec = timeout_sec
                    elif target == "proxy":
                        self.server_state.proxy_behavior = behavior
                self._send_json(200, {"status": "behavior_updated", "target": target, "behavior": behavior})
            except Exception as e:
                self._send_json(400, {"error": str(e)})
            return

        if self.path == "/__mock/control/reset":
            self.server_state.reset_behaviors()
            self._send_json(200, {"status": "reset_complete"})
            return

        # Record incoming request
        self.server_state.record_request(endpoint, "POST", self.path, self.headers, body)

        # Get behavior for current server mode
        behavior = "normal"
        err_code = 500
        timeout_sec = 10.0

        with self.server_state.lock:
            if endpoint == "cloud":
                behavior = self.server_state.cloud_behavior
                err_code = self.server_state.cloud_error_code
                timeout_sec = self.server_state.cloud_timeout_sec
            elif endpoint == "relay":
                behavior = self.server_state.relay_behavior
                err_code = self.server_state.relay_error_code
                timeout_sec = self.server_state.relay_timeout_sec
            elif endpoint == "ollama":
                behavior = self.server_state.ollama_behavior
                err_code = self.server_state.ollama_error_code
                timeout_sec = self.server_state.ollama_timeout_sec

        # Simulate network failure modes
        if behavior == "timeout":
            time.sleep(timeout_sec)
            self._send_json(504, {"error": "Gateway Timeout Simulation"})
            return

        if behavior.startswith("error_") or behavior in ("error_500", "error_502", "error_503", "error_504"):
            code = int(behavior.split("_")[1]) if "_" in behavior else err_code
            self._send_json(code, {"error": f"Simulated HTTP {code} error", "message": "Backend failure"})
            return

        if behavior == "corrupt_json":
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            self.wfile.write(b"{\"incomplete_json\": [1, 2, ")
            return

        if behavior == "empty_200":
            self.send_response(200)
            self.send_header("Content-Length", "0")
            self.end_headers()
            return

        # Normal Response Dispatch
        if endpoint in ("cloud", "relay"):
            # OpenAI / OpenRouter format
            # Validate payload has messages
            model_name = "mock-cloud-model" if endpoint == "cloud" else "mock-relay-model"
            content = "Answer from Cloud Tier 1" if endpoint == "cloud" else "Answer from Relay VPS Tier 2"
            try:
                pj = json.loads(body)
                if "model" in pj:
                    model_name = pj["model"]
            except:
                pass

            resp_payload = {
                "id": f"chatcmpl-mock-{int(time.time())}",
                "object": "chat.completion",
                "created": int(time.time()),
                "model": model_name,
                "choices": [
                    {
                        "index": 0,
                        "message": {
                            "role": "assistant",
                            "content": content
                        },
                        "finish_reason": "stop"
                    }
                ],
                "usage": {
                    "prompt_tokens": 15,
                    "completion_tokens": 8,
                    "total_tokens": 23
                }
            }
            self._send_json(200, resp_payload)

        elif endpoint == "ollama":
            # Ollama format (/api/generate)
            resp_payload = {
                "model": "llava:latest",
                "created_at": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
                "response": "Answer from Local Ollama Tier 3",
                "done": True,
                "total_duration": 1500000000
            }
            self._send_json(200, resp_payload)
        else:
            self._send_json(200, {"result": "ok"})


class MockProxyRequestHandler(BaseHTTPRequestHandler):
    """
    Simulates a corporate / forwarding proxy requiring HTTP 407 Basic Authentication.
    """
    server_state: MockServerState = None
    cloud_port: int = 0
    relay_port: int = 0

    def log_message(self, format, *args):
        pass

    def _send_407_challenge(self):
        body = b"Proxy Authentication Required"
        self.send_response(407)
        self.send_header("Proxy-Authenticate", 'Basic realm="GhostAI Mock Proxy"')
        self.send_header("Content-Type", "text/plain")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _validate_auth(self):
        auth_hdr = self.headers.get("Proxy-Authorization")
        if not auth_hdr:
            return False
        parts = auth_hdr.split(" ", 1)
        if len(parts) != 2 or parts[0].lower() != "basic":
            return False
        try:
            decoded = base64.b64decode(parts[1]).decode("utf-8")
            user, pwd = decoded.split(":", 1)
            return (user == self.server_state.proxy_user and pwd == self.server_state.proxy_pass)
        except Exception:
            return False

    def do_CONNECT(self):
        # HTTPS Tunneling CONNECT request
        self.server_state.record_request("proxy", "CONNECT", self.path, self.headers, "")

        if self.server_state.proxy_behavior == "refuse":
            self.send_error(502, "Bad Gateway")
            return

        if self.server_state.proxy_require_auth and not self._validate_auth():
            self._send_407_challenge()
            return

        # Authenticated CONNECT tunnel
        self.send_response(200, "Connection Established")
        self.end_headers()

    def do_POST(self):
        length = int(self.headers.get("Content-Length", 0))
        body = self.rfile.read(length).decode("utf-8", errors="replace") if length > 0 else ""
        self.server_state.record_request("proxy", "POST", self.path, self.headers, body)

        if self.server_state.proxy_behavior == "refuse":
            self.send_error(502, "Bad Gateway")
            return

        if self.server_state.proxy_require_auth and not self._validate_auth():
            self._send_407_challenge()
            return

        # Forwarded HTTP request: return forwarded response
        resp_payload = {
            "id": "chatcmpl-mock-proxy",
            "choices": [{"message": {"content": "Answer via Authenticated Proxy"}}]
        }
        res_bytes = json.dumps(resp_payload).encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(res_bytes)))
        self.end_headers()
        self.wfile.write(res_bytes)


class ClosedPortSimulator:
    """
    Listens on a socket and immediately resets/aborts connections (TCP RST),
    or binds and closes to guarantee port is closed, simulating WinHttp 12029.
    """
    def __init__(self, port=0):
        # Bind to find free port then close immediately
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.bind(("127.0.0.1", port))
        self.port = s.getsockname()[1]
        s.close()

    def get_port(self):
        return self.port


class MockCluster:
    """
    Manages the cluster of mock servers:
    - Cloud API (OpenAI)
    - Relay VPS (OpenAI)
    - Local Ollama (Ollama)
    - Proxy (407 Basic Auth)
    - Refused Port (WinHttp 12029)
    """
    def __init__(self, base_port=0):
        self.state = MockServerState()
        self.threads = []
        self.servers = []

        # Find ports
        self.cloud_server, self.cloud_port = self._create_server("cloud")
        self.relay_server, self.relay_port = self._create_server("relay")
        self.ollama_server, self.ollama_port = self._create_server("ollama")
        self.proxy_server, self.proxy_port = self._create_proxy_server()

        # Closed port for 12029 simulation
        self.closed_port_sim = ClosedPortSimulator()
        self.closed_port = self.closed_port_sim.get_port()

    def _create_server(self, mode):
        handler_cls = type(f"Handler_{mode}", (UnifiedMockRequestHandler,), {
            "server_state": self.state,
            "server_mode": mode
        })
        server = HTTPServer(("127.0.0.1", 0), handler_cls)
        port = server.server_address[1]
        return server, port

    def _create_proxy_server(self):
        handler_cls = type("ProxyHandler", (MockProxyRequestHandler,), {
            "server_state": self.state,
            "cloud_port": self.cloud_port,
            "relay_port": self.relay_port
        })
        server = HTTPServer(("127.0.0.1", 0), handler_cls)
        port = server.server_address[1]
        return server, port

    def start(self):
        for s in [self.cloud_server, self.relay_server, self.ollama_server, self.proxy_server]:
            self.servers.append(s)
            t = threading.Thread(target=s.serve_forever, daemon=True)
            t.start()
            self.threads.append(t)
        # Give servers a moment to bind
        time.sleep(0.1)

    def stop(self):
        for s in self.servers:
            try:
                s.shutdown()
                s.server_close()
            except Exception:
                pass

    def get_endpoints(self):
        return {
            "cloud_url": f"http://127.0.0.1:{self.cloud_port}/v1/chat/completions",
            "cloud_port": self.cloud_port,
            "relay_url": f"http://127.0.0.1:{self.relay_port}/v1/chat/completions",
            "relay_port": self.relay_port,
            "ollama_url": f"http://127.0.0.1:{self.ollama_port}",
            "ollama_port": self.ollama_port,
            "proxy_url": f"http://127.0.0.1:{self.proxy_port}",
            "proxy_port": self.proxy_port,
            "proxy_auth_url": f"http://{self.state.proxy_user}:{self.state.proxy_pass}@127.0.0.1:{self.proxy_port}",
            "closed_port": self.closed_port,
            "closed_url": f"http://127.0.0.1:{self.closed_port}/v1/chat/completions"
        }


if __name__ == "__main__":
    cluster = MockCluster()
    cluster.start()
    ep = cluster.get_endpoints()
    print("MockCluster running:")
    for k, v in ep.items():
        print(f"  {k}: {v}")
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        cluster.stop()
        print("MockCluster stopped.")
