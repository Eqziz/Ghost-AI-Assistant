"""
crypto.py — шифрование конфига через Windows DPAPI.
"""
import base64
import json
import logging
import platform
import hashlib
from typing import Optional

logger = logging.getLogger(__name__)

IS_WINDOWS = platform.system() == "Windows"

if IS_WINDOWS:
    try:
        import ctypes
        from ctypes import wintypes

        class _DATA_BLOB(ctypes.Structure):
            _fields_ = [
                ("cbData", wintypes.DWORD),
                ("pbData", ctypes.POINTER(ctypes.c_byte)),
            ]

        _CryptProtectData = ctypes.windll.crypt32.CryptProtectData
        _CryptProtectData.argtypes = [
            ctypes.POINTER(_DATA_BLOB), ctypes.c_wchar_p,
            ctypes.POINTER(_DATA_BLOB), ctypes.c_void_p,
            ctypes.c_void_p, wintypes.DWORD, ctypes.POINTER(_DATA_BLOB),
        ]
        _CryptProtectData.restype = wintypes.BOOL

        _CryptUnprotectData = ctypes.windll.crypt32.CryptUnprotectData
        _CryptUnprotectData.argtypes = [
            ctypes.POINTER(_DATA_BLOB), ctypes.c_wchar_p,
            ctypes.POINTER(_DATA_BLOB), ctypes.c_void_p,
            ctypes.c_void_p, wintypes.DWORD, ctypes.POINTER(_DATA_BLOB),
        ]
        _CryptUnprotectData.restype = wintypes.BOOL

        DPAPI_AVAILABLE = True
    except Exception as e:
        logger.warning("DPAPI недоступен: %s", e)
        DPAPI_AVAILABLE = False
else:
    DPAPI_AVAILABLE = False


def _dpapi_encrypt(data: bytes) -> Optional[bytes]:
    if not (IS_WINDOWS and DPAPI_AVAILABLE):
        return None
    try:
        inp = _DATA_BLOB(len(data), ctypes.cast(
            ctypes.c_char_p(data), ctypes.POINTER(ctypes.c_byte)))
        out = _DATA_BLOB()
        ok = _CryptProtectData(
            ctypes.byref(inp), None, None, None, None,
            0x01, ctypes.byref(out))
        if not ok:
            return None
        buf = ctypes.string_at(out.pbData, out.cbData)
        ctypes.windll.kernel32.LocalFree(out.pbData)
        return buf
    except Exception as e:
        logger.error("DPAPI encrypt error: %s", e)
        return None


def _dpapi_decrypt(data: bytes) -> Optional[bytes]:
    if not (IS_WINDOWS and DPAPI_AVAILABLE):
        return None
    try:
        inp = _DATA_BLOB(len(data), ctypes.cast(
            ctypes.c_char_p(data), ctypes.POINTER(ctypes.c_byte)))
        out = _DATA_BLOB()
        ok = _CryptUnprotectData(
            ctypes.byref(inp), None, None, None, None,
            0x01, ctypes.byref(out))
        if not ok:
            return None
        buf = ctypes.string_at(out.pbData, out.cbData)
        ctypes.windll.kernel32.LocalFree(out.pbData)
        return buf
    except Exception as e:
        logger.error("DPAPI decrypt error: %s", e)
        return None


def _machine_key() -> bytes:
    parts = [platform.node(), platform.machine(), platform.processor()]
    return hashlib.sha256("|".join(parts).encode("utf-8")).digest()


def _xor(data: bytes) -> bytes:
    key = _machine_key()
    return bytes(b ^ key[i % len(key)] for i, b in enumerate(data))


def encrypt_dict(data: dict) -> str:
    raw = json.dumps(data, ensure_ascii=False).encode("utf-8")
    if IS_WINDOWS and DPAPI_AVAILABLE:
        enc = _dpapi_encrypt(raw)
        if enc is not None:
            return "v1:dpapi:" + base64.b64encode(enc).decode("ascii")
    enc = _xor(raw)
    return "v1:xor:" + base64.b64encode(enc).decode("ascii")


def decrypt_dict(token: str) -> Optional[dict]:
    if not token or token.count(":") < 2:
        return None
    try:
        _, method, payload = token.split(":", 2)
        raw = base64.b64decode(payload)
    except Exception:
        return None
    if method == "dpapi":
        dec = _dpapi_decrypt(raw)
    elif method == "xor":
        dec = _xor(raw)
    else:
        return None
    if dec is None:
        return None
    try:
        return json.loads(dec.decode("utf-8"))
    except Exception:
        return None
