"""
capture_area.py — управление размером UI-окна через клавиши/колесо мыши.

Хранит «дельты» и применяет их к GhostUI во время работы.
"""
import logging
import threading
from dataclasses import dataclass
from typing import Dict, Optional

logger = logging.getLogger(__name__)


@dataclass
class SizeRect:
    width: int
    height: int

    def clamp(self, min_val: int = 100, max_val: int = 4000) -> "SizeRect":
        return SizeRect(
            width=max(min_val, min(self.width, max_val)),
            height=max(min_val, min(self.height, max_val)),
        )


class CaptureAreaController:
    """
    Хранит текущий размер UI-окна. GhostUI подписывается через
    set_resize_callback() и применяет новый размер при изменениях.
    """

    DEFAULT_W = 350
    DEFAULT_H = 200

    def __init__(self, base_offset: int = 50, step: int = 25):
        self._step = step
        self._rect = SizeRect(
            width=self.DEFAULT_W + base_offset,
            height=self.DEFAULT_H + base_offset,
        )
        self._lock = threading.Lock()
        self._handles: Dict[str, object] = {}
        self._resize_callback = None

    def set_resize_callback(self, callback) -> None:
        """Регистрация callback(width, height) — вызывается при изменении."""
        self._resize_callback = callback

    def get(self) -> SizeRect:
        with self._lock:
            return SizeRect(self._rect.width, self._rect.height)

    def reset(self) -> None:
        self.set(self.DEFAULT_W, self.DEFAULT_H)

    def _modify(self, d_w: int = 0, d_h: int = 0) -> None:
        with self._lock:
            self._rect = SizeRect(
                self._rect.width + d_w,
                self._rect.height + d_h,
            ).clamp()
            r = self._rect
        logger.info("UI-окно: %dx%d", r.width, r.height)
        if self._resize_callback:
            try:
                self._resize_callback(r.width, r.height)
            except Exception as e:
                logger.error("resize_callback error: %s", e)

    def set(self, width: int, height: int) -> None:
        with self._lock:
            self._rect = SizeRect(width, height).clamp()
            r = self._rect
        logger.info("UI-окно: %dx%d", r.width, r.height)
        if self._resize_callback:
            try:
                self._resize_callback(r.width, r.height)
            except Exception as e:
                logger.error("resize_callback error: %s", e)

    # ---------- Действия ----------
    def expand_right(self)  -> None: self._modify(d_w=+self._step)
    def shrink_left(self)   -> None: self._modify(d_w=-self._step)
    def expand_down(self)   -> None: self._modify(d_h=+self._step)
    def shrink_up(self)     -> None: self._modify(d_h=-self._step)
    def expand_all(self)    -> None: self._modify(d_w=+self._step, d_h=+self._step)
    def shrink_all(self)    -> None: self._modify(d_w=-self._step, d_h=-self._step)
    def reset_capture(self) -> None: self.reset()

    # ---------- Регистрация хоткеев ----------
    def bind_all(self, keymap: Dict[str, str]) -> None:
        import keyboard
        actions = {
            "expand_right":  self.expand_right,
            "shrink_left":   self.shrink_left,
            "expand_down":   self.expand_down,
            "shrink_up":     self.shrink_up,
            "expand_all":    self.expand_all,
            "shrink_all":    self.shrink_all,
            "reset_capture": self.reset_capture,
        }
        self.unbind_all()
        for action, method in actions.items():
            key = keymap.get(action, "").strip().lower()
            if not key:
                continue
            try:
                self._handles[action] = keyboard.add_hotkey(key, method)
                logger.info("UI size: %-15s → %s", action, key)
            except (ValueError, KeyError) as e:
                logger.error("Невалидный хоткей '%s' для '%s': %s",
                             key, action, e)

    def unbind_all(self) -> None:
        import keyboard
        for handle in self._handles.values():
            try:
                keyboard.remove_hotkey(handle)
            except (KeyError, ValueError):
                pass
        self._handles.clear()
