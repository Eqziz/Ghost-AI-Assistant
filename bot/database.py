"""
database.py — SQLite хранилище для пользователей, заказов и лицензий Ghost AI.
"""
import sqlite3
import time
from typing import Optional, List, Dict, Any
from pathlib import Path

DB_PATH = Path(__file__).parent / "ghost_ai.db"


class GhostDatabase:
    def __init__(self, db_path: Path = DB_PATH):
        self.db_path = str(db_path)
        self.init_db()

    def _get_connection(self) -> sqlite3.Connection:
        conn = sqlite3.connect(self.db_path, timeout=10.0)
        conn.row_factory = sqlite3.Row
        return conn

    def init_db(self):
        """Создает таблицы базы данных, если они еще не существуют."""
        with self._get_connection() as conn:
            cursor = conn.cursor()
            
            # Таблица пользователей
            cursor.execute("""
                CREATE TABLE IF NOT EXISTS users (
                    user_id INTEGER PRIMARY KEY,
                    username TEXT,
                    first_name TEXT,
                    created_at INTEGER,
                    is_banned INTEGER DEFAULT 0
                )
            """)

            # Таблица лицензий
            cursor.execute("""
                CREATE TABLE IF NOT EXISTS licenses (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    key TEXT UNIQUE NOT NULL,
                    user_id INTEGER NOT NULL,
                    duration_days INTEGER NOT NULL,
                    expires_at INTEGER NOT NULL,
                    is_active INTEGER DEFAULT 1,
                    hwid_hash TEXT DEFAULT NULL,
                    created_at INTEGER NOT NULL,
                    FOREIGN KEY (user_id) REFERENCES users (user_id)
                )
            """)

            # Таблица заказов
            cursor.execute("""
                CREATE TABLE IF NOT EXISTS orders (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    user_id INTEGER NOT NULL,
                    payment_method TEXT NOT NULL,
                    invoice_id TEXT UNIQUE NOT NULL,
                    tariff_days INTEGER NOT NULL,
                    amount REAL NOT NULL,
                    currency TEXT NOT NULL,
                    status TEXT DEFAULT 'pending',
                    created_at INTEGER NOT NULL,
                    FOREIGN KEY (user_id) REFERENCES users (user_id)
                )
            """)
            conn.commit()

    def add_or_update_user(self, user_id: int, username: Optional[str], first_name: Optional[str]):
        now = int(time.time())
        with self._get_connection() as conn:
            cursor = conn.cursor()
            cursor.execute("""
                INSERT INTO users (user_id, username, first_name, created_at)
                VALUES (?, ?, ?, ?)
                ON CONFLICT(user_id) DO UPDATE SET
                    username = excluded.username,
                    first_name = excluded.first_name
            """, (user_id, username, first_name, now))
            conn.commit()

    def is_user_banned(self, user_id: int) -> bool:
        with self._get_connection() as conn:
            cursor = conn.cursor()
            cursor.execute("SELECT is_banned FROM users WHERE user_id = ?", (user_id,))
            row = cursor.fetchone()
            return bool(row["is_banned"]) if row else False

    def create_order(self, user_id: int, payment_method: str, invoice_id: str,
                     tariff_days: int, amount: float, currency: str) -> int:
        now = int(time.time())
        with self._get_connection() as conn:
            cursor = conn.cursor()
            cursor.execute("""
                INSERT INTO orders (user_id, payment_method, invoice_id, tariff_days, amount, currency, status, created_at)
                VALUES (?, ?, ?, ?, ?, ?, 'pending', ?)
            """, (user_id, payment_method, str(invoice_id), tariff_days, amount, currency, now))
            conn.commit()
            return cursor.lastrowid

    def get_order(self, invoice_id: str) -> Optional[Dict[str, Any]]:
        with self._get_connection() as conn:
            cursor = conn.cursor()
            cursor.execute("SELECT * FROM orders WHERE invoice_id = ?", (str(invoice_id),))
            row = cursor.fetchone()
            return dict(row) if row else None

    def mark_order_paid(self, invoice_id: str) -> bool:
        with self._get_connection() as conn:
            cursor = conn.cursor()
            cursor.execute("""
                UPDATE orders SET status = 'paid' WHERE invoice_id = ? AND status != 'paid'
            """, (str(invoice_id),))
            conn.commit()
            return cursor.rowcount > 0

    def add_license(self, key: str, user_id: int, duration_days: int,
                    expires_at: int, hwid_hash: Optional[str] = None):
        now = int(time.time())
        with self._get_connection() as conn:
            cursor = conn.cursor()
            cursor.execute("""
                INSERT INTO licenses (key, user_id, duration_days, expires_at, is_active, hwid_hash, created_at)
                VALUES (?, ?, ?, ?, 1, ?, ?)
            """, (key, user_id, duration_days, expires_at, hwid_hash, now))
            conn.commit()

    def get_active_license(self, user_id: int) -> Optional[Dict[str, Any]]:
        now = int(time.time())
        with self._get_connection() as conn:
            cursor = conn.cursor()
            cursor.execute("""
                SELECT * FROM licenses 
                WHERE user_id = ? AND is_active = 1 AND expires_at > ?
                ORDER BY expires_at DESC LIMIT 1
            """, (user_id, now))
            row = cursor.fetchone()
            return dict(row) if row else None

    def get_user_licenses(self, user_id: int) -> List[Dict[str, Any]]:
        with self._get_connection() as conn:
            cursor = conn.cursor()
            cursor.execute("""
                SELECT * FROM licenses 
                WHERE user_id = ?
                ORDER BY created_at DESC
            """, (user_id,))
            return [dict(r) for r in cursor.fetchall()]

    def bind_hwid(self, key: str, hwid_hash: str) -> bool:
        clean_hwid = str(hwid_hash).strip().lower()[:16]
        with self._get_connection() as conn:
            cursor = conn.cursor()
            cursor.execute("""
                UPDATE licenses SET hwid_hash = ? WHERE key = ?
            """, (clean_hwid, key))
            conn.commit()
            return cursor.rowcount > 0

    def reset_user_hwid(self, user_id: int) -> bool:
        with self._get_connection() as conn:
            cursor = conn.cursor()
            cursor.execute("""
                UPDATE licenses SET hwid_hash = NULL WHERE user_id = ? AND is_active = 1
            """, (user_id,))
            conn.commit()
            return cursor.rowcount > 0

    def revoke_license(self, key: str) -> bool:
        with self._get_connection() as conn:
            cursor = conn.cursor()
            cursor.execute("UPDATE licenses SET is_active = 0 WHERE key = ?", (key,))
            conn.commit()
            return cursor.rowcount > 0

    def get_stats(self) -> Dict[str, Any]:
        now = int(time.time())
        with self._get_connection() as conn:
            cursor = conn.cursor()
            
            cursor.execute("SELECT COUNT(*) AS total FROM users")
            total_users = cursor.fetchone()["total"]

            cursor.execute("SELECT COUNT(*) AS total FROM licenses WHERE is_active = 1 AND expires_at > ?", (now,))
            active_licenses = cursor.fetchone()["total"]

            cursor.execute("SELECT COUNT(*) AS total FROM orders WHERE status = 'paid'")
            paid_orders = cursor.fetchone()["total"]

            cursor.execute("SELECT SUM(amount) AS stars_revenue FROM orders WHERE status = 'paid' AND currency = 'XTR'")
            stars_rev = cursor.fetchone()["stars_revenue"] or 0

            cursor.execute("SELECT SUM(amount) AS crypto_revenue FROM orders WHERE status = 'paid' AND currency = 'USDT'")
            crypto_rev = cursor.fetchone()["crypto_revenue"] or 0

            return {
                "total_users": total_users,
                "active_licenses": active_licenses,
                "paid_orders": paid_orders,
                "stars_revenue": int(stars_rev),
                "crypto_revenue": round(float(crypto_rev), 2),
            }

    def get_all_user_ids(self) -> List[int]:
        with self._get_connection() as conn:
            cursor = conn.cursor()
            cursor.execute("SELECT user_id FROM users WHERE is_banned = 0")
            return [row["user_id"] for row in cursor.fetchall()]
