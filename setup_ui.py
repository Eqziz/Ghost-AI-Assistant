"""
setup_ui.py — стартовое окно настроек Ghost-AI.
Включает 4 секции: backend, системный промпт, основные хоткеи,
ресайз UI, прозрачность/видимость, kill switch.
"""
import logging
import tkinter as tk
from tkinter import ttk, messagebox
from typing import Callable, Optional, Dict

import keyboard

from config import AppConfig

logger = logging.getLogger(__name__)


class SetupUI:
    def __init__(self, on_complete: Callable[[AppConfig], None]):
        self.on_complete = on_complete
        self.config = AppConfig.load()

        self._listening_action: Optional[str] = None
        self._listening_kind: Optional[str] = None
        self._listening_top: Optional[tk.Toplevel] = None
        self._listen_hook = None

        self.root = tk.Tk()
        self.root.title("Ghost-AI — Setup")
        self.root.geometry("620x900")
        self.root.minsize(620, 900)
        self.root.resizable(False, False)
        self.root.configure(bg="#1e1e1e")

        self._build_ui()
        self._load_into_fields()

        self.root.protocol("WM_DELETE_WINDOW", self._on_close)
        self.root.mainloop()

    # ============================================================
    #  Построение UI
    # ============================================================

    def _build_ui(self) -> None:
        style = ttk.Style()
        try:
            style.theme_use("clam")
        except tk.TclError:
            pass

        for k, v in {
            "TFrame":      {"background": "#1e1e1e"},
            "TLabel":      {"background": "#1e1e1e", "foreground": "#ffffff"},
            "TLabelframe": {"background": "#1e1e1e", "foreground": "#00d4ff"},
            "TLabelframe.Label": {
                "background": "#1e1e1e", "foreground": "#00d4ff",
                "font": ("Segoe UI", 10, "bold"),
            },
            "TButton":     {"padding": 6},
            "TRadiobutton": {"background": "#1e1e1e", "foreground": "#ffffff"},
            "TEntry":      {"fieldbackground": "#2a2a2a",
                            "foreground": "#ffffff", "insertcolor": "#ffffff"},
            "TSpinbox":    {"fieldbackground": "#2a2a2a", "foreground": "#ffffff"},
        }.items():
            style.configure(k, **v)

        # ===== Canvas + scroll =====
        outer = ttk.Frame(self.root)
        outer.pack(fill="both", expand=True)

        canvas = tk.Canvas(outer, bg="#1e1e1e", highlightthickness=0)
        scrollbar = ttk.Scrollbar(outer, orient="vertical",
                                  command=canvas.yview)
        scroll_frame = ttk.Frame(canvas)
        scroll_frame.bind(
            "<Configure>",
            lambda e: canvas.configure(scrollregion=canvas.bbox("all"))
        )
        canvas.create_window((0, 0), window=scroll_frame, anchor="nw")
        canvas.configure(yscrollcommand=scrollbar.set)
        canvas.pack(side="left", fill="both", expand=True,
                    padx=(20, 0), pady=10)
        scrollbar.pack(side="right", fill="y", padx=(0, 20), pady=10)

        def _on_mousewheel(event):
            canvas.yview_scroll(int(-1 * (event.delta / 120)), "units")
        canvas.bind_all("<MouseWheel>", _on_mousewheel)

        c = scroll_frame

        # --- Заголовок ---
        tk.Label(c, text="👻 Ghost-AI Setup",
                 font=("Segoe UI", 18, "bold"),
                 bg="#1e1e1e", fg="#00d4ff").pack(pady=(5, 5))
        tk.Label(c, text="Выберите режим и настройте горячие клавиши",
                 font=("Segoe UI", 9),
                 bg="#1e1e1e", fg="#888888").pack(pady=(0, 10))

        # ============ Backend ============
        bf = ttk.LabelFrame(c, text="Режим работы", padding=10)
        bf.pack(fill="x", pady=5)

        self.backend_var = tk.StringVar(value=self.config.backend or "ollama")
        ttk.Radiobutton(bf, text="🦙 Ollama (локально)",
                        variable=self.backend_var, value="ollama",
                        command=self._on_backend_change).grid(
            row=0, column=0, sticky="w", pady=2)
        ttk.Radiobutton(bf, text="☁️  OpenRouter (облако)",
                        variable=self.backend_var, value="openrouter",
                        command=self._on_backend_change).grid(
            row=1, column=0, sticky="w", pady=2)

        ttk.Label(bf, text="Ollama URL:").grid(
            row=2, column=0, sticky="w", pady=(8, 0))
        self.ollama_url_var = tk.StringVar()
        ttk.Entry(bf, textvariable=self.ollama_url_var).grid(
            row=2, column=1, sticky="ew", pady=(8, 0), padx=(10, 0))

        ttk.Label(bf, text="Модель Ollama:").grid(
            row=3, column=0, sticky="w", pady=4)
        self.ollama_model_var = tk.StringVar()
        self.ollama_model_entry = ttk.Entry(bf, textvariable=self.ollama_model_var)
        self.ollama_model_entry.grid(row=3, column=1, sticky="ew",
                                     pady=4, padx=(10, 0))

        ttk.Label(bf, text="OpenRouter API Key:").grid(
            row=4, column=0, sticky="w", pady=4)
        self.openrouter_key_var = tk.StringVar()
        self.openrouter_key_entry = ttk.Entry(
            bf, textvariable=self.openrouter_key_var, show="•"
        )
        self.openrouter_key_entry.grid(
            row=4, column=1, sticky="ew", pady=4, padx=(10, 0))

        ttk.Label(bf, text="Модель OpenRouter:").grid(
            row=5, column=0, sticky="w", pady=4)
        self.openrouter_model_var = tk.StringVar(value="llama-4-scout")
        ttk.Entry(bf, textvariable=self.openrouter_model_var,
                  state="readonly").grid(
            row=5, column=1, sticky="ew", pady=4, padx=(10, 0))

        bf.columnconfigure(1, weight=1)

        # ============ Системный промпт ============
        prompts_frame = ttk.LabelFrame(
            c, text="📝 Промпты (что AI получит на каждом скриншоте)", padding=10
        )
        prompts_frame.pack(fill="x", pady=8)

        # Системный промпт — роль/стиль
        tk.Label(prompts_frame,
                 text="Системный промпт (задаёт роль и стиль ответа):",
                 bg="#1e1e1e", fg="#00d4ff",
                 font=("Segoe UI", 9, "bold")).pack(anchor="w")
        self.system_prompt_text = tk.Text(
            prompts_frame, height=3, bg="#2a2a2a", fg="#ffffff",
            insertbackground="#ffffff", wrap=tk.WORD,
            font=("Segoe UI", 9),
        )
        self.system_prompt_text.pack(fill="x", pady=(2, 8))

        # Пользовательский промпт — что делать с изображением
        tk.Label(prompts_frame,
                 text="Промпт пользователя (отправляется с КАЖДЫМ скриншотом):",
                 bg="#1e1e1e", fg="#00d4ff",
                 font=("Segoe UI", 9, "bold")).pack(anchor="w")
        self.user_prompt_text = tk.Text(
            prompts_frame, height=2, bg="#2a2a2a", fg="#ffffff",
            insertbackground="#ffffff", wrap=tk.WORD,
            font=("Segoe UI", 9),
        )
        self.user_prompt_text.pack(fill="x", pady=(2, 0))


        # ============ Основные хоткеи ============
        hk = ttk.LabelFrame(c, text="🔥 Основные горячие клавиши", padding=10)
        hk.pack(fill="x", pady=8)
        self.hotkey_labels = {}
        for i, (action, label) in enumerate([
            ("screenshot",    "📸 Скриншот и анализ"),
            ("toggle_follow", "🎯 Следование за курсором"),
            ("reset",         "🔄 Сброс UI"),
        ]):
            ttk.Label(hk, text=label).grid(row=i, column=0,
                                           sticky="w", pady=2)
            var = tk.StringVar(value=self.config.hotkeys.get(action, "—"))
            self.hotkey_labels[action] = var
            ttk.Label(hk, textvariable=var, width=18,
                      foreground="#00d4ff").grid(row=i, column=1, padx=10)
            ttk.Button(hk, text="Изменить",
                       command=lambda a=action: self._start_rebind(a, "hotkey")
                       ).grid(row=i, column=2, padx=5)
        hk.columnconfigure(0, weight=1)

        # ============ Ресайз UI ============
        rs = ttk.LabelFrame(c, text="📐 Клавиши изменения размера UI", padding=10)
        rs.pack(fill="x", pady=8)
        self.capture_key_labels = {}
        for i, (action, label) in enumerate([
            ("expand_right",  "→ Расширить вправо"),
            ("shrink_left",   "← Сузить слева"),
            ("expand_down",   "↓ Расширить вниз"),
            ("shrink_up",     "↑ Сузить сверху"),
            ("expand_all",    "⤢ Расширить всё"),
            ("shrink_all",    "⤡ Сузить всё"),
            ("reset_capture", "⏹ Сброс"),
        ], start=1):
            ttk.Label(rs, text=label).grid(row=i, column=0,
                                           sticky="w", pady=2)
            var = tk.StringVar(value=self.config.capture_keys.get(action, "—"))
            self.capture_key_labels[action] = var
            ttk.Label(rs, textvariable=var, width=22,
                      foreground="#00d4ff").grid(row=i, column=1, padx=10)
            ttk.Button(rs, text="Изменить",
                       command=lambda a=action: self._start_rebind(a, "capture")
                       ).grid(row=i, column=2, padx=5)
        rs.columnconfigure(0, weight=1)

        # ============ Прозрачность / видимость ============
        vs = ttk.LabelFrame(
            c, text="👁 Прозрачность, click-through, невидимка", padding=10
        )
        vs.pack(fill="x", pady=8)
        self.visibility_key_labels = {}
        for i, (action, label) in enumerate([
            ("increase_alpha",      "🔼 Увеличить видимость (менее прозрачно)"),
            ("decrease_alpha",      "🔽 Уменьшить видимость (прозрачнее)"),
            ("toggle_clickthrough", "🖱 Пропускать клики через окно"),
            ("toggle_invisible",    "👻 Режим невидимки (полностью скрыть)"),
        ], start=1):
            ttk.Label(vs, text=label).grid(row=i, column=0,
                                           sticky="w", pady=2)
            var = tk.StringVar(value=self.config.visibility_keys.get(action, "—"))
            self.visibility_key_labels[action] = var
            ttk.Label(vs, textvariable=var, width=22,
                      foreground="#00d4ff").grid(row=i, column=1, padx=10)
            ttk.Button(vs, text="Изменить",
                       command=lambda a=action: self._start_rebind(a, "visibility")
                       ).grid(row=i, column=2, padx=5)
        vs.columnconfigure(0, weight=1)

        # ============ Kill switch ============
        ks = ttk.LabelFrame(c, text="💀 Kill Switch (экстренное завершение)", padding=10)
        ks.pack(fill="x", pady=8)
        ttk.Label(ks, text="Горячая клавиша для полного выхода:").grid(
            row=0, column=0, sticky="w", pady=2)
        self.kill_key_var = tk.StringVar(value=self.config.kill_key)
        ttk.Label(ks, textvariable=self.kill_key_var, width=22,
                  foreground="#ff5555").grid(row=0, column=1, padx=10)
        ttk.Button(ks, text="Изменить",
                   command=lambda: self._start_rebind("kill", "kill")
                   ).grid(row=0, column=2, padx=5)
        ks.columnconfigure(0, weight=1)

                # ============ Контекст диалога ============
        ctx_frame = ttk.LabelFrame(
            c, text="💬 Контекст диалога", padding=10
        )
        ctx_frame.pack(fill="x", pady=8)

        # Лимит пар
        ttk.Label(ctx_frame, text="Максимум пар (вопрос-ответ):").grid(
            row=0, column=0, sticky="w", pady=2)
        self.max_pairs_var = tk.IntVar(value=self.config.max_context_pairs)
        ttk.Spinbox(ctx_frame, from_=0, to=50, increment=1,
                    textvariable=self.max_pairs_var, width=8).grid(
            row=0, column=1, sticky="w", padx=10, pady=2)
        ttk.Label(ctx_frame, text="(0 = выключить контекст)",
                  foreground="#888888").grid(
            row=0, column=2, sticky="w", pady=2)

        # Чекбокс: сохранять в файл
        self.persist_var = tk.BooleanVar(value=self.config.persist_context)
        ttk.Checkbutton(
            ctx_frame,
            text="Сохранять контекст в файл между запусками",
            variable=self.persist_var,
        ).grid(row=1, column=0, columnspan=3, sticky="w", pady=4)

        # Чекбокс: изображения в контексте
        self.include_images_var = tk.BooleanVar(
            value=self.config.include_images_in_context
        )
        ttk.Checkbutton(
            ctx_frame,
            text="Передавать изображения из истории (тратит больше токенов)",
            variable=self.include_images_var,
        ).grid(row=2, column=0, columnspan=3, sticky="w", pady=4)

        # Хоткей сброса
        ttk.Label(ctx_frame, text="Сброс контекста:").grid(
            row=3, column=0, sticky="w", pady=(8, 2))
        self.reset_context_key_var = tk.StringVar(
            value=self.config.reset_context_key
        )
        ttk.Label(ctx_frame, textvariable=self.reset_context_key_var,
                  width=22, foreground="#00d4ff").grid(
            row=3, column=1, sticky="w", padx=10, pady=(8, 2))
        ttk.Button(
            ctx_frame, text="Изменить",
            command=lambda: self._start_rebind("reset_context", "context_key"),
        ).grid(row=3, column=2, padx=5, pady=(8, 2))

        ctx_frame.columnconfigure(0, weight=1)


        # ============ Кнопки снизу (закреплены) ============
        btn_frame = ttk.Frame(self.root)
        btn_frame.pack(side="bottom", fill="x", padx=20, pady=15)
        tk.Frame(self.root, bg="#00d4ff", height=1).pack(
            side="bottom", fill="x")
        ttk.Button(btn_frame, text="Отмена", command=self._on_close).pack(
            side="right", padx=5)
        ttk.Button(btn_frame, text="💾 Сохранить и запустить",
                   command=self._save_and_start).pack(side="right", padx=5)

    # ============================================================
    #  Загрузка / валидация
    # ============================================================

    def _load_into_fields(self) -> None:
        self.ollama_url_var.set(self.config.ollama_url)
        self.ollama_model_var.set(self.config.ollama_model)
        self.openrouter_key_var.set(self.config.openrouter_api_key)

        # Промпты
        self.system_prompt_text.delete("1.0", tk.END)
        self.system_prompt_text.insert("1.0", self.config.system_prompt)
        self.user_prompt_text.delete("1.0", tk.END)
        self.user_prompt_text.insert("1.0", self.config.user_prompt)

        self._on_backend_change()

    def _on_backend_change(self) -> None:
        is_ollama = (self.backend_var.get() == "ollama")
        self.ollama_model_entry.config(state="normal" if is_ollama else "disabled")
        self.openrouter_key_entry.config(state="normal" if not is_ollama else "disabled")

    # ============================================================
    #  Rebind (общий для всех категорий)
    # ============================================================

    def _start_rebind(self, action: str, kind: str) -> None:
        if self._listening_action is not None:
            return
        self._listening_action = action
        self._listening_kind = kind

        win = tk.Toplevel(self.root)
        win.title("Нажмите клавишу")
        win.geometry("340x170")
        win.configure(bg="#1e1e1e")
        win.transient(self.root)
        win.grab_set()
        win.resizable(False, False)
        self._listening_top = win

        labels = {
            "screenshot":          "скриншота",
            "toggle_follow":       "следования за курсором",
            "reset":               "сброса UI",
            "expand_right":        "расширения вправо",
            "shrink_left":         "сужения слева",
            "expand_down":         "расширения вниз",
            "shrink_up":           "сужения сверху",
            "expand_all":          "расширения всего",
            "shrink_all":          "сужения всего",
            "reset_capture":       "сброса размера",
            "increase_alpha":      "увеличения видимости",
            "decrease_alpha":      "уменьшения видимости",
            "toggle_clickthrough": "пропуска кликов",
            "toggle_invisible":    "режима невидимки",
            "kill":                "экстренного завершения",
        }
        action_label = labels.get(action, action)

        tk.Label(
            win, text=f"Нажмите новую клавишу для {action_label}…",
            bg="#1e1e1e", fg="#ffffff",
            font=("Segoe UI", 10), wraplength=300,
        ).pack(pady=(20, 5))
        tk.Label(
            win,
            text="Примеры: right, ctrl+up, mouse wheel up\n(Esc — отмена)",
            bg="#1e1e1e", fg="#888888",
            font=("Segoe UI", 8), justify="center",
        ).pack()
        ttk.Button(
            win, text="Отмена",
            command=lambda: self._finish_rebind(None),
        ).pack(pady=10)

        self._listen_hook = keyboard.on_press(self._on_rebind_key,
                                              suppress=False)

    def _on_rebind_key(self, event) -> None:
        key = (event.name or "").lower()
        if not key:
            return
        self.root.after(0, lambda: self._finish_rebind(key))

    def _finish_rebind(self, new_key: Optional[str]) -> None:
        if self._listen_hook is not None:
            try:
                keyboard.unhook(self._listen_hook)
            except Exception:
                pass
            self._listen_hook = None
        if self._listening_top is not None:
            try:
                self._listening_top.grab_release()
                self._listening_top.destroy()
            except tk.TclError:
                pass
            self._listening_top = None

        action = self._listening_action
        kind = self._listening_kind
        self._listening_action = None
        self._listening_kind = None

        if not new_key or new_key == "esc":
            return

        if kind == "hotkey":
            self.config.hotkeys[action] = new_key
            if action in self.hotkey_labels:
                self.hotkey_labels[action].set(new_key)
        elif kind == "capture":
            self.config.capture_keys[action] = new_key
            if action in self.capture_key_labels:
                self.capture_key_labels[action].set(new_key)
        elif kind == "visibility":
            self.config.visibility_keys[action] = new_key
            if action in self.visibility_key_labels:
                self.visibility_key_labels[action].set(new_key)
        elif kind == "kill":
            self.config.kill_key = new_key
            if hasattr(self, "kill_key_var"):
                self.kill_key_var.set(new_key)
        elif kind == "context_key":
            self.config.reset_context_key = new_key
            if hasattr(self, "reset_context_key_var"):
                self.reset_context_key_var.set(new_key)


    # ============================================================
    #  Сохранение
    # ============================================================

    def _save_and_start(self) -> None:
        backend = self.backend_var.get()
        ollama_url = self.ollama_url_var.get().strip() or "http://localhost:11434"
        ollama_model = self.ollama_model_var.get().strip()
        or_key = self.openrouter_key_var.get().strip()

        if backend == "ollama" and not ollama_model:
            messagebox.showerror("Ошибка", "Укажите модель Ollama.")
            return
        if backend == "openrouter" and not or_key:
            messagebox.showerror("Ошибка", "Введите API-ключ OpenRouter.")
            return

        self.config.backend = backend
        self.config.ollama_url = ollama_url
        self.config.ollama_model = ollama_model
        self.config.openrouter_api_key = or_key
        self.config.system_prompt = self.system_prompt_text.get(
            "1.0", tk.END
        ).strip()

        self.config.max_context_pairs = int(self.max_pairs_var.get())
        self.config.persist_context = bool(self.persist_var.get())
        self.config.include_images_in_context = bool(
            self.include_images_var.get()
        )
        self.config.save()

        self.config.system_prompt = self.system_prompt_text.get(
            "1.0", tk.END
        ).strip()
        self.config.user_prompt = self.user_prompt_text.get(
            "1.0", tk.END
        ).strip()
        self.config.max_context_pairs = int(self.max_pairs_var.get())
        self.config.persist_context = bool(self.persist_var.get())
        self.config.include_images_in_context = bool(
            self.include_images_var.get()
        )


        self.config.save()

        try:
            self.root.destroy()
        except tk.TclError:
            pass

        logger.info("Setup завершён: backend=%s", backend)
        try:
            self.on_complete(self.config)
        except Exception as e:
            logger.error("Ошибка в on_complete: %s", e, exc_info=True)

    def _on_close(self) -> None:
        if messagebox.askokcancel(
            "Выход",
            "Прервать настройку? Ghost-AI не запустится без конфигурации."
        ):
            try:
                if self._listen_hook is not None:
                    keyboard.unhook(self._listen_hook)
            except Exception:
                pass
            self.root.destroy()
            raise SystemExit(0)
