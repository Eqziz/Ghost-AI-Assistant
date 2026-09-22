"""
conversation.py — управление контекстом диалога.

Хранит пары (изображение + текст вопроса → ответ) в FIFO-очереди
с ограничением размера. Опционально сохраняется в JSON.
"""
import base64
import io
import json
import logging
import os
import threading
from collections import deque
from dataclasses import dataclass, asdict
from datetime import datetime
from typing import Deque, List, Optional

from PIL import Image

logger = logging.getLogger(__name__)

CONTEXT_PATH = os.path.join(
    os.path.expanduser("~"),
    ".ghost_ai_context.json"
)


@dataclass
class Turn:
    """Один ход диалога: пользователь отправил изображение+вопрос, AI ответил."""
    timestamp: str
    user_text: str          # текст промпта
    user_image_b64: str     # base64 изображения (или пустая строка)
    assistant_text: str     # ответ AI

    def to_dict(self) -> dict:
        return asdict(self)

    @classmethod
    def from_dict(cls, d: dict) -> "Turn":
        return cls(
            timestamp=d.get("timestamp", ""),
            user_text=d.get("user_text", ""),
            user_image_b64=d.get("user_image_b64", ""),
            assistant_text=d.get("assistant_text", ""),
        )


class ConversationMemory:
    """
    Потокобезопасная память диалога.
    Хранит последние N пар (image, prompt, answer).
    """

    def __init__(self, max_pairs: int = 10, persist: bool = True):
        self._max_pairs = max(0, max_pairs)
        self._persist = persist
        self._lock = threading.Lock()
        # deque с maxlen автоматически вытесняет старые элементы
        self._turns: Deque[Turn] = deque(maxlen=self._max_pairs or 1)
        if self._persist:
            self._load()

    # ============================================================
    #  Конфигурация
    # ============================================================

    def set_limits(self, max_pairs: int, persist: bool) -> None:
        """Изменить лимит хранения. Сохраняет уже существующие turn'ы."""
        with self._lock:
            old = list(self._turns)
            self._max_pairs = max(0, max_pairs)
            self._persist = persist
            self._turns = deque(old, maxlen=self._max_pairs or 1)
            if self._persist:
                self._save()

    @property
    def enabled(self) -> bool:
        return self._max_pairs > 0

    @property
    def size(self) -> int:
        with self._lock:
            return len(self._turns)

    # ============================================================
    #  Управление
    # ============================================================

    def add_turn(self, user_text: str, image: Optional[Image.Image],
                 assistant_text: str) -> None:
        """Добавить новый ход в историю."""
        if not self.enabled:
            return

        img_b64 = ""
        if image is not None:
            img_b64 = self._image_to_b64(image)

        turn = Turn(
            timestamp=datetime.now().isoformat(timespec="seconds"),
            user_text=user_text,
            user_image_b64=img_b64,
            assistant_text=assistant_text,
        )

        with self._lock:
            self._turns.append(turn)
            if self._persist:
                self._save()

        logger.info("Контекст: добавлен turn (всего %d/%d)",
                    len(self._turns), self._max_pairs)

    def clear(self) -> None:
        """Очистить всю историю."""
        with self._lock:
            self._turns.clear()
            if self._persist:
                self._save()
        logger.info("Контекст очищен")

    def get_turns(self, include_images: bool = False) -> List[Turn]:
        """Вернуть список ходов (копию) для отправки в AI."""
        with self._lock:
            turns = list(self._turns)

        if not include_images:
            # Возвращаем только текст (изображения экономят токены)
            return [
                Turn(
                    timestamp=t.timestamp,
                    user_text=t.user_text,
                    user_image_b64="",
                    assistant_text=t.assistant_text,
                )
                for t in turns
            ]
        return turns

    # ============================================================
    #  Сериализация
    # ============================================================

    @staticmethod
    def _image_to_b64(image: Image.Image) -> str:
        if image.mode != "RGB":
            image = image.convert("RGB")
        buf = io.BytesIO()
        image.save(buf, format="JPEG", quality=70)
        return base64.standard_b64encode(buf.getvalue()).decode("utf-8")

    def _save(self) -> None:
        try:
            data = {
                "max_pairs": self._max_pairs,
                "turns": [t.to_dict() for t in self._turns],
            }
            with open(CONTEXT_PATH, "w", encoding="utf-8") as f:
                json.dump(data, f, ensure_ascii=False, indent=2)
        except OSError as e:
            logger.error("Не удалось сохранить контекст: %s", e)

    def _load(self) -> None:
        if not os.path.exists(CONTEXT_PATH):
            return
        try:
            with open(CONTEXT_PATH, "r", encoding="utf-8") as f:
                data = json.load(f)
            turns = [Turn.from_dict(t) for t in data.get("turns", [])]
            with self._lock:
                self._turns = deque(turns, maxlen=self._max_pairs or 1)
            logger.info("Загружено %d ходов из %s", len(turns), CONTEXT_PATH)
        except (OSError, json.JSONDecodeError) as e:
            logger.warning("Не удалось загрузить контекст: %s", e)
