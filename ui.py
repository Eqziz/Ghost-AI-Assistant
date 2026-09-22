"""
ui.py — прозрачное окно Ghost-AI с поддержкой:
  - динамической прозрачности
  - click-through (пропуск кликов)
  - режима невидимости
"""
import tkinter as tk
import ctypes
import threading
import logging
from enum import Enum
from typing import Optional, Callable

logger = logging.getLogger(__name__)


class WindowState(Enum):
    IDLE = "idle"
    PROCESSING = "processing"
    ERROR = "error"


class GhostUI:
    WINDOW_WIDTH = 350
    WINDOW_HEIGHT = 200
    MIN_WINDOW_WIDTH = 100
    MIN_WINDOW_HEIGHT = 50
    GRIP_SIZE = 15
    POSITION_UPDATE_MS = 10
    WDA_EXCLUDEFROMCAPTURE = 0x11

    # Windows extended window styles
    WS_EX_LAYERED = 0x00080000
    WS_EX_TRANSPARENT = 0x00000020        # click-through
    GWL_EXSTYLE = -20

    def __init__(self, alpha: float = 0.5,
                 alpha_min: float = 0.05,
                 alpha_max: float = 1.0):
        self.WINDOW_ALPHA = max(alpha_min, min(alpha, alpha_max))
        self._alpha_min = alpha_min
        self._alpha_max = alpha_max

        self.state: WindowState = WindowState.IDLE
        self.is_following: bool = True
        self.ui_lock = threading.Lock()
        self._drag_start_x: int = 0
        self._drag_start_y: int = 0
        self._on_follow_toggle: Optional[Callable[[bool], None]] = None

        # Состояния видимости
        self._is_clickthrough: bool = False
        self._is_invisible: bool = False

        self._setup_window()
        self._setup_widgets()
        self.activate_stealth()

    # ============================================================
    #  Инициализация
    # ============================================================

    def _setup_window(self) -> None:
        self.root = tk.Tk()
        self.root.title("Ghost-AI")
        self.root.overrideredirect(True)
        self.root.attributes("-topmost", True)
        self.root.attributes("-alpha", self.WINDOW_ALPHA)
        self.root.config(bg='black')
        self.root.geometry(f"{self.WINDOW_WIDTH}x{self.WINDOW_HEIGHT}")

    def _setup_widgets(self) -> None:
        self.text_area = tk.Text(
            self.root,
            wrap=tk.WORD, bg='black', fg='white',
            font=("Consolas", 10), bd=0, highlightthickness=0,
            selectbackground='black', selectforeground='white',
            inactiveselectbackground='black', exportselection=False,
        )
        self.text_area.pack(expand=True, fill='both', padx=5, pady=5)
        self.text_area.config(state=tk.DISABLED)

        self.grip = tk.Frame(self.root, bg='gray20',
                             cursor="bottom_right_corner")
        self.grip.place(relx=1.0, rely=1.0, anchor="se",
                        width=self.GRIP_SIZE, height=self.GRIP_SIZE)
        self.grip.bind("<B1-Motion>", self._on_resize)

        self.text_area.bind("<Button-1>", self._on_start_drag)
        self.text_area.bind("<B1-Motion>", self._on_do_drag)
        self.text_area.bind("<MouseWheel>", self._on_mousewheel)
        self.grip.bind("<MouseWheel>", self._on_mousewheel)

    def activate_stealth(self) -> None:
        if not self._is_windows():
            logger.info("Скрытность недоступна: не Windows")
            return
        try:
            self.root.update_idletasks()
            hwnd = self.root.winfo_id()
            while ctypes.windll.user32.GetParent(hwnd):
                hwnd = ctypes.windll.user32.GetParent(hwnd)
            ctypes.windll.user32.SetWindowDisplayAffinity(
                hwnd, self.WDA_EXCLUDEFROMCAPTURE
            )
            logger.info("Режим скрытности активирован")
        except Exception as e:
            logger.warning(f"Не удалось активировать скрытность: {e}")

    @staticmethod
    def _is_windows() -> bool:
        return hasattr(ctypes, 'windll')

    def _get_hwnd(self) -> Optional[int]:
        """Получить верхнеуровневый HWND окна (Windows)."""
        if not self._is_windows():
            return None
        try:
            self.root.update_idletasks()
            hwnd = self.root.winfo_id()
            while ctypes.windll.user32.GetParent(hwnd):
                hwnd = ctypes.windll.user32.GetParent(hwnd)
            return hwnd
        except Exception:
            return None

    # ============================================================
    #  Публичный API
    # ============================================================

    def log(self, message: str) -> None:
        with self.ui_lock:
            try:
                self.text_area.config(state=tk.NORMAL)
                self.text_area.delete("1.0", tk.END)
                if message:
                    self.text_area.insert("1.0", message)
                self.text_area.config(state=tk.DISABLED)
            except tk.TclError:
                pass
            except Exception as e:
                logger.error(f"Ошибка логирования: {e}")

    def set_state(self, state: WindowState) -> None:
        self.state = state

    def toggle_follow(self) -> bool:
        self.is_following = not self.is_following
        status = "включен" if self.is_following else "отключен"
        logger.info(f"Отслеживание курсора {status}")
        if self._on_follow_toggle:
            try:
                self._on_follow_toggle(self.is_following)
            except Exception as e:
                logger.error(f"Ошибка в callback: {e}")
        return self.is_following

    def set_position(self, x: int, y: int) -> None:
        if not self.is_following or self._is_invisible:
            return
        try:
            self.root.geometry(f"+{x}+{y}")
        except tk.TclError:
            pass

    def apply_size(self, width: int, height: int) -> None:
        try:
            self.root.geometry(f"{width}x{height}")
        except tk.TclError:
            pass

    def get_geometry(self) -> tuple:
        try:
            return (
                self.root.winfo_x(), self.root.winfo_y(),
                self.root.winfo_width(), self.root.winfo_height(),
            )
        except tk.TclError:
            return 0, 0, self.WINDOW_WIDTH, self.WINDOW_HEIGHT

    def schedule(self, callback: Callable[[], None]) -> None:
        try:
            self.root.after(self.POSITION_UPDATE_MS, callback)
        except tk.TclError:
            pass

    def mainloop(self) -> None:
        try:
            self.root.mainloop()
        except KeyboardInterrupt:
            pass

    def destroy(self) -> None:
        try:
            self.root.quit()
            self.root.destroy()
        except tk.TclError:
            pass

    def set_on_follow_toggle(self, callback: Callable[[bool], None]) -> None:
        self._on_follow_toggle = callback

    # ============================================================
    #  Прозрачность
    # ============================================================

    def set_alpha(self, alpha: float) -> None:
        alpha = max(self._alpha_min, min(alpha, self._alpha_max))
        try:
            self.root.attributes("-alpha", alpha)
            self.WINDOW_ALPHA = alpha
        except tk.TclError:
            pass

    def get_alpha(self) -> float:
        return self.WINDOW_ALPHA

    def adjust_alpha(self, delta: float) -> None:
        self.set_alpha(self.WINDOW_ALPHA + delta)

    # ============================================================
    #  Click-through (Windows only)
    # ============================================================

    def set_clickthrough(self, enabled: bool) -> bool:
        """
        Включить/выключить пропуск кликов.
        Возвращает фактическое состояние.
        """
        if not self._is_windows():
            logger.warning("click-through недоступен на этой ОС")
            return False

        hwnd = self._get_hwnd()
        if hwnd is None:
            return False

        try:
            current = ctypes.windll.user32.GetWindowLongW(
                hwnd, self.GWL_EXSTYLE
            )
            if enabled:
                new = current | self.WS_EX_LAYERED | self.WS_EX_TRANSPARENT
            else:
                new = current & ~self.WS_EX_TRANSPARENT

            ctypes.windll.user32.SetWindowLongW(hwnd, self.GWL_EXSTYLE, new)

            self._is_clickthrough = enabled
            logger.info("Click-through: %s", "ON" if enabled else "OFF")
            return True
        except Exception as e:
            logger.error("set_clickthrough error: %s", e)
            return False

    def toggle_clickthrough(self) -> bool:
        return self.set_clickthrough(not self._is_clickthrough)

    def is_clickthrough(self) -> bool:
        return self._is_clickthrough

    # ============================================================
    #  Невидимость (полное скрытие + пропуск кликов)
    # ============================================================

    def set_invisible(self, invisible: bool) -> None:
        """
        Невидимка: окно полностью скрывается.
        При повторном показе — прозрачность возвращается.
        """
        self._is_invisible = invisible

        if invisible:
            # Запоминаем текущую прозрачность, делаем прозрачным на 0
            self._saved_alpha = self.WINDOW_ALPHA
            try:
                self.root.attributes("-alpha", 0.0)
                self.root.withdraw()        # полностью скрыть из таскбара
            except tk.TclError:
                pass
            logger.info("Окно скрыто (невидимка)")
        else:
            try:
                self.root.deiconify()
                # Восстанавливаем прозрачность
                self.root.attributes("-alpha", getattr(self, "_saved_alpha", 0.5))
            except tk.TclError:
                pass
            logger.info("Окно восстановлено")

    def toggle_invisible(self) -> bool:
        self.set_invisible(not self._is_invisible)
        return self._is_invisible

    def is_invisible(self) -> bool:
        return self._is_invisible

    # ============================================================
    #  Внутренние обработчики
    # ============================================================

    def _on_start_drag(self, event: tk.Event) -> None:
        self._drag_start_x = event.x
        self._drag_start_y = event.y

    def _on_do_drag(self, event: tk.Event) -> None:
        if self.is_following or self._is_clickthrough:
            return
        dx = event.x - self._drag_start_x
        dy = event.y - self._drag_start_y
        self.root.geometry(
            f"+{self.root.winfo_x() + dx}+{self.root.winfo_y() + dy}"
        )

    def _on_resize(self, event: tk.Event) -> None:
        if self.is_following:
            return
        new_width = self.root.winfo_pointerx() - self.root.winfo_rootx()
        new_height = self.root.winfo_pointery() - self.root.winfo_rooty()
        if (new_width >= self.MIN_WINDOW_WIDTH
                and new_height >= self.MIN_WINDOW_HEIGHT):
            self.root.geometry(f"{new_width}x{new_height}")

    def _on_mousewheel(self, event: tk.Event) -> None:
        try:
            cur_w = self.root.winfo_width()
            cur_h = self.root.winfo_height()
            step = 25
            if event.delta > 0:
                self.apply_size(cur_w + step, cur_h + step)
            elif event.delta < 0:
                self.apply_size(
                    max(self.MIN_WINDOW_WIDTH, cur_w - step),
                    max(self.MIN_WINDOW_HEIGHT, cur_h - step),
                )
        except tk.TclError:
            pass
        return "break"
