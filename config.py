"""
config.py — сохранение/загрузка настроек.
"""
from crypto import encrypt_dict, decrypt_dict

import json
import os
import logging
from dataclasses import dataclass, asdict, field
from typing import Dict

logger = logging.getLogger(__name__)

CONFIG_PATH = os.path.join(
    os.path.expanduser("~"),
    ".ghost_ai_config.json"
)


@dataclass
class AppConfig:
    # Backend
    backend: str = ""
    ollama_url: str = "http://localhost:11434"
    ollama_model: str = ""
    openrouter_api_key: str = ""
    openrouter_model: str = "llama-4-scout"

    # Системный промпт — задаёт роль и стиль ответа
    system_prompt: str = (
        "Только правильный ответ без никаких слов"
    )

    # Пользовательский промпт — что делать с КАЖДЫМ изображением
    user_prompt: str = (
        "Отвечай максимально коротко и без ошибок"
    )

    # Хоткеи
    hotkeys: Dict[str, str] = field(default_factory=lambda: {
        "screenshot":    "esc",
        "toggle_follow": "right ctrl",
        "reset":         "ctrl+shift+r",
    })

    capture_keys: Dict[str, str] = field(default_factory=lambda: {
        "expand_right":  "right",
        "shrink_left":   "left",
        "expand_down":   "down",
        "shrink_up":     "up",
        "expand_all":    "mouse wheel up",
        "shrink_all":    "mouse wheel down",
        "reset_capture": "ctrl+0",
    })

    visibility_keys: Dict[str, str] = field(default_factory=lambda: {
        "increase_alpha":      "ctrl+up",
        "decrease_alpha":      "ctrl+down",
        "toggle_clickthrough": "ctrl+shift+c",
        "toggle_invisible":    "ctrl+shift+h",
    })

    kill_key: str = "ctrl+shift+q"

    # Контекст
    max_context_pairs: int = 10
    persist_context: bool = True
    include_images_in_context: bool = False
    reset_context_key: str = "ctrl+shift+x"

    # Прочее
    alpha_min: float = 0.05
    alpha_max: float = 1.0
    alpha_step: float = 0.1
    alpha_default: float = 0.5
    screenshot_offset: int = 50
    capture_step: int = 25
    request_timeout: int = 30
    window_alpha: float = 0.5

    def to_dict(self) -> dict:
        return asdict(self)

    @classmethod
    def from_dict(cls, data: dict) -> "AppConfig":
        defaults = cls()
        merged = defaults.to_dict()
        merged.update(data or {})
        for dict_field in ("hotkeys", "capture_keys", "visibility_keys"):
            d = defaults.__dict__[dict_field].copy()
            d.update(merged.get(dict_field, {}))
            merged[dict_field] = d
        return cls(**merged)

    def save(self) -> None:
        try:
            plain = self.to_dict()
            plain["_secrets"] = encrypt_dict({
            "openrouter_api_key": self.openrouter_api_key,
            })
            plain["openrouter_api_key"] = ""
            with open(CONFIG_PATH, "w", encoding="utf-8") as f:
                json.dump(plain, f, indent=2, ensure_ascii=False)
        except OSError as e:
            logger.error("Не удалось сохранить конфиг: %s", e)

    @classmethod
    def load(cls) -> "AppConfig":
        if not os.path.exists(CONFIG_PATH):
            return cls()
        try:
            with open(CONFIG_PATH, "r", encoding="utf-8") as f:
                data = json.load(f)
            token = data.get("_secrets", "")
            if token:
                secrets = decrypt_dict(token) or {}
                data["openrouter_api_key"] = secrets.get(
                "openrouter_api_key", "")
            data.pop("_secrets", None)
            return cls.from_dict(data)
        except (OSError, json.JSONDecodeError) as e:
            logger.warning("Не удалось загрузить конфиг: %s", e)
            return cls()
