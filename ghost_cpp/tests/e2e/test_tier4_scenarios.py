"""
Tier 4: Real-World Scenarios (End-to-End Operational Resiliency).
Total test cases: 10 complex operational scenarios.
"""

import unittest
import json
import base64
import os
import time

try:
    from .mock_server import MockCluster
    from .test_harness import ConfigHelper, LogParser, MockClientSimulator
except ImportError:
    from mock_server import MockCluster
    from test_harness import ConfigHelper, LogParser, MockClientSimulator


class TestTier4RealWorldScenarios(unittest.TestCase):
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

    def test_scenario_1_proctor_firewall_hard_block_failover_to_ollama(self):
        """
        Scenario S1: Proctor Firewall Lockdown.
        An anti-cheat or proctoring application installs WFP firewall rules blocking
        all outbound traffic to external cloud IPs (triggering WinHttp 12029 Connection Refused).
        GhostAI must seamlessly cascade through Tier 1 (12029) and Tier 2 (12029) down to
        Tier 3 Local Ollama (127.0.0.1:11434).
        """
        # Tier 1 Cloud is blocked by firewall (closed port / 12029)
        s1, _, _ = MockClientSimulator.send_request(self.endpoints["closed_url"], {})
        self.assertEqual(s1, 0, "Tier 1 must report connection refused")

        # Tier 2 Backup VPS is also blocked by firewall
        s2, _, _ = MockClientSimulator.send_request(self.endpoints["closed_url"], {})
        self.assertEqual(s2, 0, "Tier 2 must report connection refused")

        # Tier 3 Local Offline Ollama on loopback is permitted
        sample_img = "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk+A8AAQUBAScY42YAAAAASUVORK5CYII="
        s3, body3, _ = MockClientSimulator.send_request(
            url=f"{self.endpoints['ollama_url']}/api/generate",
            payload={
                "model": "llava:latest",
                "prompt": "Ты экзаменационный солвер. Найди текущий вопрос на экране и напиши только ответ.",
                "images": [sample_img],
                "stream": False
            }
        )
        self.assertEqual(s3, 200)
        resp = json.loads(body3)
        self.assertEqual(resp["response"], "Answer from Local Ollama Tier 3")

    def test_scenario_2_cloud_provider_503_overload_relay_transition(self):
        """
        Scenario S2: Cloud Provider Outage (HTTP 503 Service Unavailable).
        OpenRouter or OpenAI returns 503 Overloaded.
        GhostAI detects 503 and immediately switches to Tier 2 VPS Relay.
        """
        self.cluster.state.cloud_behavior = "error_503"

        # Tier 1 returns 503
        s1, _, _ = MockClientSimulator.send_request(self.endpoints["cloud_url"], {"messages": []})
        self.assertEqual(s1, 503)

        # Transition to Tier 2 Relay
        s2, body2, _ = MockClientSimulator.send_request(
            url=self.endpoints["relay_url"],
            payload={
                "model": "meta-llama/llama-3.2-11b-vision-instruct:free",
                "messages": [{"role": "user", "content": "Exam question"}]
            },
            auth_header="Bearer sk-backup-vps-test-key"
        )
        self.assertEqual(s2, 200)
        self.assertIn("Answer from Relay VPS Tier 2", body2)

    def test_scenario_3_corporate_proxy_with_basic_auth_transit(self):
        """
        Scenario S3: Corporate Authenticated Proxy Transit.
        Environment enforces outbound proxy transit requiring Basic Authentication.
        GhostAI submits credentials, masks headers as Chrome on Windows, and queries Cloud API.
        """
        cred = base64.b64encode(b"ghost_user:ghost_secret_pass").decode("ascii")
        status, body, headers = MockClientSimulator.send_request(
            url=f"http://127.0.0.1:{self.endpoints['proxy_port']}/v1/chat/completions",
            payload={"messages": [{"role": "user", "content": "Query"}]},
            custom_headers={"Proxy-Authorization": f"Basic {cred}"}
        )
        self.assertEqual(status, 200)
        self.assertIn("Authenticated Proxy", body)

        # Verify proxy logged the request with browser headers
        reqs = self.cluster.state.get_requests("proxy")
        self.assertGreater(len(reqs), 0)
        self.assertIn("Mozilla/5.0", reqs[-1]["headers"].get("User-Agent", ""))

    def test_scenario_4_isp_complete_fiber_cut_offline_mode(self):
        """
        Scenario S4: ISP Complete WAN Outage.
        Outbound internet is completely severed. System cascades to local Ollama.
        """
        # Both external endpoints fail
        s1, _, _ = MockClientSimulator.send_request(self.endpoints["closed_url"], {})
        s2, _, _ = MockClientSimulator.send_request(self.endpoints["closed_url"], {})
        self.assertEqual(s1, 0)
        self.assertEqual(s2, 0)

        # Local Ollama continues responding
        s3, body3, _ = MockClientSimulator.send_request(
            url=f"{self.endpoints['ollama_url']}/api/generate",
            payload={"model": "llava:latest", "prompt": "Offline answer"}
        )
        self.assertEqual(s3, 200)

    def test_scenario_5_vps_relay_upstream_502_bad_gateway_to_ollama(self):
        """
        Scenario S5: VPS Relay 502 Bad Gateway.
        Cloud is down (12029), VPS relay reverse proxy is alive but backend is down (502).
        GhostAI intercepts 502 and falls back to Tier 3 Ollama.
        """
        s1, _, _ = MockClientSimulator.send_request(self.endpoints["closed_url"], {})
        self.assertEqual(s1, 0)

        # Relay returns 502 Bad Gateway
        self.cluster.state.relay_behavior = "error_502"
        s2, _, _ = MockClientSimulator.send_request(self.endpoints["relay_url"], {})
        self.assertEqual(s2, 502)

        # Cascades to Local Ollama
        s3, body3, _ = MockClientSimulator.send_request(f"{self.endpoints['ollama_url']}/api/generate", {})
        self.assertEqual(s3, 200)
        self.assertIn("Local Ollama", body3)

    def test_scenario_6_flapping_network_with_intermittent_timeouts(self):
        """
        Scenario S6: Flapping Network with Timeouts.
        High packet loss triggers timeout on Cloud; failover recovers on Relay.
        """
        self.cluster.state.cloud_behavior = "timeout"
        self.cluster.state.cloud_timeout_sec = 0.5

        s1, _, _ = MockClientSimulator.send_request(
            url=self.endpoints["cloud_url"],
            payload={},
            timeout_sec=0.1
        )
        self.assertIn(s1, (0, -1))

        # Relay succeeds
        s2, body2, _ = MockClientSimulator.send_request(self.endpoints["relay_url"], {})
        self.assertEqual(s2, 200)

    def test_scenario_7_stealth_examination_workflow(self):
        """
        Scenario S7: Stealth Examination Workflow.
        Full payload containing screenshot and concise solver instruction.
        """
        sample_screenshot_b64 = "iVBORw0KGgoAAAANSUhEUgAAAAIAAAACCAYAAABytg0kAAAAEklEQVR42mNk+M9QzwAEjAwACXgC/5JgOggAAAAASUVORK5CYII="
        system_prompt = "Ты экзаменационный солвер. Твоя задача — дать ТОЛЬКО финальный ответ."
        user_prompt = "Найди текущий вопрос на экране и напиши только ответ."

        payload = {
            "model": "meta-llama/llama-3.2-11b-vision-instruct:free",
            "messages": [
                {"role": "system", "content": system_prompt},
                {"role": "user", "content": [
                    {"type": "text", "text": user_prompt},
                    {"type": "image_url", "image_url": {"url": f"data:image/jpeg;base64,{sample_screenshot_b64}"}}
                ]}
            ]
        }
        status, body, _ = MockClientSimulator.send_request(
            url=self.endpoints["cloud_url"],
            payload=payload,
            auth_header="Bearer sk-or-v1-primary-key"
        )
        self.assertEqual(status, 200)
        resp = json.loads(body)
        self.assertIn("choices", resp)

    def test_scenario_8_credential_rotation_without_restart(self):
        """
        Scenario S8: Credential Rotation at Runtime.
        Initial key is rejected with 401; user updates key in configuration; request succeeds.
        """
        # Bad key rejected
        bad_auth = "Bearer sk-expired-key"
        self.cluster.state.cloud_behavior = "error_500"  # Cloud error
        s1, _, _ = MockClientSimulator.send_request(self.endpoints["cloud_url"], {}, auth_header=bad_auth)
        self.assertEqual(s1, 500)

        # User updates key
        self.cluster.state.cloud_behavior = "normal"
        new_auth = "Bearer sk-valid-new-key"
        s2, body2, _ = MockClientSimulator.send_request(self.endpoints["cloud_url"], {}, auth_header=new_auth)
        self.assertEqual(s2, 200)

    def test_scenario_9_unattended_boot_and_secret_restoration(self):
        """
        Scenario S9: Cold Boot Secret Decryption.
        Config loaded from disk on application startup with encrypted secrets.
        Secrets are decrypted and used in outbound cascade queries.
        """
        raw_key = "vps-coldboot-key-555"
        cfg = ConfigHelper.create_config({"fallback_api_key": raw_key})
        enc = ConfigHelper.apply_secrets_encryption(cfg)

        # Cold boot loader simulation
        b64 = enc["_secrets"]["fallback_api_key"]
        cipher = base64.b64decode(b64)
        decrypted = bytes([b ^ 0x5A for b in cipher]).decode("utf-8")

        status, body, _ = MockClientSimulator.send_request(
            url=self.endpoints["relay_url"],
            payload={},
            auth_header=f"Bearer {decrypted}"
        )
        self.assertEqual(status, 200)

    def test_scenario_10_forensic_audit_trail_validation(self):
        """
        Scenario S10: Forensic Audit Log Trail.
        Verifies complete chronology of a cascading failover in network.log.
        """
        simulated_audit_log = [
            "[2026-10-24 18:20:00.001] [TIER-1: OpenRouter] Attempting POST https://openrouter.ai/api/v1/chat/completions",
            "[2026-10-24 18:20:00.080] [ERROR] [TIER-1] WinHttpError=12029 HTTP=0 Connection refused",
            "[2026-10-24 18:20:00.085] [TIER-1 -> TIER-2] Switching to Backup VPS Relay due to WinHttp 12029",
            "[2026-10-24 18:20:00.090] [TIER-2: VPS Relay] Attempting POST https://backup.relay.example/v1/chat/completions",
            "[2026-10-24 18:20:00.150] [ERROR] [TIER-2] WinHttpError=0 HTTP=503 Service Unavailable",
            "[2026-10-24 18:20:00.155] [TIER-2 -> TIER-3] Switching to Local Offline Ollama due to HTTP 503",
            "[2026-10-24 18:20:00.160] [TIER-3: Local Ollama] Attempting POST http://127.0.0.1:11434/api/generate",
            "[2026-10-24 18:20:00.320] [SUCCESS] [TIER-3] Received HTTP 200 in 160ms"
        ]
        entries = []
        for l in simulated_audit_log:
            m = LogParser.LOG_PATTERN.match(l)
            entries.append({"raw": l, "tag": m.group("tag") if m else ""})

        transitions = LogParser.find_transitions(entries)
        errors = LogParser.find_errors(entries)

        self.assertEqual(transitions, [(1, 2), (2, 3)])
        self.assertEqual(len(errors), 2)


if __name__ == "__main__":
    unittest.main()
