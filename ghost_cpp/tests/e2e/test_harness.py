"""
Test Harness utilities and fixtures for GhostAI Resilient Network E2E tests.
Includes:
- Config builder & secret encryptor
- Log parser for network.log / ghost_startup.log
- Mock cluster lifecycle fixture
- Payload validators and client request simulator
"""

import os
import sys
import json
import re
import time
import base64
import socket
import urllib.request
import urllib.error
from typing import Dict, List, Optional, Tuple

try:
    from .mock_server import MockCluster
except ImportError:
    from mock_server import MockCluster


class ConfigHelper:
    """Creates and manages test configurations."""
    DEFAULT_CONFIG = {
        "backend": "openrouter",
        "ollama_url": "http://127.0.0.1:11434",
        "ollama_model": "llava:latest",
        "openrouter_api_key": "sk-or-v1-primary-test-key",
        "openrouter_model": "meta-llama/llama-3.2-11b-vision-instruct:free",
        "proxy_url": "",
        "fallback_url": "http://127.0.0.1:50002/v1/chat/completions",
        "fallback_api_key": "sk-backup-vps-test-key",
        "fallback_model": "meta-llama/llama-3.2-11b-vision-instruct:free",
        "enable_local_fallback": True,
        "proxy_user": "",
        "proxy_pass": "",
        "request_timeout": 5,
        "system_prompt": "Answer shortly",
        "user_prompt": "What is in the image?"
    }

    @staticmethod
    def create_config(overrides: Optional[Dict] = None, output_path: Optional[str] = None) -> Dict:
        conf = dict(ConfigHelper.DEFAULT_CONFIG)
        if overrides:
            conf.update(overrides)
        if output_path:
            os.makedirs(os.path.dirname(os.path.abspath(output_path)), exist_ok=True)
            with open(output_path, "w", encoding="utf-8") as f:
                json.dump(conf, f, indent=2)
        return conf

    @staticmethod
    def apply_secrets_encryption(config_dict: Dict) -> Dict:
        """
        Simulates XOR/DPAPI secret isolation under _secrets key.
        """
        result = dict(config_dict)
        secrets = {}
        if "openrouter_api_key" in result and result["openrouter_api_key"]:
            # XOR with 0x5A as simulation
            raw = result["openrouter_api_key"].encode("utf-8")
            enc = bytes([b ^ 0x5A for b in raw])
            secrets["openrouter_api_key"] = base64.b64encode(enc).decode("ascii")
            result["openrouter_api_key"] = ""

        if "fallback_api_key" in result and result["fallback_api_key"]:
            raw = result["fallback_api_key"].encode("utf-8")
            enc = bytes([b ^ 0x5A for b in raw])
            secrets["fallback_api_key"] = base64.b64encode(enc).decode("ascii")
            result["fallback_api_key"] = ""

        if "proxy_pass" in result and result["proxy_pass"]:
            raw = result["proxy_pass"].encode("utf-8")
            enc = bytes([b ^ 0x5A for b in raw])
            secrets["proxy_pass"] = base64.b64encode(enc).decode("ascii")
            result["proxy_pass"] = ""

        result["_secrets"] = secrets
        return result


class LogParser:
    """Parses and validates structured logs from network.log and ghost_startup.log."""
    LOG_PATTERN = re.compile(
        r"^\[(?P<timestamp>\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3})\]\s+"
        r"(?:\[(?P<tag>[^\]]+)\]\s+)?(?P<message>.*)$"
    )

    @staticmethod
    def parse_log_file(file_path: str) -> List[Dict]:
        if not os.path.exists(file_path):
            return []
        entries = []
        with open(file_path, "r", encoding="utf-8", errors="replace") as f:
            for line_no, line in enumerate(f, 1):
                line = line.strip()
                if not line:
                    continue
                m = LogParser.LOG_PATTERN.match(line)
                if m:
                    entries.append({
                        "line": line_no,
                        "raw": line,
                        "timestamp": m.group("timestamp"),
                        "tag": m.group("tag") or "",
                        "message": m.group("message")
                    })
                else:
                    entries.append({
                        "line": line_no,
                        "raw": line,
                        "timestamp": "",
                        "tag": "",
                        "message": line
                    })
        return entries

    @staticmethod
    def find_transitions(entries: List[Dict]) -> List[Tuple[int, int]]:
        """Finds tier transitions like [TIER-1 -> TIER-2]"""
        transitions = []
        trans_pattern = re.compile(r"TIER-(\d+)\s*(?:->|to)\s*TIER-(\d+)", re.IGNORECASE)
        for e in entries:
            m = trans_pattern.search(e["raw"])
            if m:
                transitions.append((int(m.group(1)), int(m.group(2))))
        return transitions

    @staticmethod
    def find_errors(entries: List[Dict]) -> List[Dict]:
        """Finds log entries indicating error events tagged with [ERROR] or reporting WinHttp errors."""
        error_entries = []
        for e in entries:
            raw = e["raw"].lower()
            tag = e.get("tag", "").lower()
            if "error" in tag or "winhttperror=" in raw:
                error_entries.append(e)
        return error_entries


class MockClientSimulator:
    """
    Sends simulated HTTP/WinHttp requests according to GhostAI contract.
    Used for opaque-box contract verification of endpoints, proxy, headers, and fallback.
    """
    @staticmethod
    def send_request(
        url: str,
        payload: Dict,
        auth_header: Optional[str] = None,
        proxy_url: Optional[str] = None,
        proxy_user: Optional[str] = None,
        proxy_pass: Optional[str] = None,
        custom_headers: Optional[Dict] = None,
        timeout_sec: float = 5.0
    ) -> Tuple[int, str, Dict]:
        data = json.dumps(payload).encode("utf-8")
        headers = {
            "Content-Type": "application/json; charset=utf-8",
            "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36",
            "Accept": "application/json, text/plain, */*",
            "Accept-Language": "en-US,en;q=0.9",
            "Connection": "keep-alive"
        }
        if auth_header:
            headers["Authorization"] = auth_header
        if custom_headers:
            headers.update(custom_headers)

        handlers = []
        if proxy_url:
            p_url = proxy_url
            if proxy_user and proxy_pass and "@" not in proxy_url:
                p_parsed = urllib.parse.urlsplit(proxy_url)
                p_url = f"{p_parsed.scheme}://{proxy_user}:{proxy_pass}@{p_parsed.netloc}{p_parsed.path}"
            handlers.append(urllib.request.ProxyHandler({"http": p_url, "https": p_url}))

        opener = urllib.request.build_opener(*handlers)
        req = urllib.request.Request(url, data=data, headers=headers, method="POST")

        try:
            with opener.open(req, timeout=timeout_sec) as resp:
                resp_body = resp.read().decode("utf-8", errors="replace")
                return resp.status, resp_body, dict(resp.headers)
        except urllib.error.HTTPError as e:
            err_body = e.read().decode("utf-8", errors="replace") if hasattr(e, "read") else ""
            return e.code, err_body, dict(e.headers)
        except (urllib.error.URLError, TimeoutError, socket.timeout) as e:
            reason = str(getattr(e, "reason", e))
            return 0, reason, {}
        except Exception as e:
            return 0 if "timeout" in str(e).lower() else -1, str(e), {}
