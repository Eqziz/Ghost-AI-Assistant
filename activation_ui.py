"""
activation_ui.py — окно активации лицензии.
"""
import logging
import tkinter as tk
from tkinter import messagebox
from typing import Optional, Callable

from license_manager import (
    verify_key, save_license, load_license,
    format_license_info, LicenseData,
)

logger = logging.getLogger(__name__)


class ActivationUI:
    def __init__(self, on_activated: Callable[[LicenseData], None]):
        self.on_activated = on_activated
        self.activated: Optional[LicenseData] = None

        self.root = tk.Tk()
        self.root.title("Ghost AI — Активация")
        self.root.geometry("520x340")
        self.root.resizable(False, False)
        self.root.configure(bg="#1e1e1e")
        self.root.attributes("-topmost", True)

        self._build_ui()

        self.root.protocol("WM_DELETE_WINDOW", self._on_close)
        self.root.mainloop()

    def _build_ui(self) -> None:
        tk.Label(
            self.root, text="👻 Ghost AI",
            font=("Segoe UI", 24, "bold"),
            bg="#1e1e1e", fg="#00d4ff",
        ).pack(pady=(30, 5))

        tk.Label(
            self.root,
            text="Введите лицензионный ключ\n"
                 "(получен в @your_bot после оплаты)",
            font=("Segoe UI", 10),
            bg="#1e1e1e", fg="#888888",
            justify="center",
        ).pack(pady=(0, 20))

        # Поле ввода — длинное, т.к. формат base64.hex
        self.key_var = tk.StringVar()
        entry = tk.Entry(
            self.root, textvariable=self.key_var,
            font=("Consolas", 11),
            bg="#2a2a2a", fg="#ffffff",
            insertbackground="#ffffff",
            relief="flat", bd=5,
        )
        entry.pack(fill="x", padx=40, ipady=8)
        entry.focus_set()

        tk.Label(
            self.root,
            text="Формат: <base64>.<hmac_hex>",
            font=("Segoe UI", 8),
            bg="#1e1e1e", fg="#555555",
        ).pack(pady=(5, 20))

        btn_frame = tk.Frame(self.root, bg="#1e1e1e")
        btn_frame.pack(pady=10)

        tk.Button(
            btn_frame, text="✓ Активировать",
            font=("Segoe UI", 11, "bold"),
            bg="#00d4ff", fg="#000000",
            activebackground="#00b8e6",
            relief="flat", padx=25, pady=8,
            cursor="hand2",
            command=self._on_activate,
        ).pack(side="left", padx=5)

        tk.Button(
            btn_frame, text="💫 Купить в Telegram",
            font=("Segoe UI", 10),
            bg="#2a2a2a", fg="#ffffff",
            activebackground="#3a3a3a",
            relief="flat", padx=20, pady=8,
            cursor="hand2",
            command=self._on_buy,
        ).pack(side="left", padx=5)

        # Статус
        self.status_var = tk.StringVar(value="")
        tk.Label(
            self.root, textvariable=self.status_var,
            font=("Segoe UI", 9),
            bg="#1e1e1e", fg="#ff5555",
            wraplength=460,
        ).pack(pady=(15, 0))

        entry.bind("<Return>", lambda e: self._on_activate())

    def _on_activate(self) -> None:
        key = self.key_var.get().strip()
        if not key:
            self.status_var.set("⚠️ Введите ключ")
            return

        data = verify_key(key)
        if data is None:
            self.status_var.set(
                "❌ Невалидный ключ.\n"
                "Проверьте правильность ввода или купите новый."
            )
            return

        if data.is_expired:
            self.status_var.set(
                f"❌ Срок лицензии истёк ({data.expires_str}).\n"
                "Купите новую через @your_bot"
            )
            return

        if not save_license(key):
            self.status_var.set(
                "❌ Не удалось сохранить лицензию.\n"
                "Запустите от имени администратора."
            )
            return

        self.activated = data
        logger.info("Лицензия активирована: tg_id=%s", data.telegram_id)

        self.root.destroy()
        try:
            self.on_activated(data)
        except Exception as e:
            logger.error("Ошибка в on_activated: %s", e)

    def _on_buy(self) -> None:
        import webbrowser
        # Замените на свой username бота
        webbrowser.open("https://t.me/your_bot")

    def _on_close(self) -> None:
        self.root.destroy()
        raise SystemExit(0)
