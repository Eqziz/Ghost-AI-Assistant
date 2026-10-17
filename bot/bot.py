"""
bot.py — Telegram бот для продажи и управления лицензиями Ghost AI.
Поддерживает:
- Нативную оплату через Telegram Stars (XTR)
- Оплату криптовалютой (USDT / TON) через @CryptoBot
- SQLite базу данных пользователей, лицензий и заказов
- Защиту лицензий с опциональной привязкой к HWID
- Автоматическую сборку и отправку релизного ZIP-архива
- Личный кабинет пользователя и админ-панель
"""
import os
import time
import logging
from datetime import datetime
from typing import Optional

from telegram import (
    Update, LabeledPrice, InlineKeyboardButton,
    InlineKeyboardMarkup, InputFile
)
from telegram.ext import (
    Application, CommandHandler, CallbackQueryHandler,
    PreCheckoutQueryHandler, MessageHandler, filters, ContextTypes
)

# Загрузка переменных окружения
try:
    from dotenv import load_dotenv
    load_dotenv()
except ImportError:
    pass

from database import GhostDatabase
from license_manager_bot import generate_license, verify_license
from cryptobot import CryptoPayClient
from packager import create_release_package

logging.basicConfig(
    format="%(asctime)s - %(name)s - %(levelname)s - %(message)s",
    level=logging.INFO
)
logger = logging.getLogger("GhostBot")

# Конфигурация
BOT_TOKEN = os.getenv("BOT_TOKEN")
if not BOT_TOKEN:
    raise RuntimeError("BOT_TOKEN не установлен в файле .env!")

ADMIN_ID = int(os.getenv("ADMIN_ID", 0))
BOT_USERNAME = os.getenv("BOT_USERNAME", "gghostaibot")

db = GhostDatabase()
crypto_client = CryptoPayClient()

# Тарифная сетка
TARIFFS = {
    "1d": {
        "name": "⚡ 1 день (Тест)",
        "days": 1,
        "stars": 25,
        "usdt": 0.50,
        "desc": "Полный доступ на 24 часа. Идеально для проверки и тестов."
    },
    "7d": {
        "name": "🚀 7 дней (Неделя)",
        "days": 7,
        "stars": 70,
        "usdt": 1.50,
        "desc": "Недельный доступ со всеми функциями и обновлениями."
    },
    "30d": {
        "name": "👑 30 дней (Месяц)",
        "days": 30,
        "stars": 150,
        "usdt": 3.50,
        "desc": "Месячная подписка — выбор большинства пользователей."
    },
    "3650d": {
        "name": "♾️ Навсегда (Lifetime)",
        "days": 3650,
        "stars": 500,
        "usdt": 10.00,
        "desc": "Бессрочный доступ навсегда без ежемесячных платежей."
    }
}


def get_main_keyboard(is_admin: bool = False, has_active_license: bool = False) -> InlineKeyboardMarkup:
    buttons = [
        [InlineKeyboardButton("💳 Купить подписку", callback_data="menu_buy")],
        [InlineKeyboardButton("👤 Личный кабинет", callback_data="menu_profile")]
    ]
    if has_active_license or is_admin:
        buttons[1].append(InlineKeyboardButton("📥 Скачать софт", callback_data="menu_download"))
    buttons.append([InlineKeyboardButton("📖 Инструкция и помощь", callback_data="menu_help")])
    if is_admin:
        buttons.append([InlineKeyboardButton("⚙️ Админ-панель", callback_data="menu_admin")])
    return InlineKeyboardMarkup(buttons)


async def send_welcome(update: Update, context: ContextTypes.DEFAULT_TYPE):
    user = update.effective_user
    db.add_or_update_user(user.id, user.username, user.first_name)

    is_admin = (user.id == ADMIN_ID)
    has_active_lic = bool(db.get_active_license(user.id))
    text = (
        f"👋 Привет, {user.first_name}!\n\n"
        f"👻 **Ghost AI** — скрытный нативный ассистент с мгновенным анализом экрана "
        f"на базе передовых нейросетей и опциональным Ring 0 стелс-драйвером.\n\n"
        f"⚡ **Возможности:**\n"
        f"• Анализ любой выделенной области экрана по горячей клавише\n"
        f"• Поддержка Claude 3.5 Sonnet, GPT-4o и локальных моделей через Ollama\n"
        f"• Полная невидимость для оверлеев и средств защиты\n"
        f"• Мгновенный ответ прямо на экране поверх всех окон\n\n"
        f"Выберите действие ниже:"
    )
    markup = get_main_keyboard(is_admin, has_active_lic)
    if update.message:
        await update.message.reply_text(text, parse_mode="Markdown", reply_markup=markup)
    elif update.callback_query:
        await update.callback_query.edit_message_text(text, parse_mode="Markdown", reply_markup=markup)


async def show_buy_menu(update: Update, context: ContextTypes.DEFAULT_TYPE):
    query = update.callback_query
    if query:
        await query.answer()

    keyboard = []
    for code, t in TARIFFS.items():
        btn_text = f"{t['name']} — {t['stars']} ⭐ / ${t['usdt']:.2f}"
        keyboard.append([InlineKeyboardButton(btn_text, callback_data=f"buy_tariff_{code}")])

    keyboard.append([InlineKeyboardButton("◀️ Главное меню", callback_data="menu_main")])

    text = (
        "💳 **Выберите подходящий тариф Ghost AI:**\n\n"
        "После оплаты вы мгновенно получите:\n"
        "✅ Лицензионный ключ доступа\n"
        "✅ ZIP-архив с программой и Ring 0 драйвером\n"
        "✅ Полную пошаговую инструкцию по запуску"
    )

    if query:
        await query.edit_message_text(text, parse_mode="Markdown", reply_markup=InlineKeyboardMarkup(keyboard))
    else:
        await update.message.reply_text(text, parse_mode="Markdown", reply_markup=InlineKeyboardMarkup(keyboard))


async def select_payment_method(update: Update, context: ContextTypes.DEFAULT_TYPE):
    query = update.callback_query
    await query.answer()

    tariff_code = query.data.replace("buy_tariff_", "")
    tariff = TARIFFS.get(tariff_code)
    if not tariff:
        await query.edit_message_text("❌ Тариф не найден.", reply_markup=get_main_keyboard())
        return

    context.user_data["selected_tariff"] = tariff_code

    keyboard = [
        [InlineKeyboardButton(f"🌟 Оплатить Telegram Stars ({tariff['stars']} ⭐)", callback_data=f"pay_stars_{tariff_code}")],
    ]
    if crypto_client.is_configured:
        keyboard.append([InlineKeyboardButton(f"💎 Оплатить USDT ({tariff['usdt']:.2f} USDT)", callback_data=f"pay_crypto_{tariff_code}")])
    keyboard.append([InlineKeyboardButton("◀️ Назад к тарифам", callback_data="menu_buy")])

    text = (
        f"🛒 **Выбран тариф: {tariff['name']}**\n"
        f"📝 {tariff['desc']}\n\n"
        f"**Выберите удобный способ оплаты:**"
    )
    await query.edit_message_text(text, parse_mode="Markdown", reply_markup=InlineKeyboardMarkup(keyboard))


async def pay_with_stars(update: Update, context: ContextTypes.DEFAULT_TYPE):
    query = update.callback_query
    await query.answer()

    tariff_code = query.data.replace("pay_stars_", "")
    tariff = TARIFFS.get(tariff_code)
    if not tariff:
        return

    invoice_payload = f"stars_{query.from_user.id}_{tariff['days']}_{int(time.time())}"
    db.create_order(
        user_id=query.from_user.id,
        payment_method="stars",
        invoice_id=invoice_payload,
        tariff_days=tariff["days"],
        amount=tariff["stars"],
        currency="XTR"
    )

    await query.message.reply_invoice(
        title=f"Ghost AI — {tariff['name']}",
        description=f"Лицензионный доступ к Ghost AI на {tariff['days']} дн.",
        payload=invoice_payload,
        currency="XTR",
        prices=[LabeledPrice(f"Ghost AI ({tariff['name']})", tariff["stars"])],
    )


async def pay_with_crypto(update: Update, context: ContextTypes.DEFAULT_TYPE):
    query = update.callback_query
    await query.answer()

    tariff_code = query.data.replace("pay_crypto_", "")
    tariff = TARIFFS.get(tariff_code)
    if not tariff:
        return

    user_id = query.from_user.id
    payload = f"crypto_{user_id}_{tariff['days']}_{int(time.time())}"

    inv = await crypto_client.create_invoice(
        amount=tariff["usdt"],
        asset="USDT",
        description=f"Ghost AI — {tariff['name']}",
        payload=payload,
        bot_username=BOT_USERNAME
    )

    if not inv or not inv.get("pay_url"):
        await query.edit_message_text(
            "⚠️ Не удалось создать счет в CryptoBot. Пожалуйста, воспользуйтесь оплатой через Telegram Stars.",
            reply_markup=InlineKeyboardMarkup([[InlineKeyboardButton("◀️ Назад", callback_data=f"buy_tariff_{tariff_code}")]])
        )
        return

    invoice_id = inv["invoice_id"]
    db.create_order(
        user_id=user_id,
        payment_method="cryptobot",
        invoice_id=invoice_id,
        tariff_days=tariff["days"],
        amount=tariff["usdt"],
        currency="USDT"
    )

    keyboard = [
        [InlineKeyboardButton("💎 Оплатить в @CryptoBot", url=inv["pay_url"])],
        [InlineKeyboardButton("🔄 Проверить оплату", callback_data=f"check_crypto_{invoice_id}_{tariff['days']}")],
        [InlineKeyboardButton("◀️ Отмена", callback_data="menu_buy")]
    ]

    text = (
        f"💎 **Счет на оплату в CryptoBot:**\n\n"
        f"• Тариф: **{tariff['name']}**\n"
        f"• К оплате: **{tariff['usdt']:.2f} USDT**\n"
        f"• Время на оплату: 60 минут\n\n"
        f"Нажмите кнопку ниже для оплаты в боте, затем вернитесь сюда и нажмите **«Проверить оплату»**."
    )
    await query.edit_message_text(text, parse_mode="Markdown", reply_markup=InlineKeyboardMarkup(keyboard))


async def check_crypto_payment(update: Update, context: ContextTypes.DEFAULT_TYPE):
    query = update.callback_query
    await query.answer()

    parts = query.data.split("_")
    invoice_id = parts[2]
    tariff_days = int(parts[3])

    inv = await crypto_client.get_invoice(invoice_id)
    if not inv:
        await query.message.reply_text("❌ Ошибка соединения с CryptoBot. Попробуйте через пару секунд.")
        return

    if inv.get("status") == "paid":
        order = db.get_order(invoice_id)
        if order and order.get("status") == "paid":
            await query.message.reply_text("✅ Этот заказ уже был успешно активирован ранее!")
            return

        db.mark_order_paid(invoice_id)
        await issue_license_and_package(query.from_user.id, tariff_days, context, query.message)
    else:
        await query.message.reply_text("⏳ Оплата пока не подтверждена. Пожалуйста, завершите платеж в CryptoBot.")


async def pre_checkout_callback(update: Update, context: ContextTypes.DEFAULT_TYPE):
    query = update.pre_checkout_query
    await query.answer(ok=True)


async def successful_payment_callback(update: Update, context: ContextTypes.DEFAULT_TYPE):
    payment = update.message.successful_payment
    invoice_payload = payment.invoice_payload

    # Разбираем payload (stars_user_id_days_time)
    parts = invoice_payload.split("_")
    tariff_days = int(parts[2]) if len(parts) >= 3 else 30

    db.mark_order_paid(invoice_payload)
    await issue_license_and_package(update.effective_user.id, tariff_days, context, update.message)


async def issue_license_and_package(user_id: int, duration_days: int, context: ContextTypes.DEFAULT_TYPE, message_target=None):
    """
    Генерирует ключ, сохраняет в SQLite и отправляет ключ вместе с релизным ZIP-архивом.
    """
    expires_at = int(time.time()) + duration_days * 86400
    key = generate_license(user_id, duration_days)
    db.add_license(key, user_id, duration_days, expires_at)

    exp_date_str = datetime.fromtimestamp(expires_at).strftime("%d.%m.%Y %H:%M")

    success_text = (
        f"🎉 **Оплата успешно получена! Лицензия активирована!**\n\n"
        f"🔑 **Ваш лицензионный ключ:**\n"
        f"`{key}`\n\n"
        f"⏳ **Срок действия:** до {exp_date_str} ({duration_days} дн.)\n\n"
        f"📦 **Ниже отправлен архив с программой и драйвером.**"
    )

    # 1. Отправляем сообщение с ключом
    try:
        if message_target:
            await message_target.reply_text(success_text, parse_mode="Markdown")
        else:
            await context.bot.send_message(chat_id=user_id, text=success_text, parse_mode="Markdown")
    except Exception as e:
        logger.warning(f"Не удалось отправить reply_text, пробую send_message: {e}")
        await context.bot.send_message(chat_id=user_id, text=success_text, parse_mode="Markdown")

    # 2. Отправляем ZIP-архив с программой и драйвером
    try:
        zip_path = create_release_package()
        if zip_path.exists():
            with open(zip_path, "rb") as f:
                await context.bot.send_document(
                    chat_id=user_id,
                    document=f,
                    filename="GhostAI_Release.zip",
                    caption=(
                        "👻 **Ghost AI — Релизный пакет**\n\n"
                        "Распакуйте архив, запустите `GhostAI.exe` и вставьте ваш лицензионный ключ.\n"
                        "Инструкция по установке драйвера и настройке — в `README_INSTALL.txt`."
                    ),
                    parse_mode="Markdown",
                    read_timeout=60,
                    write_timeout=60
                )
    except Exception as e:
        logger.error(f"Не удалось отправить ZIP файл: {e}")
        await context.bot.send_message(
            chat_id=user_id,
            text="⚠️ Не удалось отправить архив напрямую. Вы можете скачать его в личном кабинете: /profile",
            parse_mode="Markdown"
        )


async def show_profile(update: Update, context: ContextTypes.DEFAULT_TYPE):
    query = update.callback_query
    if query:
        await query.answer()

    user_id = update.effective_user.id
    lic = db.get_active_license(user_id)

    if lic:
        exp_time = lic["expires_at"]
        days_left = max(0, int((exp_time - time.time()) // 86400))
        exp_date_str = datetime.fromtimestamp(exp_time).strftime("%d.%m.%Y %H:%M")
        hwid_str = lic.get("hwid_hash") or "Не привязан (работает на любом ПК)"

        text = (
            f"👤 **Личный кабинет**\n\n"
            f"• Статус подписки: 🟢 **Активна**\n"
            f"• Действует до: **{exp_date_str}** (осталось {days_left} дн.)\n"
            f"• Привязка HWID: `{hwid_str}`\n\n"
            f"🔑 **Ваш ключ:**\n`{lic['key']}`"
        )
        keyboard = [
            [InlineKeyboardButton("📥 Скачать Ghost AI (ZIP)", callback_data="menu_download")],
            [InlineKeyboardButton("🔄 Привязать / Сменить HWID", callback_data="bind_hwid_prompt")],
            [InlineKeyboardButton("💳 Продлить подписку", callback_data="menu_buy")],
            [InlineKeyboardButton("◀️ Главное меню", callback_data="menu_main")]
        ]
    else:
        text = (
            f"👤 **Личный кабинет**\n\n"
            f"• Статус подписки: 🔴 **Не активна**\n\n"
            f"У вас пока нет активной лицензии Ghost AI. Нажмите кнопку ниже, чтобы приобрести доступ."
        )
        keyboard = [
            [InlineKeyboardButton("💳 Купить подписку", callback_data="menu_buy")],
            [InlineKeyboardButton("◀️ Главное меню", callback_data="menu_main")]
        ]

    markup = InlineKeyboardMarkup(keyboard)
    if query:
        await query.edit_message_text(text, parse_mode="Markdown", reply_markup=markup)
    else:
        await update.message.reply_text(text, parse_mode="Markdown", reply_markup=markup)


async def send_download_package(update: Update, context: ContextTypes.DEFAULT_TYPE):
    query = update.callback_query
    if query:
        await query.answer()
        chat_id = query.from_user.id
    else:
        chat_id = update.effective_user.id

    # Проверяем наличие активной лицензии
    lic = db.get_active_license(chat_id)
    if not lic and chat_id != ADMIN_ID:
        text = (
            "🔒 **Скачивание доступно только после покупки лицензии!**\n\n"
            "Вы получите ссылку на скачивание актуального архива и лицензионный ключ "
            "сразу после выбора тарифа и оплаты.\n\n"
            "Нажмите кнопку ниже, чтобы приобрести подписку:"
        )
        keyboard = InlineKeyboardMarkup([
            [InlineKeyboardButton("💳 Купить подписку", callback_data="menu_buy")],
            [InlineKeyboardButton("◀️ Главное меню", callback_data="menu_main")]
        ])
        if query:
            await query.edit_message_text(text, parse_mode="Markdown", reply_markup=keyboard)
        else:
            await update.message.reply_text(text, parse_mode="Markdown", reply_markup=keyboard)
        return

    # Только если лицензия активна или это админ — отправляем архив!
    zip_path = create_release_package()
    if zip_path.exists():
        with open(zip_path, "rb") as f:
            await context.bot.send_document(
                chat_id=chat_id,
                document=f,
                filename="GhostAI_Release.zip",
                caption=(
                    "👻 **Ghost AI — Свежая сборка**\n\n"
                    "• Включает нативный `GhostAI.exe`\n"
                    "• Включает Ring 0 драйвер `ghost_driver.sys` и сертификат\n"
                    "• Скрипт авто-установки `install_driver.bat`\n\n"
                    "Активируйте ваш ключ в окне программы после запуска."
                ),
                parse_mode="Markdown",
                read_timeout=60,
                write_timeout=60
            )
    else:
        await context.bot.send_message(chat_id=chat_id, text="❌ Архив временно недоступен. Обратитесь в поддержку.")


async def show_help(update: Update, context: ContextTypes.DEFAULT_TYPE):
    query = update.callback_query
    if query:
        await query.answer()

    text = (
        "📖 **Инструкция и помощь по Ghost AI:**\n\n"
        "1. **Установка:**\n"
        "• Распакуйте скачанный ZIP-архив в любую папку.\n"
        "• Запустите `GhostAI.exe`.\n"
        "• Вставьте ваш лицензионный ключ из бота и нажмите «Активировать».\n\n"
        "2. **Выбор режима:**\n"
        "• **Light Mode:** Обычный запуск без прав ядра. Быстрый и удобный.\n"
        "• **Hard Mode (Ring 0):** Максимальная скрытность. Запустите `install_driver.bat` "
        "от Администратора один раз, чтобы установить стелс-драйвер ядра.\n\n"
        "3. **HWID Привязка:**\n"
        "• В окне активации есть кнопка «Копировать HWID». Если хотите жестко зафиксировать ключ "
        "за своим компьютером, отправьте этот код в личный кабинет бота.\n\n"
        "💬 Вопросы и техподдержка: @nodir_support"
    )
    keyboard = InlineKeyboardMarkup([[InlineKeyboardButton("◀️ Главное меню", callback_data="menu_main")]])
    if query:
        await query.edit_message_text(text, parse_mode="Markdown", reply_markup=keyboard)
    else:
        await update.message.reply_text(text, parse_mode="Markdown", reply_markup=keyboard)


async def prompt_bind_hwid(update: Update, context: ContextTypes.DEFAULT_TYPE):
    query = update.callback_query
    await query.answer()

    context.user_data["awaiting_hwid"] = True
    text = (
        "🔐 **Привязка лицензии к вашему ПК (HWID):**\n\n"
        "1. Откройте окно активации в `GhostAI.exe`.\n"
        "2. Нажмите кнопку **«📋 Копировать HWID»**.\n"
        "3. Отправьте скопированный 16-значный код ответным сообщением в этот чат.\n\n"
        "Для отмены отправьте `/cancel`."
    )
    await query.edit_message_text(text, parse_mode="Markdown", reply_markup=InlineKeyboardMarkup([
        [InlineKeyboardButton("◀️ Отмена", callback_data="menu_profile")]
    ]))


async def handle_user_text(update: Update, context: ContextTypes.DEFAULT_TYPE):
    user_id = update.effective_user.id
    text = update.message.text.strip()

    if context.user_data.get("awaiting_hwid"):
        context.user_data["awaiting_hwid"] = False
        lic = db.get_active_license(user_id)
        if not lic:
            await update.message.reply_text("❌ У вас нет активной лицензии для привязки.")
            return

        clean_hwid = text.lower()[:16]
        if len(clean_hwid) < 8:
            await update.message.reply_text("❌ Некорректный формат HWID. Попробуйте снова из личного кабинета.")
            return

        # Перегенерируем ключ с HWID
        remaining_days = max(1, int((lic["expires_at"] - time.time()) // 86400))
        new_key = generate_license(user_id, remaining_days, hwid=clean_hwid)

        db.revoke_license(lic["key"])
        db.add_license(new_key, user_id, remaining_days, lic["expires_at"], hwid_hash=clean_hwid)

        await update.message.reply_text(
            f"✅ **Лицензия успешно привязана к HWID `{clean_hwid}`!**\n\n"
            f"🔑 **Ваш новый ключ (работает только на вашем ПК):**\n"
            f"`{new_key}`\n\n"
            f"Скопируйте его и активируйте в программе.",
            parse_mode="Markdown",
            reply_markup=get_main_keyboard()
        )
        return

    # Обычный текст — показываем главное меню
    await send_welcome(update, context)


# ======================================================================
#                            АДМИН-ПАНЕЛЬ
# ======================================================================

async def show_admin_panel(update: Update, context: ContextTypes.DEFAULT_TYPE):
    user_id = update.effective_user.id
    if user_id != ADMIN_ID:
        if update.message:
            await update.message.reply_text("⛔ Доступ запрещен.")
        return

    stats = db.get_stats()
    text = (
        f"⚙️ **Панель администратора Ghost AI:**\n\n"
        f"👥 Всего пользователей: **{stats['total_users']}**\n"
        f"🟢 Активных лицензий: **{stats['active_licenses']}**\n"
        f"💰 Оплаченных заказов: **{stats['paid_orders']}**\n"
        f"⭐ Выручка Stars: **{stats['stars_revenue']} ⭐**\n"
        f"💎 Выручка Crypto: **${stats['crypto_revenue']:.2f} USDT**\n\n"
        f"**Команды админа:**\n"
        f"• `/gen <дней>` — сгенерировать промо-ключ\n"
        f"• `/broadcast <текст>` — рассылка всем пользователям"
    )
    keyboard = InlineKeyboardMarkup([[InlineKeyboardButton("◀️ Главное меню", callback_data="menu_main")]])
    if update.callback_query:
        await update.callback_query.edit_message_text(text, parse_mode="Markdown", reply_markup=keyboard)
    else:
        await update.message.reply_text(text, parse_mode="Markdown", reply_markup=keyboard)


async def admin_gen_key(update: Update, context: ContextTypes.DEFAULT_TYPE):
    if update.effective_user.id != ADMIN_ID:
        return

    args = context.args
    days = int(args[0]) if args and args[0].isdigit() else 30
    key = generate_license(update.effective_user.id, days)
    expires_at = int(time.time()) + days * 86400
    db.add_license(key, update.effective_user.id, days, expires_at)

    await update.message.reply_text(
        f"🎁 **Сгенерирован ключ на {days} дн.:**\n\n`{key}`",
        parse_mode="Markdown"
    )


async def admin_broadcast(update: Update, context: ContextTypes.DEFAULT_TYPE):
    if update.effective_user.id != ADMIN_ID:
        return

    text = " ".join(context.args)
    if not text:
        await update.message.reply_text("Использование: `/broadcast Текст сообщения`", parse_mode="Markdown")
        return

    user_ids = db.get_all_user_ids()
    sent = 0
    for uid in user_ids:
        try:
            await context.bot.send_message(uid, text, parse_mode="Markdown")
            sent += 1
        except Exception:
            pass

    await update.message.reply_text(f"📢 Рассылка завершена. Доставлено: {sent}/{len(user_ids)}")


# ======================================================================
#                              ЗАПУСК
# ======================================================================

def main():
    logger.info("Инициализация Ghost AI Telegram Bot...")
    create_release_package()  # Проверяем сборку релизного архива при старте

    app = Application.builder().token(BOT_TOKEN).build()

    # Команды
    app.add_handler(CommandHandler("start", send_welcome))
    app.add_handler(CommandHandler("buy", show_buy_menu))
    app.add_handler(CommandHandler("profile", show_profile))
    app.add_handler(CommandHandler("download", send_download_package))
    app.add_handler(CommandHandler("help", show_help))
    app.add_handler(CommandHandler("admin", show_admin_panel))
    app.add_handler(CommandHandler("gen", admin_gen_key))
    app.add_handler(CommandHandler("broadcast", admin_broadcast))

    # Колбэки меню
    app.add_handler(CallbackQueryHandler(show_buy_menu, pattern="^menu_buy$"))
    app.add_handler(CallbackQueryHandler(send_welcome, pattern="^menu_main$"))
    app.add_handler(CallbackQueryHandler(show_profile, pattern="^menu_profile$"))
    app.add_handler(CallbackQueryHandler(send_download_package, pattern="^menu_download$"))
    app.add_handler(CallbackQueryHandler(show_help, pattern="^menu_help$"))
    app.add_handler(CallbackQueryHandler(show_admin_panel, pattern="^menu_admin$"))
    app.add_handler(CallbackQueryHandler(prompt_bind_hwid, pattern="^bind_hwid_prompt$"))

    # Колбэки оплаты
    app.add_handler(CallbackQueryHandler(select_payment_method, pattern="^buy_tariff_"))
    app.add_handler(CallbackQueryHandler(pay_with_stars, pattern="^pay_stars_"))
    app.add_handler(CallbackQueryHandler(pay_with_crypto, pattern="^pay_crypto_"))
    app.add_handler(CallbackQueryHandler(check_crypto_payment, pattern="^check_crypto_"))

    # Обработчики платежей Telegram Stars
    app.add_handler(PreCheckoutQueryHandler(pre_checkout_callback))
    app.add_handler(MessageHandler(filters.SUCCESSFUL_PAYMENT, successful_payment_callback))

    # Обработчик текста (HWID ввод)
    app.add_handler(MessageHandler(filters.TEXT & ~filters.COMMAND, handle_user_text))

    logger.info("Ghost AI Telegram Bot успешно запущен!")
    app.run_polling()


if __name__ == "__main__":
    main()