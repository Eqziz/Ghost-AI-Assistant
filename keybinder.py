"""
keybinder.py — управление ремаппингом горячих клавиш.

Идея: keyboard.add_hotkey принимает строку (например 'esc', 'right ctrl',
'ctrl+shift+s'). Храним имена в config.AppConfig.hotkeys (dict).
При ремаппинге снимаем старый хоткей и ставим новый.
"""
import logging
from typing import Callable, Dict, Optional

import keyboard

logger = logging.getLogger(__name__)


class KeyBinder:
    """
    Управляет hotkey → callback связками.
    Устойчив к переустановке (можно перебиндить на лету).
    """

    def __init__(self):
        # (action_name) -> current_key_string
        self._bindings: Dict[str, str] = {}
        # (action_name) -> callback
        self._callbacks: Dict[str, Callable[[], None]] = {}
        # keyboard требует уникальный handle для снятия
        # (action_name) -> keyboard.add_hotkey handle
        self._handles: Dict[str, object] = {}

    # ============================================================
    #  Регистрация
    # ============================================================

    def register(self, action: str, key: str, callback: Callable[[], None]) -> None:
        """
        Зарегистрировать/перебиндить действие.
        """
        self.unregister(action)            # снять старый
        self._bindings[action] = key
        self._callbacks[action] = callback
        self._handles[action] = keyboard.add_hotkey(key, callback)
        logger.info("Hotkey: %-15s → %s", action, key)

    def unregister(self, action: str) -> None:
        """Снять ремаппинг одного действия."""
        if action not in self._handles:
            return
        try:
            handle = self._handles.pop(action)
            keyboard.remove_hotkey(handle)
        except (KeyError, ValueError) as e:
            logger.debug("remove_hotkey %s: %s", action, e)
        self._bindings.pop(action, None)
        self._callbacks.pop(action, None)

    def unregister_all(self) -> None:
        for action in list(self._handles.keys()):
            self.unregister(action)

    # ============================================================
    #  Ремаппинг (новый ключ → валидация → переустановка)
    # ============================================================

    def rebind(self, action: str, new_key: str) -> bool:
        """
        Перебиндить действие. Возвращает True при успехе.
        При провале (клавиша уже занята или невалидна) — откат.
        """
        new_key = new_key.strip().lower()
        if not new_key:
            logger.warning("Пустая клавиша для '%s'", action)
            return False

        # Проверяем конфликты с другими нашими хоткеями
        for other_action, other_key in self._bindings.items():
            if other_action != action and other_key == new_key:
                logger.warning(
                    "Конфликт: '%s' уже занят хоткеем '%s'",
                    new_key, other_action,
                )
                return False

        callback = self._callbacks.get(action)
        if callback is None:
            logger.warning("Нет callback для '%s'", action)
            return False

        old_key = self._bindings.get(action)

        try:
            self.register(action, new_key, callback)
            return True
        except (ValueError, KeyError) as e:
            logger.error("Невалидная клавиша '%s': %s", new_key, e)
            # Откат
            if old_key:
                try:
                    self.register(action, old_key, callback)
                except Exception:
                    pass
            return False

    def get_key(self, action: str) -> Optional[str]:
        return self._bindings.get(action)

    def all_bindings(self) -> Dict[str, str]:
        return dict(self._bindings)
