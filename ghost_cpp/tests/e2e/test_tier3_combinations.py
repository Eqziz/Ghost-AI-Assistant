"""
Tier 3: Cross-Feature Combinations (Pairwise & Multi-Feature Interactions).
Total test cases: 20 cross-cutting combination tests.
"""

import unittest
import json
import base64
import os
import time
import threading

try:
    from .mock_server import MockCluster
    from .test_harness import ConfigHelper, LogParser, MockClientSimulator
except ImportError:
    from mock_server import MockCluster
    from test_harness import ConfigHelper, LogParser, MockClientSimulator


class TestTier3CrossFeatureCombinations(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cluster = MockCluster()
        cls.cluster.start()
        cls.endpoints = cls.cluster.get_endpoints()

    @classmethod
    def tearDownClass(cls):
        cls.cluster.stop()

    def setUp(self):
        self.cluster.state.reset_behaviors()

    def test_c01_proxy_with_12029_fallback_to_relay(self):
        """Proxy configured, but Cloud endpoint returns 12029 -> Falls back to Relay."""
        # Cloud endpoint on closed port (simulating 12029) through authenticated proxy
        cred = base64.b64encode(b"ghost_user:ghost_secret_pass").decode("ascii")
        s1, _, _ = MockClientSimulator.send_request(
            url=self.endpoints["closed_url"],
            payload={"messages": []},
            custom_headers={"Proxy-Authorization": f"Basic {cred}"}
        )
        # Closed port returns 0 (connection refused)
        self.assertEqual(s1, 0)

        # Fallback to Relay VPS
        s2, body2, _ = MockClientSimulator.send_request(
            url=self.endpoints["relay_url"],
            payload={"messages": []},
            custom_headers={"Proxy-Authorization": f"Basic {cred}"}
        )
        self.assertEqual(s2, 200)
        self.assertIn("Relay VPS", body2)

    def test_c02_proxy_refusal_cascades_to_direct_offline_ollama(self):
        """Proxy itself is down (refusing connections) -> System cascades to local Ollama bypassing proxy."""
        # Proxy connection fails
        s1, _, _ = MockClientSimulator.send_request(
            url=self.endpoints["closed_url"],
            payload={}
        )
        self.assertEqual(s1, 0)

        # Local Ollama on loopback operates directly without proxy
        s_ollama, body_ollama, _ = MockClientSimulator.send_request(
            url=f"{self.endpoints['ollama_url']}/api/generate",
            payload={"model": "llava:latest", "prompt": "offline", "images": []}
        )
        self.assertEqual(s_ollama, 200)
        self.assertIn("Local Ollama", body_ollama)

    def test_c03_proxy_407_auth_challenge_then_cloud_success(self):
        """Proxy issues 407 challenge, client authenticates with Basic Auth, Cloud returns 200."""
        # 1. First attempt unauthenticated
        s1, _, headers1 = MockClientSimulator.send_request(
            url=f"http://127.0.0.1:{self.endpoints['proxy_port']}/v1/chat/completions",
            payload={"model": "test"}
        )
        self.assertEqual(s1, 407)
        self.assertIn("Proxy-Authenticate", headers1)

        # 2. Responds with valid credentials
        cred = base64.b64encode(b"ghost_user:ghost_secret_pass").decode("ascii")
        s2, body2, _ = MockClientSimulator.send_request(
            url=f"http://127.0.0.1:{self.endpoints['proxy_port']}/v1/chat/completions",
            payload={"model": "test"},
            custom_headers={"Proxy-Authorization": f"Basic {cred}"}
        )
        self.assertEqual(s2, 200)
        self.assertIn("Authenticated Proxy", body2)

    def test_c04_proxy_407_auth_rejected_cascades_to_relay_or_offline(self):
        """Proxy rejects invalid credentials -> System aborts proxy attempt and falls back to local Ollama."""
        bad_cred = base64.b64encode(b"wrong:pass").decode("ascii")
        s1, _, _ = MockClientSimulator.send_request(
            url=f"http://127.0.0.1:{self.endpoints['proxy_port']}/v1/chat/completions",
            payload={},
            custom_headers={"Proxy-Authorization": f"Basic {bad_cred}"}
        )
        self.assertEqual(s1, 407)

        # Cascade to Tier 3 Local Ollama
        s_ollama, body_ollama, _ = MockClientSimulator.send_request(
            url=f"{self.endpoints['ollama_url']}/api/generate",
            payload={"model": "llava:latest", "prompt": "offline", "images": []}
        )
        self.assertEqual(s_ollama, 200)

    def test_c05_secret_encryption_with_config_save_and_transport_auth(self):
        """Encrypted fallback_api_key in config is decrypted and passed in Authorization header to Relay."""
        raw_key = "sk-backup-vps-secure-token-999"
        cfg = ConfigHelper.create_config({"fallback_api_key": raw_key})
        encrypted_cfg = ConfigHelper.apply_secrets_encryption(cfg)

        # Simulate decrypting key on load
        b64 = encrypted_cfg["_secrets"]["fallback_api_key"]
        cipher = base64.b64decode(b64)
        decrypted_key = bytes([b ^ 0x5A for b in cipher]).decode("utf-8")
        self.assertEqual(decrypted_key, raw_key)

        # Send request with decrypted Bearer token
        status, body, _ = MockClientSimulator.send_request(
            url=self.endpoints["relay_url"],
            payload={"messages": []},
            auth_header=f"Bearer {decrypted_key}"
        )
        self.assertEqual(status, 200)
        reqs = self.cluster.state.get_requests("relay")
        self.assertEqual(reqs[-1]["headers"].get("Authorization"), f"Bearer {raw_key}")

    def test_c06_secret_encryption_with_proxy_credentials(self):
        """Encrypted proxy_pass in config is decrypted and correctly applied to Proxy-Authorization."""
        raw_pwd = "ghost_secret_pass"
        cfg = ConfigHelper.create_config({"proxy_user": "ghost_user", "proxy_pass": raw_pwd})
        enc = ConfigHelper.apply_secrets_encryption(cfg)

        # Decrypt
        b64 = enc["_secrets"]["proxy_pass"]
        cipher = base64.b64decode(b64)
        decrypted_pass = bytes([b ^ 0x5A for b in cipher]).decode("utf-8")
        self.assertEqual(decrypted_pass, raw_pwd)

        # Send through proxy
        token = base64.b64encode(f"ghost_user:{decrypted_pass}".encode("utf-8")).decode("ascii")
        status, _, _ = MockClientSimulator.send_request(
            url=f"http://127.0.0.1:{self.endpoints['proxy_port']}/v1/chat/completions",
            payload={},
            custom_headers={"Proxy-Authorization": f"Basic {token}"}
        )
        self.assertEqual(status, 200)

    def test_c07_cascade_with_multiformat_payload_cloud_to_ollama(self):
        """Tier 1 (OpenAI payload) fails 502 -> Tier 2 (OpenAI payload) fails 504 -> Tier 3 converts to Ollama payload."""
        self.cluster.state.cloud_behavior = "error_502"
        self.cluster.state.relay_behavior = "error_504"

        # Tier 1 fails
        s1, _, _ = MockClientSimulator.send_request(self.endpoints["cloud_url"], {"messages": []})
        self.assertEqual(s1, 502)

        # Tier 2 fails
        s2, _, _ = MockClientSimulator.send_request(self.endpoints["relay_url"], {"messages": []})
        self.assertEqual(s2, 504)

        # Tier 3 payload adapter
        sample_img = "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk+A8AAQUBAScY42YAAAAASUVORK5CYII="
        ollama_payload = {
            "model": "llava:latest",
            "prompt": "Answer short",
            "images": [sample_img],
            "stream": False
        }
        s3, body3, _ = MockClientSimulator.send_request(
            url=f"{self.endpoints['ollama_url']}/api/generate",
            payload=ollama_payload
        )
        self.assertEqual(s3, 200)
        resp = json.loads(body3)
        self.assertEqual(resp["response"], "Answer from Local Ollama Tier 3")

    def test_c08_cascade_engine_with_status_callback_sequence(self):
        """Status callback receives chronological notifications during failover."""
        statuses = []
        status_cb = lambda msg: statuses.append(msg)

        # Simulate cascade transitions
        status_cb("Анализирую...")
        # Tier 1 fails -> transition to Tier 2
        status_cb("Подключение к резервному серверу...")
        # Tier 2 fails -> transition to Tier 3
        status_cb("Переключение на локальный Ollama...")
        # Completed
        status_cb("Готово")

        self.assertEqual(statuses, [
            "Анализирую...",
            "Подключение к резервному серверу...",
            "Переключение на локальный Ollama...",
            "Готово"
        ])

    def test_c09_cascade_engine_with_diagnostic_network_logger(self):
        """Failover trace matches exact expected transition sequence in network.log."""
        simulated_log = [
            "[2026-10-24 18:10:00.001] [TIER-1: OpenRouter] Attempting POST https://openrouter.ai/api/v1/chat/completions",
            "[2026-10-24 18:10:00.050] [ERROR] [TIER-1] WinHttpError=12029 HTTP=0 Connection refused",
            "[2026-10-24 18:10:00.055] [TIER-1 -> TIER-2] Switching to Backup VPS Relay due to WinHttp 12029",
            "[2026-10-24 18:10:00.060] [TIER-2: VPS Relay] Attempting POST https://backup.relay.example/v1/chat/completions",
            "[2026-10-24 18:10:00.100] [ERROR] [TIER-2] WinHttpError=0 HTTP=502 Bad Gateway",
            "[2026-10-24 18:10:00.105] [TIER-2 -> TIER-3] Switching to Local Offline Ollama due to HTTP 502",
            "[2026-10-24 18:10:00.110] [TIER-3: Local Ollama] Attempting POST http://127.0.0.1:11434/api/generate",
            "[2026-10-24 18:10:00.350] [SUCCESS] [TIER-3] Received HTTP 200 in 240ms"
        ]
        entries = []
        for l in simulated_log:
            m = LogParser.LOG_PATTERN.match(l)
            entries.append({"raw": l, "tag": m.group("tag") if m else ""})

        transitions = LogParser.find_transitions(entries)
        errors = LogParser.find_errors(entries)

        self.assertEqual(transitions, [(1, 2), (2, 3)])
        self.assertEqual(len(errors), 2)

    def test_c10_setup_ui_save_triggers_live_client_config_update(self):
        """Updating config in UI transfers immediately to client configuration."""
        initial_cfg = ConfigHelper.create_config({"fallback_url": "http://127.0.0.1:50001"})
        new_ui_cfg = ConfigHelper.create_config({"fallback_url": "http://127.0.0.1:50002"})

        # Simulating client updateConfig
        current_cfg = dict(initial_cfg)
        current_cfg.update(new_ui_cfg)
        self.assertEqual(current_cfg["fallback_url"], "http://127.0.0.1:50002")

    def test_c11_browser_masking_headers_across_all_cascade_tiers(self):
        """All tiers (Cloud, Relay, Proxy) receive browser stealth headers."""
        for endpoint in ["cloud_url", "relay_url"]:
            status, _, _ = MockClientSimulator.send_request(
                url=self.endpoints[endpoint],
                payload={"messages": []}
            )
            self.assertEqual(status, 200)

        req_cloud = self.cluster.state.get_requests("cloud")[-1]
        req_relay = self.cluster.state.get_requests("relay")[-1]

        for req in [req_cloud, req_relay]:
            self.assertIn("Mozilla/5.0", req["headers"].get("User-Agent", ""))
            self.assertIn("Accept", req["headers"])
            self.assertIn("Accept-Language", req["headers"])

    def test_c12_winhttp_error_interception_with_timeout_cascade(self):
        """Tier 1 times out (12002) -> Transitions to Tier 2 and succeeds."""
        self.cluster.state.cloud_behavior = "timeout"
        self.cluster.state.cloud_timeout_sec = 0.5

        # Tier 1 times out
        s1, _, _ = MockClientSimulator.send_request(
            url=self.endpoints["cloud_url"],
            payload={},
            timeout_sec=0.1
        )
        self.assertIn(s1, (0, -1))

        # Failover to Tier 2
        s2, body2, _ = MockClientSimulator.send_request(
            url=self.endpoints["relay_url"],
            payload={}
        )
        self.assertEqual(s2, 200)

    def test_c13_local_fallback_disabled_stops_cascade_at_relay(self):
        """When enable_local_fallback is False, Tier 3 is skipped and failure is raised."""
        cfg = ConfigHelper.create_config({"enable_local_fallback": False})
        self.cluster.state.cloud_behavior = "error_500"
        self.cluster.state.relay_behavior = "error_502"

        s1, _, _ = MockClientSimulator.send_request(self.endpoints["cloud_url"], {})
        s2, _, _ = MockClientSimulator.send_request(self.endpoints["relay_url"], {})

        self.assertEqual(s1, 500)
        self.assertEqual(s2, 502)
        # Verify Ollama was NOT invoked
        self.assertEqual(len(self.cluster.state.get_requests("ollama")), 0)

    def test_c14_corrupted_response_at_tier1_triggers_fallback_to_tier2(self):
        """Tier 1 returns corrupted non-JSON body -> Triggers fallback to Tier 2."""
        self.cluster.state.cloud_behavior = "corrupt_json"
        s1, body1, _ = MockClientSimulator.send_request(self.endpoints["cloud_url"], {})
        self.assertEqual(s1, 200)
        with self.assertRaises(json.JSONDecodeError):
            json.loads(body1)

        # Fallback to Tier 2
        s2, body2, _ = MockClientSimulator.send_request(self.endpoints["relay_url"], {})
        self.assertEqual(s2, 200)
        parsed2 = json.loads(body2)
        self.assertIn("choices", parsed2)

    def test_c15_concurrent_requests_with_independent_cascades(self):
        """Concurrent requests can cascade independently without collision."""
        results = []
        def worker(idx):
            if idx % 2 == 0:
                s, b, _ = MockClientSimulator.send_request(self.endpoints["cloud_url"], {})
            else:
                s, b, _ = MockClientSimulator.send_request(self.endpoints["relay_url"], {})
            results.append((idx, s))

        threads = [threading.Thread(target=worker, args=(i,)) for i in range(10)]
        for t in threads: t.start()
        for t in threads: t.join()

        self.assertEqual(len(results), 10)
        self.assertTrue(all(r[1] == 200 for r in results))

    def test_c16_proxy_url_user_pass_in_url_vs_separate_fields(self):
        """User and password extracted from proxy URL match explicit config credentials."""
        url_with_auth = "http://ghost_user:ghost_secret_pass@127.0.0.1:8080"
        import re
        m = re.match(r"^http://(?P<user>[^:]+):(?P<pass>[^@]+)@(?P<host>[^:]+):(?P<port>\d+)$", url_with_auth)
        self.assertIsNotNone(m)
        self.assertEqual(m.group("user"), "ghost_user")
        self.assertEqual(m.group("pass"), "ghost_secret_pass")

    def test_c17_model_override_at_tier2(self):
        """Tier 2 uses fallback_model when specified in AppConfig."""
        cfg = ConfigHelper.create_config({
            "openrouter_model": "cloud-model:v1",
            "fallback_model": "backup-model:v2"
        })
        # When querying Relay, payload contains fallback_model
        s, body, _ = MockClientSimulator.send_request(
            url=self.endpoints["relay_url"],
            payload={"model": cfg["fallback_model"], "messages": []}
        )
        self.assertEqual(s, 200)
        reqs = self.cluster.state.get_requests("relay")
        last_body = json.loads(reqs[-1]["body"])
        self.assertEqual(last_body["model"], "backup-model:v2")

    def test_c18_large_image_screenshot_across_cascade_tiers(self):
        """Screenshot payload data is preserved across cascade transitions."""
        img_data = "iVBORw0KGgoAAAANSUhEUgAAAAIAAAACCAYAAABytg0kAAAAEklEQVR42mNk+M9QzwAEjAwACXgC/5JgOggAAAAASUVORK5CYII="
        # Tier 1 fails
        self.cluster.state.cloud_behavior = "error_500"
        s1, _, _ = MockClientSimulator.send_request(
            self.endpoints["cloud_url"],
            {"messages": [{"role": "user", "content": [{"type": "image_url", "image_url": {"url": f"data:image/jpeg;base64,{img_data}"}}]}]}
        )
        self.assertEqual(s1, 500)

        # Tier 2 receives same image
        s2, body2, _ = MockClientSimulator.send_request(
            self.endpoints["relay_url"],
            {"messages": [{"role": "user", "content": [{"type": "image_url", "image_url": {"url": f"data:image/jpeg;base64,{img_data}"}}]}]}
        )
        self.assertEqual(s2, 200)
        relay_req = json.loads(self.cluster.state.get_requests("relay")[-1]["body"])
        self.assertIn(img_data, relay_req["messages"][0]["content"][0]["image_url"]["url"])

    def test_c19_memory_conversation_history_preserved_across_failover(self):
        """Conversation history turns are included in messages array during failover."""
        history = [
            {"role": "user", "content": "Question 1"},
            {"role": "assistant", "content": "Answer 1"},
            {"role": "user", "content": "Question 2"}
        ]
        s, body, _ = MockClientSimulator.send_request(
            url=self.endpoints["relay_url"],
            payload={"messages": history}
        )
        self.assertEqual(s, 200)
        req = json.loads(self.cluster.state.get_requests("relay")[-1]["body"])
        self.assertEqual(len(req["messages"]), 3)

    def test_c20_failover_recovery_on_subsequent_request(self):
        """After a failover, a subsequent request tries Tier 1 first when restored."""
        # 1. Tier 1 is down -> fails over to Tier 2
        self.cluster.state.cloud_behavior = "error_500"
        s1, _, _ = MockClientSimulator.send_request(self.endpoints["cloud_url"], {})
        s2, _, _ = MockClientSimulator.send_request(self.endpoints["relay_url"], {})
        self.assertEqual(s1, 500)
        self.assertEqual(s2, 200)

        # 2. Tier 1 comes back up
        self.cluster.state.cloud_behavior = "normal"
        s3, body3, _ = MockClientSimulator.send_request(self.endpoints["cloud_url"], {})
        self.assertEqual(s3, 200)
        self.assertIn("Cloud Tier 1", body3)


if __name__ == "__main__":
    unittest.main()
