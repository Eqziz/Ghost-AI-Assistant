"""Тест обфускации."""
import hashlib
import secrets as _secrets_lib

_KEY_HASH = "f3d77924997275d9"
_MARKER = "ghost_ai_default_build_marker_2024"
_APP_OBF_HEX = "bf33b4113b6110ccc4b2d3c01eb6460096376972809bcc980d354c473dc5f54c"


def _unobf_v1():
    _h = hashlib.sha256(_MARKER.encode("utf-8")).digest()
    return hashlib.sha256(_h).digest()


def _unobf_v2(seed):
    _raw = bytes.fromhex(_APP_OBF_HEX)
    return bytes(b ^ seed[i % len(seed)] for i, b in enumerate(_raw))


def _check_marker(key):
    return hashlib.sha256(key).hexdigest()[:16] == _KEY_HASH


def _get_license_secret():
    _k = _unobf_v1()
    if not _check_marker(_k):
        return _secrets_lib.token_bytes(32)
    return _unobf_v2(_k)


secret = _get_license_secret()
print(f"Расшифрованный секрет ({len(secret)} байт):")
print(secret.hex())
print()

expected = "21cb68e8e505c3c4b6a2fdaa80347dd057239a8c59c2e2d1d3a9eefe73d3ca2e"
if secret.hex() == expected:
    print("✅ СОВПАДАЕТ — обфускация работает корректно")
else:
    print("❌ НЕ СОВПАДАЕТ")
    print(f"Ожидалось: {expected}")
