import os
from telegram import Update, LabeledPrice
from telegram.ext import (
    Application, CommandHandler,
    PreCheckoutQueryHandler, MessageHandler,
    filters, ContextTypes
)
from license_manager_bot import generate_license

# Загружаем токен из .env файла
try:
    from dotenv import load_dotenv
    load_dotenv()
except ImportError:
    pass

BOT_TOKEN = os.getenv("BOT_TOKEN")
if not BOT_TOKEN:
    raise RuntimeError(
        "BOT_TOKEN не установлен!\n"
        "Создайте файл .env рядом с bot.py:\n"
        "  BOT_TOKEN=ваш_токен_от_botfather"
    )

PRICE_STARS = 100



async def start(update: Update, context: ContextTypes.DEFAULT_TYPE):
    await update.message.reply_text("👻 Ghost AI\n\n/buy — купить доступ на 30 дней")


async def buy(update: Update, context: ContextTypes.DEFAULT_TYPE):
    await update.message.reply_invoice(
        title="Ghost AI — 30 дней",
        description="Умный ассистент с анализом изображений",
        payload="ghost_ai_license",
        currency="XTR",          # XTR = Telegram Stars
        prices=[LabeledPrice("Ghost AI", PRICE_STARS)],
    )


async def pre_checkout(update: Update, context: ContextTypes.DEFAULT_TYPE):
    # Telegram требует ответить в течение 10 секунд
    await update.pre_checkout_query.answer(ok=True)


async def payment_received(update: Update, context: ContextTypes.DEFAULT_TYPE):
    user_id = update.effective_user.id
    key = generate_license(user_id)

    await update.message.reply_text(
        f"✅ Оплата получена!\n\n"
        f"🔑 Твой ключ:\n`{key}`\n\n"
        f"⏳ Действителен 30 дней.",
        parse_mode="Markdown"
    )


app = Application.builder().token(BOT_TOKEN).build()
app.add_handler(CommandHandler("start", start))
app.add_handler(CommandHandler("buy", buy))
app.add_handler(PreCheckoutQueryHandler(pre_checkout))
app.add_handler(MessageHandler(filters.SUCCESSFUL_PAYMENT, payment_received))
app.run_polling()