#include "bot.h"
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

#define TG_API_BASE                  "https://api.telegram.org/bot"
#define TG_FILE_BASE                 "https://api.telegram.org/file/bot"
#define POLL_TIMEOUT                 30
#define MAX_REPLY_LEN                4096
#define MAX_FILE_MB                  20
#define MAX_LIST_ENTRIES             10
#define HTTP_TIMEOUT_SECS            60L
#define HTTP_DOWNLOAD_TIMEOUT_SECS   120L
#define RECONNECT_DELAY_SECS         5
#define RETRY_DELAY_SECS             2
#define TG_CMD_START                 "/start"
#define TG_CMD_HELP                  "/help"
#define TG_CMD_LIST                  "/list"
#define TG_CMD_DELETE                "/delete"
#define TG_CMD_RETRY                 "/retry"
#define PROMPT_REFRESH_INTERVAL_SECS 300

/* Forward declaration */
static void on_prompt_change(const char *prompt_name, const char *new_content, void *userdata);

struct TgBot {
    const Config *cfg;
    Processor *processor;
    DBBackend *db;
    StorageBackend *storage;
    PromptFetcher *prompt_fetcher;
    atomic_int running;
    long offset;
};

/* ── HTTP helpers ─────────────────────────────────────────────────────────── */

static char *http_get(const char *url, long timeout_sec) {
    LOG_DEBUG("GET %s (timeout=%lds)", url, timeout_sec);

    /* Create temporary client for this request */
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

    /* Transfer ownership of body to caller */
    char *body = resp.body;
    resp.body = NULL;

    http_response_free(&resp);
    http_client_free(client);
    return body;
}

static char *http_post_json(const char *url, const char *json_body) {
    LOG_DEBUG("POST %s", url);

    /* Create temporary client for this request */
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

    /* Transfer ownership of body to caller */
    char *body = resp.body;
    resp.body = NULL;

    http_response_free(&resp);
    http_client_free(client);
    return body;
}

/* ── Telegram helpers ─────────────────────────────────────────────────────── */

/* Check Telegram API JSON response for errors. Logs on failure, silently
 * ignores NULL input. Returns 0 on ok=true, -1 on error/parse failure. */
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

    char url[MAX_URL_LEN];
    snprintf(url, sizeof(url), "%s%s/sendMessage", TG_API_BASE, bot->cfg->telegram_token);

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

static void tg_send_message_with_keyboard(const TgBot *bot, int64_t chat_id, const char *text,
                                          int64_t db_file_id) {
    if (!text) {
        LOG_ERROR("tg_send_message_with_keyboard: text is NULL");
        return;
    }

    char url[MAX_URL_LEN];
    snprintf(url, sizeof(url), "%s%s/sendMessage", TG_API_BASE, bot->cfg->telegram_token);

    cJSON *payload = cJSON_CreateObject();
    cJSON_AddNumberToObject(payload, "chat_id", (double)chat_id);
    cJSON_AddStringToObject(payload, "text", text);
    cJSON_AddStringToObject(payload, "parse_mode", "Markdown");

    /* Build inline keyboard with Retry and Delete buttons */
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

static void tg_answer_callback_query(const TgBot *bot, const char *query_id, const char *text) {
    char url[MAX_URL_LEN];
    snprintf(url, sizeof(url), "%s%s/answerCallbackQuery", TG_API_BASE, bot->cfg->telegram_token);

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

static void tg_delete_message(const TgBot *bot, int64_t chat_id, int64_t message_id) {
    char url[MAX_URL_LEN];
    snprintf(url, sizeof(url), "%s%s/deleteMessage", TG_API_BASE, bot->cfg->telegram_token);

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

static char *tg_get_file_path(const TgBot *bot, const char *file_id) {
    char url[MAX_URL_LEN];
    char *encoded_id = url_percent_encode(file_id);
    if (!encoded_id)
        return NULL;

    snprintf(url, sizeof(url), "%s%s/getFile?file_id=%s", TG_API_BASE, bot->cfg->telegram_token,
             encoded_id);
    free(encoded_id);

    char *resp = http_get(url, HTTP_TIMEOUT_SECS);
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

static uint8_t *tg_download_file(const TgBot *bot, const char *file_path, size_t *out_len) {
    char url[MAX_URL_LEN];
    snprintf(url, sizeof(url), "%s%s/%s", TG_FILE_BASE, bot->cfg->telegram_token, file_path);
    LOG_DEBUG("downloading file: %s", file_path);

    HttpClient *client = http_client_new();
    if (!client)
        return NULL;

    HttpResponse resp;
    int rc = http_client_get(client, url, HTTP_DOWNLOAD_TIMEOUT_SECS, &resp);

    if (rc != 0 || !resp.success) {
        LOG_ERROR("download failed: %s", resp.error);
        http_response_free(&resp);
        http_client_free(client);
        return NULL;
    }

    *out_len = resp.body_len;
    uint8_t *data = (uint8_t *)resp.body;
    resp.body = NULL; /* Transfer ownership */

    http_response_free(&resp);
    http_client_free(client);
    return data;
}

/* ── update handlers ─────────────────────────────────────────────────────── */

typedef struct {
    char *buf;
    int pos;
    int cap;
    int count;
} ListCtx;

static void list_cb(const FileRecord *rec, void *ud) {
    ListCtx *c = (ListCtx *)ud;
    if (c->count >= MAX_LIST_ENTRIES)
        return;

    int remaining = c->cap - c->pos;
    if (remaining <= 0)
        return;

    int written =
        snprintf(c->buf + c->pos, (size_t)remaining, "%d. %s\n   %s | %lld bytes | OCR: %s\n\n",
                 c->count + 1, rec->saved_file_name, rec->original_file_name,
                 (long long)rec->file_size_bytes, rec->is_ocr_processed ? "done" : "pending");

    if (written > 0 && written < remaining) {
        c->pos += written;
    } else {
        c->pos = c->cap - 1;
    }
    c->count++;
}

static int str_starts_with(const char *text, const char *cmd) {
    size_t cmd_len = strlen(cmd);
    return strncmp(text, cmd, cmd_len) == 0 && (text[cmd_len] == '\0' || text[cmd_len] == ' ');
}

/* Delete a file record and its storage files. Returns 0 on success. */
static int do_delete_file(const TgBot *bot, int64_t db_file_id, char *msg_buf, size_t msg_len) {
    FileRecord rec;
    int found = db_backend_find_by_id(bot->db, db_file_id, &rec);
    if (found != 0) {
        snprintf(msg_buf, msg_len, "File with ID %lld not found.", (long long)db_file_id);
        return -1;
    }

    /* Delete storage files (best-effort) */
    if (bot->storage && rec.saved_file_name[0] != '\0')
        storage_backend_delete_file(bot->storage, rec.saved_file_name);
    if (bot->storage && rec.ocr_file_name[0] != '\0')
        storage_backend_delete_file(bot->storage, rec.ocr_file_name);

    /* Delete from DB (CASCADE will delete parsed_receipts) */
    int rc = db_backend_delete_file(bot->db, db_file_id);
    if (rc != 0) {
        snprintf(msg_buf, msg_len, "Failed to delete file %lld.", (long long)db_file_id);
        return -1;
    }

    snprintf(msg_buf, msg_len, "Deleted: %s (ID: %lld)", rec.original_file_name,
             (long long)db_file_id);
    return 0;
}

static void handle_command(TgBot *bot, int64_t chat_id, const char *text) {
    if (str_starts_with(text, TG_CMD_START) || str_starts_with(text, TG_CMD_HELP)) {
        tg_send_message(bot, chat_id,
                        "OCR Bot\n\n"
                        "Send me any image (JPEG, PNG, WebP, BMP, GIF, TIFF) "
                        "or PDF document and I will:\n"
                        "  1. Save it with a timestamped filename\n"
                        "  2. Extract all text via Gemini OCR\n"
                        "  3. Send you the result\n\n"
                        "Commands:\n"
                        "  /help      - show this message\n"
                        "  /list      - show recently uploaded files\n"
                        "  /delete ID - delete a file by its ID\n"
                        "  /retry ID  - retry OCR parsing for a file");
        return;
    }

    if (str_starts_with(text, TG_CMD_LIST)) {
        char reply[MAX_REPLY_LEN];
        int pos = 0;
        pos += snprintf(reply + pos, sizeof(reply) - pos, "Recent uploads:\n\n");

        ListCtx ctx = {reply, pos, (int)sizeof(reply), 0};
        db_backend_list(bot->db, list_cb, &ctx);

        tg_send_message(bot, chat_id, reply);
        return;
    }

    if (str_starts_with(text, TG_CMD_DELETE)) {
        const char *arg = text + strlen(TG_CMD_DELETE);
        while (*arg == ' ')
            arg++;

        if (*arg == '\0') {
            tg_send_message(bot, chat_id, "Usage: /delete <id>");
            return;
        }

        char *end;
        long long id = strtoll(arg, &end, 10);
        if (end == arg || *end != '\0') {
            tg_send_message(bot, chat_id, "Invalid ID. Usage: /delete <id>");
            return;
        }

        char msg[MAX_REPLY_LEN];
        do_delete_file(bot, (int64_t)id, msg, sizeof(msg));
        tg_send_message(bot, chat_id, msg);
        return;
    }

    if (str_starts_with(text, TG_CMD_RETRY)) {
        const char *arg = text + strlen(TG_CMD_RETRY);
        while (*arg == ' ')
            arg++;

        if (*arg == '\0') {
            tg_send_message(bot, chat_id, "Usage: /retry <id>");
            return;
        }

        char *end;
        long long id = strtoll(arg, &end, 10);
        if (end == arg || *end != '\0') {
            tg_send_message(bot, chat_id, "Invalid ID. Usage: /retry <id>");
            return;
        }

        char reply[MAX_REPLY_LEN];
        int rc = processor_retry_ocr(bot->processor, (int64_t)id, reply, sizeof(reply));
        if (rc == 0) {
            tg_send_message_with_keyboard(bot, chat_id, reply, (int64_t)id);
        } else {
            tg_send_message(bot, chat_id, reply);
        }
        return;
    }

    tg_send_message(bot, chat_id, "Unknown command. Send /help for instructions.");
}

static void handle_file(TgBot *bot, int64_t chat_id, const char *file_id,
                        const char *original_name) {
    LOG_DEBUG("handling file: %s from chat %lld", original_name, (long long)chat_id);

    tg_send_message(bot, chat_id, "Downloading...");

    char *file_path = tg_get_file_path(bot, file_id);
    if (!file_path) {
        LOG_ERROR("could not retrieve file info from Telegram");
        tg_send_message(bot, chat_id, "Could not retrieve file info from Telegram.");
        return;
    }

    size_t data_len = 0;
    uint8_t *data = tg_download_file(bot, file_path, &data_len);
    free(file_path);

    if (!data) {
        LOG_ERROR("file download failed");
        tg_send_message(bot, chat_id, "File download failed.");
        return;
    }

    if (data_len == 0) {
        LOG_WARN("downloaded file is empty");
        tg_send_message(bot, chat_id, "Downloaded file is empty.");
        free(data);
        return;
    }

    char reply[MAX_REPLY_LEN];
    tg_send_message(bot, chat_id, "Processing...");

    int64_t db_file_id = 0;
    processor_handle_file(bot->processor, original_name, data, data_len, reply, sizeof(reply),
                          &db_file_id);
    free(data);

    if (db_file_id > 0) {
        tg_send_message_with_keyboard(bot, chat_id, reply, db_file_id);
    } else {
        tg_send_message(bot, chat_id, reply);
    }
}

/* ── update dispatcher ───────────────────────────────────────────────────── */

static int dispatch_auth_check(const TgBot *bot, cJSON *message, int64_t user_id) {
    if (config_is_allowed(bot->cfg, user_id))
        return 1;

    cJSON *chat = cJSON_GetObjectItem(message, "chat");
    cJSON *chat_id = chat ? cJSON_GetObjectItem(chat, "id") : NULL;
    LOG_WARN("unauthorized user %lld attempted access", (long long)user_id);
    if (cJSON_IsNumber(chat_id))
        tg_send_message(bot, (int64_t)chat_id->valuedouble,
                        "You are not authorised to use this bot.");
    return 0;
}

static int64_t dispatch_get_chat_id(cJSON *message) {
    cJSON *chat = cJSON_GetObjectItem(message, "chat");
    cJSON *chat_id = chat ? cJSON_GetObjectItem(chat, "id") : NULL;
    if (!cJSON_IsNumber(chat_id))
        return 0;
    return (int64_t)chat_id->valuedouble;
}

static void dispatch_photo(TgBot *bot, int64_t chat_id, int64_t user_id, cJSON *photos) {
    int n = cJSON_GetArraySize(photos);
    if (n <= 0)
        return;
    cJSON *largest = cJSON_GetArrayItem(photos, n - 1);
    if (!largest)
        return;
    cJSON *fid = cJSON_GetObjectItem(largest, "file_id");
    if (cJSON_IsString(fid)) {
        LOG_DEBUG("received photo from user %lld", (long long)user_id);
        handle_file(bot, chat_id, fid->valuestring, "photo.jpg");
    }
}

static void dispatch_document(TgBot *bot, int64_t chat_id, int64_t user_id, cJSON *doc) {
    cJSON *fid = cJSON_GetObjectItem(doc, "file_id");
    cJSON *fname = cJSON_GetObjectItem(doc, "file_name");
    cJSON *fsize = cJSON_GetObjectItem(doc, "file_size");

    if (cJSON_IsNumber(fsize) && fsize->valuedouble > MAX_FILE_MB * 1024.0 * 1024.0) {
        LOG_WARN("user %lld attempted to upload file exceeding 20MB limit", (long long)user_id);
        tg_send_message(bot, chat_id, "File exceeds 20 MB - Telegram bot API limit.");
        return;
    }

    const char *name = cJSON_IsString(fname) ? fname->valuestring : "file";
    if (cJSON_IsString(fid)) {
        LOG_DEBUG("received document from user %lld: %s", (long long)user_id, name);
        handle_file(bot, chat_id, fid->valuestring, name);
    }
}

static void dispatch_update(TgBot *bot, cJSON *update) {
    /* Handle callback_query (inline keyboard button clicks) */
    cJSON *cb_query = cJSON_GetObjectItem(update, "callback_query");
    if (cb_query) {
        cJSON *cb_data = cJSON_GetObjectItem(cb_query, "data");
        cJSON *cb_id = cJSON_GetObjectItem(cb_query, "id");
        cJSON *cb_from = cJSON_GetObjectItem(cb_query, "from");
        cJSON *cb_message = cJSON_GetObjectItem(cb_query, "message");

        if (!cJSON_IsString(cb_data) || !cJSON_IsString(cb_id) || !cb_from || !cb_message)
            return;

        cJSON *from_id = cJSON_GetObjectItem(cb_from, "id");
        if (!cJSON_IsNumber(from_id))
            return;
        int64_t user_id = (int64_t)from_id->valuedouble;

        if (!config_is_allowed(bot->cfg, user_id)) {
            LOG_WARN("unauthorized callback from user %lld", (long long)user_id);
            tg_answer_callback_query(bot, cb_id->valuestring, "Not authorized.");
            return;
        }

        const char *data = cb_data->valuestring;
        if (strncmp(data, "delete:", 7) == 0) {
            long long file_id = strtoll(data + 7, NULL, 10);

            cJSON *chat = cJSON_GetObjectItem(cb_message, "chat");
            cJSON *chat_id = chat ? cJSON_GetObjectItem(chat, "id") : NULL;
            cJSON *msg_id = cJSON_GetObjectItem(cb_message, "message_id");

            char msg[MAX_REPLY_LEN];
            int rc = do_delete_file(bot, (int64_t)file_id, msg, sizeof(msg));

            if (rc == 0) {
                if (cJSON_IsNumber(chat_id) && cJSON_IsNumber(msg_id)) {
                    tg_delete_message(bot, (int64_t)chat_id->valuedouble,
                                      (int64_t)msg_id->valuedouble);
                }
                tg_answer_callback_query(bot, cb_id->valuestring, "File deleted.");
            } else {
                tg_answer_callback_query(bot, cb_id->valuestring, msg);
            }
        } else if (strncmp(data, "retry:", 6) == 0) {
            long long file_id = strtoll(data + 6, NULL, 10);

            cJSON *chat = cJSON_GetObjectItem(cb_message, "chat");
            cJSON *chat_id_obj = chat ? cJSON_GetObjectItem(chat, "id") : NULL;
            int64_t retry_chat_id =
                cJSON_IsNumber(chat_id_obj) ? (int64_t)chat_id_obj->valuedouble : 0;

            tg_answer_callback_query(bot, cb_id->valuestring, "Retrying OCR...");

            if (retry_chat_id != 0) {
                char reply[MAX_REPLY_LEN];
                int rc =
                    processor_retry_ocr(bot->processor, (int64_t)file_id, reply, sizeof(reply));
                if (rc == 0) {
                    tg_send_message_with_keyboard(bot, retry_chat_id, reply, (int64_t)file_id);
                } else {
                    tg_send_message(bot, retry_chat_id, reply);
                }
            }
        } else {
            tg_answer_callback_query(bot, cb_id->valuestring, NULL);
        }
        return;
    }

    cJSON *message = cJSON_GetObjectItem(update, "message");
    if (!message)
        return;

    cJSON *from = cJSON_GetObjectItem(message, "from");
    cJSON *from_id = from ? cJSON_GetObjectItem(from, "id") : NULL;
    if (!cJSON_IsNumber(from_id))
        return;
    int64_t user_id = (int64_t)from_id->valuedouble;

    if (!dispatch_auth_check(bot, message, user_id))
        return;

    int64_t chat_id = dispatch_get_chat_id(message);
    if (chat_id == 0)
        return;

    /* Text / command */
    cJSON *text = cJSON_GetObjectItem(message, "text");
    if (cJSON_IsString(text)) {
        LOG_DEBUG("received command from user %lld", (long long)user_id);
        handle_command(bot, chat_id, text->valuestring);
        return;
    }

    /* Photo (Telegram compresses; pick largest size) */
    cJSON *photos = cJSON_GetObjectItem(message, "photo");
    if (cJSON_IsArray(photos)) {
        dispatch_photo(bot, chat_id, user_id, photos);
        return;
    }

    /* Document (PDF, WebP, uncompressed image, etc.) */
    cJSON *doc = cJSON_GetObjectItem(message, "document");
    if (doc) {
        dispatch_document(bot, chat_id, user_id, doc);
        return;
    }

    LOG_WARN("unsupported message type from user %lld", (long long)user_id);
    tg_send_message(bot, chat_id, "Unsupported message type. Please send an image or PDF.");
}

/* ── public API ───────────────────────────────────────────────────────────── */

TgBot *bot_new(const Config *cfg, Processor *processor, DBBackend *db, StorageBackend *storage) {
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
    char url[MAX_URL_LEN];
    LOG_INFO("starting long-poll loop (timeout=%ds)", POLL_TIMEOUT);

    while (atomic_load(&bot->running)) {
        snprintf(
            url, sizeof(url),
            "%s%s/"
            "getUpdates?offset=%ld&timeout=%d&allowed_updates=[\"message\",\"callback_query\"]",
            TG_API_BASE, bot->cfg->telegram_token, bot->offset, POLL_TIMEOUT);

        char *body = http_get(url, POLL_TIMEOUT);
        if (!body) {
            LOG_ERROR("getUpdates failed, retrying in %ds", RECONNECT_DELAY_SECS);
            sleep(RECONNECT_DELAY_SECS);
            continue;
        }

        cJSON *json = cJSON_Parse(body);
        free(body);

        if (!json) {
            LOG_ERROR("failed to parse getUpdates response");
            sleep(RETRY_DELAY_SECS);
            continue;
        }

        cJSON *ok = cJSON_GetObjectItem(json, "ok");
        if (!cJSON_IsTrue(ok)) {
            cJSON *desc = cJSON_GetObjectItem(json, "description");
            LOG_ERROR("getUpdates not ok: %s",
                      cJSON_IsString(desc) ? desc->valuestring : "unknown");
            cJSON_Delete(json);
            sleep(RECONNECT_DELAY_SECS);
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
                    dispatch_update(bot, update);
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

/* ── Prompt change notification ───────────────────────────────────────────── */

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

static void on_prompt_change(const char *prompt_name, const char *new_content, void *userdata) {
    const TgBot *bot = (const TgBot *)userdata;
    bot_notify_prompt_change(bot, prompt_name, new_content);
}
