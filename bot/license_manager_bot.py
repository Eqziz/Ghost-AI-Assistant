"""
license_manager_bot.py — генератор ключей для бота.

Используется ТОЛЬКО на сервере бота, не в приложении.
"""
import base64
import hashlib
import hmac
import json
import time

# ============================================================
#  ОБФУСЦИРОВАННЫЙ LICENSE_SECRET (тот же, что в license_manager.py)
# ============================================================
import secrets as _secrets_lib

_KEY_HASH = "f3d77924997275d9"
_MARKER = "ghost_ai_default_build_marker_2024"

_DECOY_A = "12f1bfd969661ca2fa909722e3943ed5c1a299b9ce873c8274f525d6c6144477d3c862ea408d2d8004a3dc31df659732"
_DECOY_B = "11128d414affed423336bc70cd09e3b1d59e9f08525b77339dacc693e7355510"

def _unobf_v1() -> bytes:
    """Восстанавливает ключ раскрытия через двойной SHA256 маркера."""
    _h = hashlib.sha256(_MARKER.encode("utf-8")).digest()
    return hashlib.sha256(_h).digest()

def _unobf_v2(seed: bytes) -> bytes:
    """Применяет ключ к обфусцированным данным."""
    _raw = bytes.fromhex(_BOT_OBF_HEX)
    return bytes(b ^ seed[i % len(seed)] for i, b in enumerate(_raw))

def _check_marker(key: bytes) -> bool:
    return hashlib.sha256(key).hexdigest()[:16] == _KEY_HASH

_BOT_OBF_HEX = "bf33b4113b6110ccc4b2d3c01eb6460096376972809bcc980d354c473dc5f54c"

def _get_license_secret() -> bytes:
    """Получить оригинальный LICENSE_SECRET (32 байта)."""
    _k = _unobf_v1()
    if not _check_marker(_k):
        return _secrets_lib.token_bytes(32)
    return _unobf_v2(_k)

# ============================================================


def generate_license(user_id: int, duration_days: int = 30, hwid: str = None) -> str:
    """Генерирует ключ для пользователя с опциональной привязкой к HWID."""
    expires_at = int(time.time()) + duration_days * 86400
    data = {"uid": user_id, "exp": expires_at}
    if hwid:
        data["hwid"] = str(hwid).strip().lower()[:16]
    payload = json.dumps(data)
    payload_b64 = base64.urlsafe_b64encode(payload.encode()).decode().rstrip("=")
    secret = _get_license_secret()
    sig = hmac.new(secret, payload_b64.encode(), hashlib.sha256).hexdigest()
    return f"{payload_b64}.{sig}"


def verify_license(key: str, check_hwid: str = None) -> dict | None:
    """Проверить ключ (для отладки и валидации)."""
    try:
        payload_b64, sig = key.rsplit(".", 1)
        secret = _get_license_secret()
        expected = hmac.new(
            secret, payload_b64.encode(), hashlib.sha256
        ).hexdigest()

        if not hmac.compare_digest(sig, expected):
            return None

        padded = payload_b64 + "=" * ((4 - len(payload_b64) % 4) % 4)
        payload = json.loads(base64.urlsafe_b64decode(padded).decode())

        if time.time() > payload.get("exp", 0):
            return None

        if check_hwid and "hwid" in payload:
            if payload["hwid"] != str(check_hwid).strip().lower()[:16]:
                return None

        return payload
    except Exception:
        return None


if __name__ == "__main__":
    # Тест
    print("Тест генерации и проверки:")
    key = generate_license(123456789, 30)
    print(f"Ключ: {key}")
    result = verify_license(key)
    print(f"Проверка: {result}")
    print("✅ OK" if result and result["uid"] == 123456789 else "❌ FAIL")
