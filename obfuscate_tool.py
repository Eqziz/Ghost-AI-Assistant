"""
obfuscate_tool.py — утилита для обфускации LICENSE_SECRET.
"""
import secrets
import hashlib
import os
import sys


def get_machine_specific_key() -> bytes:
    marker = os.environ.get(
        "GHOST_BUILD_MARKER",
        "ghost_ai_default_build_marker_2024"
    ).encode("utf-8")
    return hashlib.sha256(
        hashlib.sha256(marker).digest()
    ).digest()


def xor_obfuscate(secret: bytes, key: bytes) -> str:
    result = bytes(s ^ key[i % len(key)] for i, s in enumerate(secret))
    return result.hex()


def generate_obfuscated_block(secret: bytes, var_name: str) -> str:
    key = get_machine_specific_key()
    key_hash = hashlib.sha256(key).hexdigest()[:16]
    obf_hex = xor_obfuscate(secret, key)

    decoy1 = secrets.token_bytes(48).hex()
    decoy2 = secrets.token_bytes(32).hex()
    marker = os.environ.get('GHOST_BUILD_MARKER', 'ghost_ai_default_build_marker_2024')

    code = f'''
# ============================================================
#  ОБФУСЦИРОВАННЫЙ LICENSE_SECRET
#  Не редактировать вручную! Сгенерировано obfuscate_tool.py
# ============================================================
import secrets as _secrets_lib

_KEY_HASH = "{key_hash}"
_MARKER = "{marker}"

# Фейковые переменные (мусор для анализаторов)
_DECOY_A = "{decoy1}"
_DECOY_B = "{decoy2}"

def _unobf_v1() -> bytes:
    """Восстанавливает ключ раскрытия через двойной SHA256 маркера."""
    _h = hashlib.sha256(_MARKER.encode("utf-8")).digest()
    return hashlib.sha256(_h).digest()

def _unobf_v2(seed: bytes) -> bytes:
    """Применяет ключ к обфусцированным данным."""
    _raw = bytes.fromhex({var_name})
    return bytes(b ^ seed[i % len(seed)] for i, b in enumerate(_raw))

def _check_marker(key: bytes) -> bool:
    return hashlib.sha256(key).hexdigest()[:16] == _KEY_HASH

{var_name} = "{obf_hex}"

def _get_license_secret() -> bytes:
    """Получить оригинальный LICENSE_SECRET (32 байта)."""
    _k = _unobf_v1()
    if not _check_marker(_k):
        return _secrets_lib.token_bytes(32)
    return _unobf_v2(_k)
'''
    return code


def main():
    print("=" * 60)
    print("  Утилита обфускации LICENSE_SECRET")
    print("=" * 60)
    print()

    if len(sys.argv) > 1:
        secret_hex = sys.argv[1]
        try:
            secret = bytes.fromhex(secret_hex)
        except ValueError:
            print(f"Ошибка: '{secret_hex}' — не hex-строка")
            sys.exit(1)
    else:
        secret = secrets.token_bytes(32)
        print(f"🔑 Сгенерирован новый секрет (32 байта):")
        print(f"   {secret.hex()}")
        print()

    if len(secret) != 32:
        print(f"Ошибка: секрет должен быть 32 байта, а не {len(secret)}")
        sys.exit(1)

    print("=" * 60)
    print("Вставьте этот код в license_manager.py:")
    print("=" * 60)
    print(generate_obfuscated_block(secret, "_APP_OBF_HEX"))

    print("\n" + "=" * 60)
    print("Вставьте этот код в bot/license_manager_bot.py:")
    print("=" * 60)
    print(generate_obfuscated_block(secret, "_BOT_OBF_HEX"))

    print("\n" + "=" * 60)
    print(f"Чистый секрет (для тестов): {secret.hex()}")
    print("=" * 60)


if __name__ == "__main__":
    main()
