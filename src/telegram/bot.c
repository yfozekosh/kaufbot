#include "bot.h"
#include "bot_commands.h"
#include "cJSON.h"
#include "config.h"
#include "http_client.h"
#include "prompt_fetcher.h"
#include "utils.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* URL buffer large enough for API base + token + endpoint */
#define BOT_URL_BUF_LEN 2048

#define MAX_REPLY_LEN                4096
#define PROMPT_REFRESH_INTERVAL_SECS 300

/* Forward declaration */
static void on_prompt_change(const char *prompt_name, const char *new_content, void *userdata);

/* ── HTTP helpers ─────────────────────────────────────────────────────────── */

static char *http_get(const char *url, long timeout_sec) {
    LOG_DEBUG("GET %s (timeout=%lds)", url, timeout_sec);

    HttpClient *client = http_client_new();
    if (!client)
        return NULL;

    HttpResponse resp;
    int rc = http_client_get(client, url, timeout_sec, &resp);

    if (rc != 0 || !resp.success) {
        LOG_ERROR("GET failed: %s", resp.error);
        http_response_free(&resp);
        http_client_free(client);
        return NULL;
    }

    char *body = resp.body;
    resp.body = NULL;

    http_response_free(&resp);
    http_client_free(client);
    return body;
}

static char *http_post_json(const char *url, const char *json_body) {
    LOG_DEBUG("POST %s", url);

    HttpClient *client = http_client_new();
    if (!client)
        return NULL;

    HttpResponse resp;
    int rc = http_client_post_json(client, url, json_body, &resp);

    if (rc != 0 || !resp.success) {
        LOG_ERROR("POST failed: %s", resp.error);
        http_response_free(&resp);
        http_client_free(client);
        return NULL;
    }

    char *body = resp.body;
    resp.body = NULL;

    http_response_free(&resp);
    http_client_free(client);
    return body;
}

/* ── Telegram helpers ─────────────────────────────────────────────────────── */

static int tg_check_ok(const char *resp, const char *context) {
    if (!resp)
        return -1;
    cJSON *json = cJSON_Parse(resp);
    if (!json) {
        LOG_ERROR("%s: failed to parse response", context);
        return -1;
    }
    cJSON *ok = cJSON_GetObjectItem(json, "ok");
    if (!cJSON_IsTrue(ok)) {
        cJSON *desc = cJSON_GetObjectItem(json, "description");
        cJSON *err_code = cJSON_GetObjectItem(json, "error_code");
        LOG_ERROR("%s: %s (code: %d)", context,
                  cJSON_IsString(desc) ? desc->valuestring : "unknown",
                  cJSON_IsNumber(err_code) ? err_code->valueint : -1);
        cJSON_Delete(json);
        return -1;
    }
    cJSON_Delete(json);
    return 0;
}

void tg_send_message(const TgBot *bot, int64_t chat_id, const char *text) {
    if (!text) {
        LOG_ERROR("tg_send_message: text is NULL");
        return;
    }

    char url[BOT_URL_BUF_LEN];
    snprintf(url, sizeof(url), "%s%s/sendMessage", bot->cfg->telegram_api_base,
             bot->cfg->telegram_token);

    cJSON *payload = cJSON_CreateObject();
    cJSON_AddNumberToObject(payload, "chat_id", (double)chat_id);
    cJSON_AddStringToObject(payload, "text", text);

    char *body = cJSON_PrintUnformatted(payload);
    cJSON_Delete(payload);

    if (!body)
        return;

    char *resp = http_post_json(url, body);
    free(body);
    tg_check_ok(resp, "sendMessage");
    free(resp);
}

void tg_send_message_with_keyboard(const TgBot *bot, int64_t chat_id, const char *text,
                                   int64_t db_file_id) {
    if (!text) {
        LOG_ERROR("tg_send_message_with_keyboard: text is NULL");
        return;
    }

    char url[BOT_URL_BUF_LEN];
    snprintf(url, sizeof(url), "%s%s/sendMessage", bot->cfg->telegram_api_base,
             bot->cfg->telegram_token);

    cJSON *payload = cJSON_CreateObject();
    cJSON_AddNumberToObject(payload, "chat_id", (double)chat_id);
    cJSON_AddStringToObject(payload, "text", text);
    cJSON_AddStringToObject(payload, "parse_mode", "Markdown");

    cJSON *keyboard = cJSON_CreateArray();
    cJSON *row = cJSON_CreateArray();

    cJSON *retry_btn = cJSON_CreateObject();
    char retry_data[64];
    snprintf(retry_data, sizeof(retry_data), "retry:%lld", (long long)db_file_id);
    cJSON_AddStringToObject(retry_btn, "text", "\xF0\x9F\x94\x84 Retry OCR");
    cJSON_AddStringToObject(retry_btn, "callback_data", retry_data);
    cJSON_AddItemToArray(row, retry_btn);

    cJSON *delete_btn = cJSON_CreateObject();
    char delete_data[64];
    snprintf(delete_data, sizeof(delete_data), "delete:%lld", (long long)db_file_id);
    cJSON_AddStringToObject(delete_btn, "text", "\xF0\x9F\x97\x91\xEF\xB8\x8F Delete");
    cJSON_AddStringToObject(delete_btn, "callback_data", delete_data);
    cJSON_AddItemToArray(row, delete_btn);

    cJSON_AddItemToArray(keyboard, row);
    cJSON *reply_markup = cJSON_CreateObject();
    cJSON_AddItemToObject(reply_markup, "inline_keyboard", keyboard);
    cJSON_AddItemToObject(payload, "reply_markup", reply_markup);

    char *body = cJSON_PrintUnformatted(payload);
    cJSON_Delete(payload);

    if (!body)
        return;

    char *resp = http_post_json(url, body);
    free(body);
    tg_check_ok(resp, "sendMessage (keyboard)");
    free(resp);
}

void tg_answer_callback_query(const TgBot *bot, const char *query_id, const char *text) {
    char url[BOT_URL_BUF_LEN];
    snprintf(url, sizeof(url), "%s%s/answerCallbackQuery", bot->cfg->telegram_api_base,
             bot->cfg->telegram_token);

    cJSON *payload = cJSON_CreateObject();
    cJSON_AddStringToObject(payload, "callback_query_id", query_id);
    if (text)
        cJSON_AddStringToObject(payload, "text", text);

    char *body = cJSON_PrintUnformatted(payload);
    cJSON_Delete(payload);

    if (!body)
        return;

    char *resp = http_post_json(url, body);
    free(body);
    tg_check_ok(resp, "answerCallbackQuery");
    free(resp);
}

void tg_delete_message(const TgBot *bot, int64_t chat_id, int64_t message_id) {
    char url[BOT_URL_BUF_LEN];
    snprintf(url, sizeof(url), "%s%s/deleteMessage", bot->cfg->telegram_api_base,
             bot->cfg->telegram_token);

    cJSON *payload = cJSON_CreateObject();
    cJSON_AddNumberToObject(payload, "chat_id", (double)chat_id);
    cJSON_AddNumberToObject(payload, "message_id", (double)message_id);

    char *body = cJSON_PrintUnformatted(payload);
    cJSON_Delete(payload);

    if (!body)
        return;

    char *resp = http_post_json(url, body);
    free(body);
    tg_check_ok(resp, "deleteMessage");
    free(resp);
}

char *tg_get_file_path(const TgBot *bot, const char *file_id) {
    char url[BOT_URL_BUF_LEN];
    char *encoded_id = url_percent_encode(file_id);
    if (!encoded_id)
        return NULL;

    snprintf(url, sizeof(url), "%s%s/getFile?file_id=%s", bot->cfg->telegram_api_base,
             bot->cfg->telegram_token, encoded_id);
    free(encoded_id);

    char *resp = http_get(url, bot->cfg->telegram_http_timeout_secs);
    if (!resp)
        return NULL;

    cJSON *json = cJSON_Parse(resp);
    free(resp);
    if (!json)
        return NULL;

    cJSON *ok = cJSON_GetObjectItem(json, "ok");
    if (!cJSON_IsTrue(ok)) {
        cJSON *desc = cJSON_GetObjectItem(json, "description");
        LOG_ERROR("getFile: %s", cJSON_IsString(desc) ? desc->valuestring : "unknown");
        cJSON_Delete(json);
        return NULL;
    }

    cJSON *result = cJSON_GetObjectItem(json, "result");
    cJSON *file_path = result ? cJSON_GetObjectItem(result, "file_path") : NULL;

    char *path = NULL;
    if (cJSON_IsString(file_path))
        path = strdup(file_path->valuestring);

    cJSON_Delete(json);
    return path;
}

uint8_t *tg_download_file(const TgBot *bot, const char *file_path, size_t *out_len) {
    char url[BOT_URL_BUF_LEN];
    snprintf(url, sizeof(url), "%s%s/%s", bot->cfg->telegram_file_base, bot->cfg->telegram_token,
             file_path);
    LOG_DEBUG("downloading file: %s", file_path);

    HttpClient *client = http_client_new();
    if (!client)
        return NULL;

    HttpResponse resp;
    int rc = http_client_get(client, url, bot->cfg->telegram_download_timeout_secs, &resp);

    if (rc != 0 || !resp.success) {
        LOG_ERROR("download failed: %s", resp.error);
        http_response_free(&resp);
        http_client_free(client);
        return NULL;
    }

    *out_len = resp.body_len;
    uint8_t *data = (uint8_t *)resp.body;
    resp.body = NULL;

    http_response_free(&resp);
    http_client_free(client);
    return data;
}

/* ── public API ───────────────────────────────────────────────────────────── */

TgBot *bot_new(const Config *cfg, struct Processor *processor, DBBackend *db,
               StorageBackend *storage) {
    TgBot *bot = calloc(1, sizeof(TgBot));
    if (!bot)
        return NULL;
    bot->cfg = cfg;
    bot->processor = processor;
    bot->db = db;
    bot->storage = storage;
    bot->offset = 0;
    atomic_store(&bot->running, 1);

    bot->prompt_fetcher =
        prompt_fetcher_new(db, PROMPT_REFRESH_INTERVAL_SECS, on_prompt_change, bot);
    if (!bot->prompt_fetcher) {
        LOG_WARN("failed to create prompt fetcher, continuing without it");
    }

    return bot;
}

void bot_free(TgBot *bot) {
    if (bot) {
        prompt_fetcher_free(bot->prompt_fetcher);
        free(bot);
    }
}

void bot_stop(TgBot *bot) {
    if (bot) {
        atomic_store(&bot->running, 0);
    }
}

void bot_start(TgBot *bot) {
    char url[BOT_URL_BUF_LEN];
    LOG_INFO("starting long-poll loop (timeout=%lds)", bot->cfg->telegram_poll_timeout_secs);

    while (atomic_load(&bot->running)) {
        snprintf(
            url, sizeof(url),
            "%s%s/"
            "getUpdates?offset=%ld&timeout=%ld&allowed_updates=[\"message\",\"callback_query\"]",
            bot->cfg->telegram_api_base, bot->cfg->telegram_token, bot->offset,
            bot->cfg->telegram_poll_timeout_secs);

        char *body = http_get(url, bot->cfg->telegram_poll_timeout_secs);
        if (!body) {
            LOG_ERROR("getUpdates failed, retrying in %lds",
                      bot->cfg->telegram_reconnect_delay_secs);
            sleep(bot->cfg->telegram_reconnect_delay_secs);
            continue;
        }

        cJSON *json = cJSON_Parse(body);
        free(body);

        if (!json) {
            LOG_ERROR("failed to parse getUpdates response");
            sleep(bot->cfg->telegram_retry_delay_secs);
            continue;
        }

        cJSON *ok = cJSON_GetObjectItem(json, "ok");
        if (!cJSON_IsTrue(ok)) {
            cJSON *desc = cJSON_GetObjectItem(json, "description");
            LOG_ERROR("getUpdates not ok: %s",
                      cJSON_IsString(desc) ? desc->valuestring : "unknown");
            cJSON_Delete(json);
            sleep(bot->cfg->telegram_reconnect_delay_secs);
            continue;
        }

        cJSON *result = cJSON_GetObjectItem(json, "result");
        if (cJSON_IsArray(result)) {
            int n = cJSON_GetArraySize(result);
            LOG_DEBUG("received %d updates", n);
            for (int i = 0; i < n; i++) {
                cJSON *update = cJSON_GetArrayItem(result, i);
                cJSON *uid = cJSON_GetObjectItem(update, "update_id");
                if (cJSON_IsNumber(uid)) {
                    bot_dispatch_update(bot, update);
                    bot->offset = (long)uid->valuedouble + 1;
                }
            }
        }

        cJSON_Delete(json);

        prompt_fetcher_tick(bot->prompt_fetcher);
    }

    LOG_INFO("polling loop stopped");
}

void bot_notify_startup(const TgBot *bot) {
    if (!bot || !bot->cfg)
        return;

    const char *msg = "✅ *Kaufbot Deployed and Running!*\n\n"
                      "📷 Send me receipt images and I will:\n"
                      "  1. Extract text via Gemini OCR\n"
                      "  2. Parse line items and totals\n"
                      "  3. Store results for later retrieval\n\n"
                      "Commands:\n"
                      "  /start - welcome message\n"
                      "  /help  - show instructions\n"
                      "  /list  - show recent receipts";

    for (int i = 0; i < bot->cfg->allowed_users_count; i++) {
        int64_t user_id = bot->cfg->allowed_users[i];
        tg_send_message(bot, user_id, msg);
    }
}

void bot_notify_prompt_change(const TgBot *bot, const char *prompt_name, const char *new_content) {
    if (!bot || !bot->cfg)
        return;

    char msg[MAX_REPLY_LEN];
    snprintf(msg, sizeof(msg), "Prompt updated: %s\n\n%s", prompt_name ? prompt_name : "(unknown)",
             new_content ? new_content : "");

    for (int i = 0; i < bot->cfg->allowed_users_count; i++) {
        int64_t user_id = bot->cfg->allowed_users[i];
        tg_send_message(bot, user_id, msg);
    }
}

__attribute__((unused)) static void on_prompt_change(const char *prompt_name,
                                                     const char *new_content, void *userdata) {
    const TgBot *bot = (const TgBot *)userdata;
    bot_notify_prompt_change(bot, prompt_name, new_content);
}
