/**
 * GhostAI Cloudflare Worker Relay
 * 
 * Назначение: Защищённый прокси-шлюз на порту 443 через глобальную сеть Cloudflare CDN.
 * Обходит сетевые фильтры прокторинга (OES, SEB, Honorlock), скрывая трафик к ИИ
 * под видом обычного веб-серфинга к CDN Cloudflare.
 * 
 * Развертывание:
 * 1. Зайдите на https://dash.cloudflare.com/ -> Workers & Pages -> Create Application -> Create Worker
 * 2. Вставьте этот код в редактор.
 * 3. (Опционально) Добавьте переменную окружения OPENROUTER_API_KEY в Settings -> Variables.
 * 4. Нажмите Save and Deploy. Вы получите адрес вида: https://ghost-relay.your-name.workers.dev/v1/chat/completions
 */

export default {
  async fetch(request, env, ctx) {
    // 1. Обработка CORS pre-flight
    if (request.method === "OPTIONS") {
      return new Response(null, {
        status: 204,
        headers: {
          "Access-Control-Allow-Origin": "*",
          "Access-Control-Allow-Methods": "POST, GET, OPTIONS",
          "Access-Control-Allow-Headers": "Content-Type, Authorization, x-relay-key",
          "Access-Control-Max-Age": "86400",
        },
      });
    }

    // Проверка статуса (GET-запрос для проверки работоспособности)
    if (request.method === "GET") {
      return new Response(JSON.stringify({ status: "ok", service: "GhostAI CDN Relay", version: "1.0" }), {
        status: 200,
        headers: { "Content-Type": "application/json" },
      });
    }

    if (request.method !== "POST") {
      return new Response("Method Not Allowed", { status: 405 });
    }

    try {
      // 2. Получение ключа авторизации (из переменной окружения Worker или из заголовка клиента)
      const clientAuth = request.headers.get("Authorization");
      const apiKey = env.OPENROUTER_API_KEY || (clientAuth ? clientAuth.replace("Bearer ", "").trim() : "");

      if (!apiKey) {
        return new Response(JSON.stringify({
          error: { message: "API key is missing. Set OPENROUTER_API_KEY in Cloudflare Worker settings or send Authorization header." }
        }), {
          status: 401,
          headers: { "Content-Type": "application/json" }
        });
      }

      // 3. Чтение и валидация тела запроса от GhostAI
      const requestData = await request.json();

      // 4. Пересылка в OpenRouter (или OpenAI)
      const openRouterUrl = "https://openrouter.ai/api/v1/chat/completions";
      
      const upstreamResponse = await fetch(openRouterUrl, {
        method: "POST",
        headers: {
          "Content-Type": "application/json",
          "Authorization": `Bearer ${apiKey}`,
          "HTTP-Referer": "https://ghostai.local",
          "X-Title": "GhostAI Assistant",
        },
        body: JSON.stringify(requestData),
      });

      const responseBody = await upstreamResponse.text();

      // 5. Возврат ответа клиенту
      return new Response(responseBody, {
        status: upstreamResponse.status,
        headers: {
          "Content-Type": "application/json",
          "Access-Control-Allow-Origin": "*",
          "Cache-Control": "no-store, no-cache, must-revalidate",
        },
      });

    } catch (err) {
      return new Response(JSON.stringify({
        error: { message: "Relay Internal Error: " + err.message }
      }), {
        status: 502,
        headers: { "Content-Type": "application/json" }
      });
    }
  },
};

