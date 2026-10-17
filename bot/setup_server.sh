#!/bin/bash
# ======================================================================
#  setup_server.sh — Автоматическая установка и запуск бота на Linux VPS
#  (Ubuntu 20.04 / 22.04 / 24.04, Debian)
# ======================================================================

set -e

echo "=== [1/4] Обновление пакетов и установка Python ==="
apt update -y
apt install -y python3 python3-pip python3-venv git curl

BOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$BOT_DIR"

echo "=== [2/4] Проверка .env файла ==="
if [ ! -f ".env" ]; then
    echo "⚠️ Файл .env не найден! Создаю шаблон..."
    cat <<EOF > .env
BOT_TOKEN=ваш_токен_от_BotFather
ADMIN_ID=ваш_telegram_id
BOT_USERNAME=gghostaibot
SUPPORT_USERNAME=superuser222
CRYPTO_PAY_TOKEN=
EOF
    echo "❗ Отредактируйте .env файл: nano .env и вставьте ваш BOT_TOKEN и ADMIN_ID"
fi

echo "=== [3/4] Установка зависимостей Python ==="
pip3 install -r requirements.txt --break-system-packages 2>/dev/null || pip3 install -r requirements.txt

echo "=== [4/4] Создание и запуск системной службы systemd (24/7) ==="
SERVICE_FILE="/etc/systemd/system/ghost_bot.service"

cat <<EOF > "$SERVICE_FILE"
[Unit]
Description=Ghost AI Telegram Bot
After=network.target

[Service]
Type=simple
User=root
WorkingDirectory=$BOT_DIR
ExecStart=$(which python3) bot.py
Restart=always
RestartSec=5
StandardOutput=journal
StandardError=journal

[Install]
WantedBy=multi-user.target
EOF

systemctl daemon-reload
systemctl enable ghost_bot.service
systemctl restart ghost_bot.service

echo ""
echo "======================================================================"
echo "  ✅ БОТ УСПЕШНО УСТАНОВЛЕН И ЗАПУЩЕН 24/7 В ФОНЕ!"
echo "======================================================================"
echo "Команды для управления:"
echo "• Проверить статус:   systemctl status ghost_bot"
echo "• Посмотреть логи:    journalctl -u ghost_bot -f"
echo "• Перезапустить:      systemctl restart ghost_bot"
echo "• Остановить:         systemctl stop ghost_bot"
echo "======================================================================"
