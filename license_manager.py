"""
license_manager.py — проверка лицензий в приложении.

Содержит обфусцированный секрет. Расшифровка происходит
только при правильном build marker (задаётся при сборке).
"""
import base64
import hashlib
import hmac
import json
import logging
import os
import platform
import subprocess
import time
import uuid
import winreg
from dataclasses import dataclass
from pathlib import Path
from typing import Optional

logger = logging.getLogger(__name__)

IS_WINDOWS = platform.system() == "Windows"

# ============================================================
#  ОБФУСЦИРОВАННЫЙ LICENSE_SECRET (НЕ РЕДАКТИРОВАТЬ!)
#  Замените этот блок на сгенерированный obfuscate_tool.py
# ============================================================
# ============================================================
#  ОБФУСЦИРОВАННЫЙ LICENSE_SECRET
#  Не редактировать вручную! Сгенерировано obfuscate_tool.py
# ============================================================
import secrets as _secrets_lib

_KEY_HASH = "f3d77924997275d9"
_MARKER = "ghost_ai_default_build_marker_2024"

# Фейковые переменные (мусор для анализаторов)
_DECOY_A = "eb032f7ec501b8d556d2e86265327ef061175bbe6369538acdaf4d01b3f25a1466eeb342508a1490d5ab501ae69b723a"
_DECOY_B = "9a2b8a5cede0a4c0514ba9ab11213cc817fcfe5ab31a29f731d836d691ae8a7b"

def _unobf_v1() -> bytes:
    """Восстанавливает ключ раскрытия через двойной SHA256 маркера."""
    _h = hashlib.sha256(_MARKER.encode("utf-8")).digest()
    return hashlib.sha256(_h).digest()

def _unobf_v2(seed: bytes) -> bytes:
    """Применяет ключ к обфусцированным данным."""
    _raw = bytes.fromhex(_APP_OBF_HEX)
    return bytes(b ^ seed[i % len(seed)] for i, b in enumerate(_raw))

def _check_marker(key: bytes) -> bool:
    return hashlib.sha256(key).hexdigest()[:16] == _KEY_HASH

_APP_OBF_HEX = "bf33b4113b6110ccc4b2d3c01eb6460096376972809bcc980d354c473dc5f54c"

def _get_license_secret() -> bytes:
    """Получить оригинальный LICENSE_SECRET (32 байта)."""
    _k = _unobf_v1()
    if not _check_marker(_k):
        return _secrets_lib.token_bytes(32)
    return _unobf_v2(_k)


# ============================================================


LICENSE_FILE = Path(os.path.expanduser("~")) / ".ghost_ai_license.json"


# ============================================================
#  HWID — привязка к устройству
# ============================================================

def get_hwid() -> str:
    """Уникальный и неизменяемый ID устройства через реестр Windows."""
    if IS_WINDOWS:
        try:
            with winreg.OpenKey(
                winreg.HKEY_LOCAL_MACHINE, 
                r"SOFTWARE\Microsoft\Cryptography", 
                0, 
                winreg.KEY_READ | winreg.KEY_WOW64_64KEY
            ) as key:
                guid, _ = winreg.QueryValueEx(key, "MachineGuid")
                if guid:
                    return str(guid).strip()
        except Exception:
            pass

    return f"{platform.node()}-{uuid.getnode():012x}"


def get_hwid_hash() -> str:
    return hashlib.sha256(get_hwid().encode("utf-8")).hexdigest()


# ============================================================
#  Лицензия
# ============================================================

@dataclass
class LicenseData:
    telegram_id: int
    expires_at: int
    hwid_hash: str = ""

    @property
    def is_expired(self) -> bool:
        return time.time() > self.expires_at

    @property
    def days_left(self) -> int:
        return max(0, int((self.expires_at - time.time()) // 86400))

    @property
    def expires_str(self) -> str:
        return time.strftime("%Y-%m-%d %H:%M", time.localtime(self.expires_at))


# ============================================================
#  Проверка ключа (формат как в bot/license.py)
# ============================================================

def verify_key(key: str) -> Optional[LicenseData]:
    """Проверить ключ формата payload_b64.hmac_hex."""
    if not key or "." not in key:
        return None

    try:
        payload_b64, sig = key.rsplit(".", 1)
    except ValueError:
        return None

    # Получаем секрет (расшифровываем на лету)
    secret = _get_license_secret()

    expected = hmac.new(
        secret, payload_b64.encode(), hashlib.sha256
    ).hexdigest()

    if not hmac.compare_digest(sig, expected):
        return None

    # Декодирование payload
    try:
        padded = payload_b64 + "=" * ((4 - len(payload_b64) % 4) % 4)
        payload = json.loads(base64.urlsafe_b64decode(padded).decode())
    except Exception:
        return None

    uid = payload.get("uid")
    exp = payload.get("exp")
    if not isinstance(uid, int) or not isinstance(exp, (int, float)):
        return None

    return LicenseData(
        telegram_id=uid,
        expires_at=int(exp),
        hwid_hash=get_hwid_hash(),
    )


# ============================================================
#  Сохранение лицензии
# ============================================================

def save_license(key: str) -> bool:
    """Сохранить ключ с привязкой к HWID."""
    data = verify_key(key)
    if data is None:
        return False

    payload = json.dumps({
        "uid": data.telegram_id,
        "exp": data.expires_at,
        "hwid": data.hwid_hash,
    }).encode("utf-8")

    hwid_key = bytes.fromhex(data.hwid_hash)
    encrypted = bytes(b ^ hwid_key[i % len(hwid_key)]
                      for i, b in enumerate(payload))

    try:
        LICENSE_FILE.write_bytes(encrypted)
        LICENSE_FILE.chmod(0o600)
        return True
    except OSError as e:
        logger.error("Не удалось сохранить лицензию: %s", e)
        return False


def load_license() -> Optional[LicenseData]:
    """Загрузить и проверить сохранённую лицензию."""
    if not LICENSE_FILE.exists():
        return None

    try:
        encrypted = LICENSE_FILE.read_bytes()
    except OSError as e:
        logger.error("Не удалось прочитать лицензию: %s", e)
        return None

    hwid_hash = get_hwid_hash()
    hwid_key = bytes.fromhex(hwid_hash)

    try:
        decrypted = bytes(b ^ hwid_key[i % len(hwid_key)]
                          for i, b in enumerate(encrypted))
        payload = json.loads(decrypted.decode("utf-8"))
    except Exception:
        return None

    data = LicenseData(
        telegram_id=payload.get("uid", 0),
        expires_at=payload.get("exp", 0),
        hwid_hash=hwid_hash,
    )

    if data.is_expired:
        return None

    return data


def clear_license() -> None:
    try:
        if LICENSE_FILE.exists():
            LICENSE_FILE.unlink()
    except OSError:
        pass


def format_license_info(data: LicenseData) -> str:
    return (
        f"👻 Лицензия активна\n"
        f"Telegram ID: {data.telegram_id}\n"
        f"Действует до: {data.expires_str}\n"
        f"Осталось дней: {data.days_left}"
    )
