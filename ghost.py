"""
ghost.py — главный модуль Ghost-AI.
"""

import sys
import traceback

def _unhandled_exception_handler(exc_type, exc_value, exc_traceback):
    with open("crash.log", "w", encoding="utf-8") as f:
        traceback.print_exception(exc_type, exc_value, exc_traceback, file=f)
    sys.__excepthook__(exc_type, exc_value, exc_traceback)

sys.excepthook = _unhandled_exception_handler

import logging
import os

def _check_license() -> None:
    try:
        from license_manager import load_license, LicenseData
        from activation_ui import ActivationUI

        data = load_license()
        if data and not data.is_expired:
            return

        # Лицензии нет — показать окно
        holder = []
        ui = ActivationUI(on_activated=lambda d: holder.append(d))
        if not holder:
            sys.exit(0)
    except SystemExit:
        raise
    except Exception as e:
        print(f"Ошибка проверки лицензии: {e}", file=sys.stderr)
        sys.exit(1)


_check_license()


import base64
import io
import os
import threading
from typing import Optional

import keyboard
import pyautogui
import requests
from PIL import Image

from config import AppConfig
from conversation import ConversationMemory
from keybinder import KeyBinder
from setup_ui import SetupUI
from ui import GhostUI, WindowState
from capture_area import CaptureAreaController

logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(levelname)s - %(message)s',
)
logger = logging.getLogger(__name__)


# ============================================================
#  Vision-клиент (с поддержкой контекста)
# ============================================================
class VisionClient:
    def __init__(self, config: AppConfig, memory: ConversationMemory):
        self.config = config
        self.memory = memory

    def analyze(self, image: Image.Image) -> str:
        """
        Анализ изображения. Использует ТОЛЬКО промпты из конфига:
          - system_prompt — задаёт стиль
          - user_prompt   — что делать с изображением
        Никаких вшитых инструкций.
        """
        user_prompt = (self.config.user_prompt or "").strip()

        if self.config.backend == "ollama":
            answer = self._analyze_ollama(image, user_prompt)
        elif self.config.backend == "openrouter":
            answer = self._analyze_openrouter(image, user_prompt)
        else:
            raise RuntimeError(f"Неизвестный backend: {self.config.backend!r}")

        self.memory.add_turn(user_prompt, image, answer)
        return answer

    # ---------- Ollama ----------
    def _analyze_ollama(self, image: Image.Image, prompt: str) -> str:
        url = f"{self.config.ollama_url.rstrip('/')}/api/generate"
        img_b64 = self._image_to_base64(image)
        full_prompt = self._build_user_prompt(prompt)

        payload = {
            "model": self.config.ollama_model,
            "prompt": full_prompt,
            "images": [img_b64],
            "stream": False,
        }

        try:
            r = requests.post(url, json=payload,
                              timeout=self.config.request_timeout)
        except requests.exceptions.ConnectionError as e:
            raise ConnectionError("Ollama недоступна") from e
        except requests.exceptions.Timeout as e:
            raise TimeoutError("Ollama timeout") from e

        if r.status_code != 200:
            raise RuntimeError(f"Ollama HTTP {r.status_code}: {r.text[:200]}")

        answer = (r.json().get("response") or "").strip()
        if not answer:
            raise RuntimeError("Ollama: пустой ответ")
        return answer

    # ---------- OpenRouter ----------
    def _analyze_openrouter(self, image: Image.Image, prompt: str) -> str:
        url = "https://openrouter.ai/api/v1/chat/completions"
        img_b64 = self._image_to_base64(image)
        headers = {
            "Authorization": f"Bearer {self.config.openrouter_api_key}",
            "Content-Type": "application/json",
        }

        messages = []

        # Системный промпт — задаёт роль и стиль
        sp = (self.config.system_prompt or "").strip()
        if sp:
            messages.append({"role": "system", "content": sp})

        # История диалога
        if self.memory.enabled:
            turns = self.memory.get_turns(
                include_images=self.config.include_images_in_context
            )
            for t in turns:
                messages.append({"role": "user", "content": t.user_text})
                messages.append({"role": "assistant",
                                 "content": t.assistant_text})

        # Текущий запрос: текст + изображение
        messages.append({
            "role": "user",
            "content": [
                {"type": "text", "text": prompt},
                {"type": "image_url",
                 "image_url": {"url": f"data:image/jpeg;base64,{img_b64}"}},
            ],
        })

        payload = {
            "model": self.config.openrouter_model,
            "messages": messages,
            "max_tokens": 500,
        }

        try:
            r = requests.post(url, json=payload, headers=headers,
                              timeout=self.config.request_timeout)
        except requests.exceptions.ConnectionError as e:
            raise ConnectionError("OpenRouter недоступен") from e
        except requests.exceptions.Timeout as e:
            raise TimeoutError("OpenRouter timeout") from e

        if r.status_code != 200:
            raise RuntimeError(
                f"OpenRouter HTTP {r.status_code}: {r.text[:200]}"
            )

        data = r.json()
        try:
            answer = data["choices"][0]["message"]["content"]
        except (KeyError, IndexError, TypeError) as e:
            raise RuntimeError(f"OpenRouter: неожиданный формат: {e}")

        if isinstance(answer, list):
            answer = " ".join(
                p.get("text", "") for p in answer
                if isinstance(p, dict) and p.get("type") == "text"
            )
        answer = (answer or "").strip()
        if not answer:
            raise RuntimeError("OpenRouter: пустой ответ")
        return answer

    # ---------- Хелперы ----------
    def _build_user_prompt(self, prompt: str) -> str:
        """
        Ollama: вшить системный промпт + историю в текст.
        """
        sp = (self.config.system_prompt or "").strip()

        history_text = ""
        if self.memory.enabled:
            turns = self.memory.get_turns(include_images=False)
            if turns:
                history_text = "\n\nПРЕДЫДУЩИЙ ДИАЛОГ:\n"
                for i, t in enumerate(turns, 1):
                    history_text += (
                        f"[{i}] Пользователь: {t.user_text}\n"
                        f"[{i}] Ассистент: {t.assistant_text}\n"
                    )

        parts = []
        if sp:
            parts.append(sp)
        if history_text:
            parts.append(history_text.strip())
        parts.append(f"ЗАДАНИЕ:\n{prompt}")
        return "\n\n".join(parts)

    @staticmethod
    def _image_to_base64(image: Image.Image) -> str:
        if image.mode != "RGB":
            image = image.convert("RGB")
        buf = io.BytesIO()
        image.save(buf, format="JPEG", quality=85)
        return base64.standard_b64encode(buf.getvalue()).decode("utf-8")


# ============================================================
#  Ассистент
# ============================================================
class GhostAssistant:
    def __init__(self, config: AppConfig):
        self.config = config

        self.ui = GhostUI(
            alpha=config.window_alpha,
            alpha_min=config.alpha_min,
            alpha_max=config.alpha_max,
        )

        self.memory = ConversationMemory(
            max_pairs=config.max_context_pairs,
            persist=config.persist_context,
        )

        self.client = VisionClient(config, self.memory)
        self.binder = KeyBinder()
        self.capture = CaptureAreaController(
            base_offset=config.screenshot_offset,
            step=config.capture_step,
        )
        self.capture.set_resize_callback(self.ui.apply_size)

        self._setup_hotkeys()
        self._start_position_loop()

        ctx_status = (
            f"{self.memory.size}/{self.config.max_context_pairs} пар"
            if self.memory.enabled else "выключен"
        )
        self.ui.log(f"👻 Готов. Контекст: {ctx_status}")

        logger.info("✓ Приложение запущено")
        logger.info("  Backend:        %s", config.backend)
        logger.info("  System prompt:  %.50s…", config.system_prompt)
        logger.info("  User prompt:    %.50s…", config.user_prompt)

    def _setup_hotkeys(self) -> None:
        hk = self.config.hotkeys
        self.binder.register("screenshot",    hk["screenshot"],    self._on_screenshot)
        self.binder.register("toggle_follow", hk["toggle_follow"], self.ui.toggle_follow)
        self.binder.register("reset",         hk["reset"],         self._on_reset)

        self.capture.bind_all(self.config.capture_keys)
        self._bind_visibility()
        self._bind_context()

    def _bind_visibility(self) -> None:
        vk = self.config.visibility_keys
        actions = {
            "increase_alpha":      lambda: self.ui.adjust_alpha(+self.config.alpha_step),
            "decrease_alpha":      lambda: self.ui.adjust_alpha(-self.config.alpha_step),
            "toggle_clickthrough": self.ui.toggle_clickthrough,
            "toggle_invisible":    self.ui.toggle_invisible,
        }
        self._visibility_handles = {}
        for action, method in actions.items():
            key = vk.get(action, "").strip().lower()
            if not key:
                continue
            try:
                self._visibility_handles[action] = keyboard.add_hotkey(
                    key, method
                )
            except (ValueError, KeyError) as e:
                logger.error("Невалидный visibility_key '%s': %s", key, e)

    def _bind_context(self) -> None:
        self._context_handle = None
        self._kill_handle = None

        key = (self.config.reset_context_key or "").strip().lower()
        if key:
            try:
                self._context_handle = keyboard.add_hotkey(
                    key, self._on_reset_context
                )
            except (ValueError, KeyError) as e:
                logger.error("Невалидный reset_context_key '%s': %s", key, e)

        if self.config.kill_key:
            try:
                self._kill_handle = keyboard.add_hotkey(
                    self.config.kill_key, self._kill_app
                )
            except (ValueError, KeyError) as e:
                logger.error("Невалидный kill_key '%s': %s",
                             self.config.kill_key, e)

    def _unbind_all_extra(self) -> None:
        for handle in getattr(self, "_visibility_handles", {}).values():
            try:
                keyboard.remove_hotkey(handle)
            except (KeyError, ValueError):
                pass

        for handle_attr in ("_context_handle", "_kill_handle"):
            h = getattr(self, handle_attr, None)
            if h:
                try:
                    keyboard.remove_hotkey(h)
                except (KeyError, ValueError):
                    pass
                setattr(self, handle_attr, None)

    def _on_reset_context(self) -> None:
        self.memory.clear()
        self.ui.log("🧹 Контекст очищен")

    def _on_screenshot(self) -> None:
        if self.ui.is_invisible():
            return
        threading.Thread(target=self.process_screen, daemon=True).start()

    def _on_reset(self) -> None:
        self.capture.reset()
        self.ui.log("🔄 Размер UI сброшен")

    def _start_position_loop(self) -> None:
        def tick():
            try:
                mx, my = pyautogui.position()
                self.ui.set_position(mx + 15, my + 15)
            except Exception as e:
                logger.debug("position tick: %s", e)
            self.ui.schedule(tick)
        tick()

    def process_screen(self) -> None:
        from license_manager import load_license
        lic = load_license()
        if not lic or lic.is_expired:
            self.ui.log("[Лицензия повреждена]")
            self.ui.set_state(WindowState.ERROR)
            return
        
        self.ui.log("Анализирую...")
        self.ui.set_state(WindowState.PROCESSING)
        try:
            x, y, w, h = self.ui.get_geometry()
            region = (x, y, w + self.config.screenshot_offset,
                      h + self.config.screenshot_offset)
            screenshot = pyautogui.screenshot(region=region)
            logger.info("Скриншот: %s", screenshot.size)

            # analyze() сам возьмёт user_prompt из конфига
            answer = self.client.analyze(screenshot)
            logger.info("✓ Ответ: %s...", answer[:50])
            self.ui.log(answer)
            self.ui.set_state(WindowState.IDLE)

        except ConnectionError as e:
            logger.error("%s", e); self.ui.log("[Нет подключения]")
            self.ui.set_state(WindowState.ERROR)
        except TimeoutError as e:
            logger.error("%s", e); self.ui.log("[Timeout]")
            self.ui.set_state(WindowState.ERROR)
        except RuntimeError as e:
            logger.error("API: %s", e); self.ui.log(f"[API: {type(e).__name__}]")
            self.ui.set_state(WindowState.ERROR)
        except Exception as e:
            logger.error("Ошибка: %s", e, exc_info=True)
            self.ui.log(f"[Ошибка: {type(e).__name__}]")
            self.ui.set_state(WindowState.ERROR)

    def _kill_app(self) -> None:
        logger.warning("💀 Kill switch активирован!")
        try:
            keyboard.unhook_all()
        except Exception:
            pass
        try:
            self.ui.destroy()
        except Exception:
            pass
        os._exit(0)

    def run(self) -> None:
        self.ui.mainloop()

    def shutdown(self) -> None:
        self.binder.unregister_all()
        self.capture.unbind_all()
        self._unbind_all_extra()
        self.ui.destroy()


def main() -> None:
    logger.info("Запуск Ghost-AI")
    try:
        SetupUI(on_complete=_launch_assistant)
    except SystemExit:
        return
    except Exception as e:
        logger.critical("Ошибка setup: %s", e, exc_info=True)


def _launch_assistant(config: AppConfig) -> None:
    app = GhostAssistant(config)
    try:
        app.run()
    except KeyboardInterrupt:
        logger.info("Остановлено пользователем")
    finally:
        app.shutdown()


if __name__ == "__main__":
    main()
