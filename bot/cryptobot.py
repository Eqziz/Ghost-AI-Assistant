"""
cryptobot.py — Async клиент для интеграции с официальным Telegram CryptoBot (Crypto Pay API).
"""
import os
import logging
from typing import Optional, Dict, Any
import aiohttp

logger = logging.getLogger(__name__)

CRYPTO_PAY_API_URL = "https://pay.crypt.bot/api"
TESTNET_API_URL = "https://testnet-pay.crypt.bot/api"


class CryptoPayClient:
    def __init__(self, token: Optional[str] = None, is_testnet: bool = False):
        self.token = token or os.getenv("CRYPTO_PAY_TOKEN")
        self.base_url = TESTNET_API_URL if is_testnet else CRYPTO_PAY_API_URL

    @property
    def is_configured(self) -> bool:
        return bool(self.token and len(self.token.strip()) > 5)

    async def create_invoice(self, amount: float, asset: str = "USDT",
                             description: str = "Ghost AI Subscription",
                             payload: str = "",
                             bot_username: str = "gghostaibot",
                             expires_in: int = 3600) -> Optional[Dict[str, Any]]:
        """
        Создает инвойс на оплату в CryptoBot.
        Возвращает словарь с invoice_id, bot_invoice_url, pay_url и статусом.
        """
        if not self.is_configured:
            logger.warning("CRYPTO_PAY_TOKEN не настроен. CryptoBot недоступен.")
            return None

        url = f"{self.base_url}/createInvoice"
        headers = {
            "Crypto-Pay-API-Token": self.token,
            "Content-Type": "application/json"
        }
        body = {
            "amount": f"{amount:.2f}",
            "asset": asset.upper(),
            "description": description,
            "payload": payload,
            "paid_btn_name": "openBot",
            "paid_btn_url": f"https://t.me/{bot_username.lstrip('@')}",
            "expires_in": expires_in
        }

        try:
            async with aiohttp.ClientSession() as session:
                async with session.post(url, json=body, headers=headers, timeout=15) as resp:
                    data = await resp.json()
                    if data.get("ok"):
                        result = data.get("result", {})
                        return {
                            "invoice_id": str(result.get("invoice_id")),
                            "bot_invoice_url": result.get("bot_invoice_url"),
                            "mini_app_invoice_url": result.get("mini_app_invoice_url"),
                            "pay_url": result.get("bot_invoice_url") or result.get("pay_url"),
                            "status": result.get("status"),
                            "amount": float(result.get("amount", amount)),
                            "asset": result.get("asset", asset),
                        }
                    else:
                        logger.error(f"CryptoPay API error: {data.get('error')}")
                        return None
        except Exception as e:
            logger.error(f"Ошибка вызова CryptoPay API: {e}")
            return None

    async def get_invoice(self, invoice_id: str) -> Optional[Dict[str, Any]]:
        """
        Проверяет статус инвойса по его ID.
        """
        if not self.is_configured:
            return None

        url = f"{self.base_url}/getInvoices"
        headers = {"Crypto-Pay-API-Token": self.token}
        params = {"invoice_ids": str(invoice_id)}

        try:
            async with aiohttp.ClientSession() as session:
                async with session.get(url, params=params, headers=headers, timeout=15) as resp:
                    data = await resp.json()
                    if data.get("ok"):
                        items = data.get("result", {}).get("items", [])
                        if items:
                            item = items[0]
                            return {
                                "invoice_id": str(item.get("invoice_id")),
                                "status": item.get("status"),  # 'active', 'paid', 'expired'
                                "amount": float(item.get("amount", 0)),
                                "asset": item.get("asset"),
                                "payload": item.get("payload"),
                                "paid_at": item.get("paid_at")
                            }
                    return None
        except Exception as e:
            logger.error(f"Ошибка проверки инвойса {invoice_id}: {e}")
            return None
