#include "bot.h"
#include "config.h"
#include "../third_party/cjson/cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <curl/curl.h>
#include <unistd.h>

#define TG_API_BASE    "https://api.telegram.org/bot"
#define POLL_TIMEOUT   30   /* seconds – Telegram long-poll window */
#define MAX_REPLY_LEN  4096 /* Telegram message limit is 4096 chars */
#define MAX_FILE_MB    20   /* Telegram bot file size limit */

struct TgBot {
    const Config   *cfg;
    Processor      *processor;
    DBBackend      *db;
    atomic_int      running;
    long            offset;   /* next update_id to request */
};

/* ── grow-buffer (reused from gemini.c pattern) ───────────────────────────── */

typedef struct {
    char  *data;
    size_t len;
    size_t cap;
} GrowBuf;

static size_t write_cb(void *ptr, size_t size, size_t nmemb, void *userdata)
{
    GrowBuf *buf = (GrowBuf *)userdata;
    size_t incoming = size * nmemb;
    size_t needed   = buf->len + incoming + 1;
    if (needed > buf->cap) {
        size_t new_cap = buf->cap ? buf->cap * 2 : 8192;
        while (new_cap < needed) new_cap *= 2;
        char *tmp = realloc(buf->data, new_cap);
        if (!tmp) return 0;
        buf->data = tmp;
        buf->cap  = new_cap;
    }
    memcpy(buf->data + buf->len, ptr, incoming);
    buf->len += incoming;
    buf->data[buf->len] = '\0';
    return incoming;
}

static void growbuf_free(GrowBuf *buf)
{
    free(buf->data);
    buf->data = NULL;
    buf->len  = 0;
    buf->cap  = 0;
}

/* ── HTTP helpers ─────────────────────────────────────────────────────────── */

/* Perform a GET request. Returns heap-allocated body or NULL on error. */
static char *http_get(const char *url)
{
    LOG_DEBUG("GET %s", url);
    CURL *curl = curl_easy_init();
    if (!curl) return NULL;

    GrowBuf buf = {0};
    curl_easy_setopt(curl, CURLOPT_URL,           url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,     &buf);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT,       60L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT,10L);
    /* Disable keep-alive to keep memory tidy on Pi */
    curl_easy_setopt(curl, CURLOPT_FORBID_REUSE,  1L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        LOG_ERROR("GET %s failed: %s", url, curl_easy_strerror(res));
        growbuf_free(&buf);
        return NULL;
    }
    return buf.data; /* caller must free */
}

/* Perform a long-poll GET (custom timeout). Returns heap-allocated body. */
static char *http_get_poll(const char *url, long timeout_sec)
{
    LOG_DEBUG("polling %s (timeout=%lds)", url, timeout_sec);
    CURL *curl = curl_easy_init();
    if (!curl) return NULL;

    GrowBuf buf = {0};
    curl_easy_setopt(curl, CURLOPT_URL,           url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,     &buf);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT,       timeout_sec + 10L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT,10L);
    curl_easy_setopt(curl, CURLOPT_FORBID_REUSE,  1L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        LOG_ERROR("poll failed: %s", curl_easy_strerror(res));
        growbuf_free(&buf);
        return NULL;
    }
    return buf.data;
}

/* POST JSON body, return heap-allocated response body. */
static char *http_post_json(const char *url, const char *json_body)
{
    LOG_DEBUG("POST %s", url);
    CURL *curl = curl_easy_init();
    if (!curl) return NULL;

    GrowBuf buf = {0};
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL,           url);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS,    json_body);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER,    headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,     &buf);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT,       30L);
    curl_easy_setopt(curl, CURLOPT_FORBID_REUSE,  1L);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        LOG_ERROR("POST failed: %s", curl_easy_strerror(res));
        growbuf_free(&buf);
        return NULL;
    }
    return buf.data;
}

/* ── Telegram helpers ─────────────────────────────────────────────────────── */

static void tg_send_message(const TgBot *bot, int64_t chat_id, const char *text)
{
    char url[MAX_URL_LEN];
    snprintf(url, sizeof(url), "%s%s/sendMessage",
             TG_API_BASE, bot->cfg->telegram_token);

    cJSON *payload = cJSON_CreateObject();
    cJSON_AddNumberToObject(payload, "chat_id", (double)chat_id);
    cJSON_AddStringToObject(payload, "text",    text);
    /* parse_mode omitted intentionally – plain text is safest for OCR output */

    char *body = cJSON_PrintUnformatted(payload);
    cJSON_Delete(payload);

    char *resp = http_post_json(url, body);
    free(body);
    free(resp); /* we don't inspect sendMessage responses */
}

/* Get Telegram file_path for a file_id. Returns heap string or NULL. */
static char *tg_get_file_path(const TgBot *bot, const char *file_id)
{
    char url[MAX_URL_LEN];
    snprintf(url, sizeof(url), "%s%s/getFile?file_id=%s",
             TG_API_BASE, bot->cfg->telegram_token, file_id);

    char *resp = http_get(url);
    if (!resp) return NULL;

    cJSON *json = cJSON_Parse(resp);
    free(resp);
    if (!json) return NULL;

    cJSON *result    = cJSON_GetObjectItem(json, "result");
    cJSON *file_path = result ? cJSON_GetObjectItem(result, "file_path") : NULL;

    char *path = NULL;
    if (cJSON_IsString(file_path))
        path = strdup(file_path->valuestring);

    cJSON_Delete(json);
    return path;
}

/* Download file bytes from Telegram CDN. Returns heap buffer or NULL.
 * Sets *out_len on success. */
static uint8_t *tg_download_file(const TgBot *bot, const char *file_path,
                                  size_t *out_len)
{
    char url[MAX_URL_LEN];
    snprintf(url, sizeof(url), "%s%s/%s",
             "https://api.telegram.org/file/bot",
             bot->cfg->telegram_token, file_path);
    LOG_DEBUG("downloading file: %s", file_path);

    CURL *curl = curl_easy_init();
    if (!curl) return NULL;

    GrowBuf buf = {0};
    curl_easy_setopt(curl, CURLOPT_URL,           url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,     &buf);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT,       120L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_FORBID_REUSE,   1L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        LOG_ERROR("download failed: %s", curl_easy_strerror(res));
        growbuf_free(&buf);
        return NULL;
    }

    *out_len = buf.len;
    return (uint8_t *)buf.data; /* caller must free */
}

/* ── update handlers ─────────────────────────────────────────────────────── */

/* Callback for db_list to format file records */
typedef struct { char *buf; int pos; int cap; int count; } ListCtx;

static void list_cb(const FileRecord *rec, void *ud)
{
    ListCtx *c = (ListCtx *)ud;
    if (c->count >= 10) return; /* cap at 10 entries */
    c->pos += snprintf(c->buf + c->pos, c->cap - c->pos,
        "%d. %s\n   📄 %s | %lld bytes | OCR: %s\n\n",
        c->count + 1,
        rec->saved_file_name,
        rec->original_file_name,
        (long long)rec->file_size_bytes,
        rec->is_ocr_processed ? "✅" : "❌");
    c->count++;
}

static void handle_command(TgBot *bot, int64_t chat_id, const char *text)
{
    if (strncmp(text, "/start", 6) == 0 || strncmp(text, "/help", 5) == 0) {
        tg_send_message(bot, chat_id,
            "👋 OCR Bot\n\n"
            "Send me any image (JPEG, PNG, WebP, BMP, GIF, TIFF) "
            "or PDF document and I will:\n"
            "  1. Save it with a timestamped filename\n"
            "  2. Extract all text via Gemini OCR\n"
            "  3. Send you the result\n\n"
            "Commands:\n"
            "  /help  – show this message\n"
            "  /list  – show recently uploaded files");
        return;
    }

    if (strncmp(text, "/list", 5) == 0) {
        /* Build reply string with recent uploads */
        char reply[MAX_REPLY_LEN];
        int  pos = 0;
        pos += snprintf(reply + pos, sizeof(reply) - pos, "📁 Recent uploads:\n\n");

        ListCtx ctx = { reply, pos, (int)sizeof(reply), 0 };
        db_backend_list(bot->db, list_cb, &ctx);
        
        tg_send_message(bot, chat_id, reply);
        return;
    }

    tg_send_message(bot, chat_id,
        "🤷 Unknown command. Send /help for instructions.");
}

static void handle_file(TgBot *bot, int64_t chat_id,
                        const char *file_id, const char *original_name)
{
    LOG_DEBUG("handling file: %s from chat %lld", original_name, (long long)chat_id);
    
    /* 1. Get file path from Telegram */
    tg_send_message(bot, chat_id, "⏳ Downloading…");

    char *file_path = tg_get_file_path(bot, file_id);
    if (!file_path) {
        LOG_ERROR("could not retrieve file info from Telegram");
        tg_send_message(bot, chat_id, "❌ Could not retrieve file info from Telegram.");
        return;
    }

    /* 2. Download bytes */
    size_t   data_len = 0;
    uint8_t *data     = tg_download_file(bot, file_path, &data_len);
    free(file_path);

    if (!data) {
        LOG_ERROR("file download failed");
        tg_send_message(bot, chat_id, "❌ File download failed.");
        return;
    }

    if (data_len == 0) {
        LOG_WARN("downloaded file is empty");
        tg_send_message(bot, chat_id, "❌ Downloaded file is empty.");
        free(data);
        return;
    }

    /* 3. Hand off to processor */
    char reply[MAX_REPLY_LEN];
    tg_send_message(bot, chat_id, "⏳ Processing…");

    processor_handle_file(bot->processor, original_name,
                          data, data_len,
                          reply, sizeof(reply));
    free(data);

    tg_send_message(bot, chat_id, reply);
}

/* ── update dispatcher ───────────────────────────────────────────────────── */

static void dispatch_update(TgBot *bot, cJSON *update)
{
    cJSON *message = cJSON_GetObjectItem(update, "message");
    if (!message) return;

    /* Sender */
    cJSON *from    = cJSON_GetObjectItem(message, "from");
    cJSON *from_id = from ? cJSON_GetObjectItem(from, "id") : NULL;
    if (!cJSON_IsNumber(from_id)) return;
    int64_t user_id = (int64_t)from_id->valuedouble;

    /* Whitelist check */
    if (!config_is_allowed(bot->cfg, user_id)) {
        cJSON *chat    = cJSON_GetObjectItem(message, "chat");
        cJSON *chat_id = chat ? cJSON_GetObjectItem(chat, "id") : NULL;
        LOG_WARN("unauthorized user %lld attempted access", (long long)user_id);
        if (cJSON_IsNumber(chat_id))
            tg_send_message(bot, (int64_t)chat_id->valuedouble,
                            "🚫 You are not authorised to use this bot.");
        return;
    }

    cJSON *chat    = cJSON_GetObjectItem(message, "chat");
    cJSON *chat_id = chat ? cJSON_GetObjectItem(chat, "id") : NULL;
    if (!cJSON_IsNumber(chat_id)) return;
    int64_t cid = (int64_t)chat_id->valuedouble;

    /* ── Text / command ── */
    cJSON *text = cJSON_GetObjectItem(message, "text");
    if (cJSON_IsString(text)) {
        LOG_DEBUG("received command from user %lld: %s", (long long)user_id, text->valuestring);
        handle_command(bot, cid, text->valuestring);
        return;
    }

    /* ── Photo (Telegram compresses; pick largest size) ── */
    cJSON *photos = cJSON_GetObjectItem(message, "photo");
    if (cJSON_IsArray(photos)) {
        int n = cJSON_GetArraySize(photos);
        cJSON *largest = cJSON_GetArrayItem(photos, n - 1);
        cJSON *fid     = cJSON_GetObjectItem(largest, "file_id");
        if (cJSON_IsString(fid)) {
            LOG_DEBUG("received photo from user %lld", (long long)user_id);
            handle_file(bot, cid, fid->valuestring, "photo.jpg");
        }
        return;
    }

    /* ── Document (PDF, WebP, uncompressed image, etc.) ── */
    cJSON *doc = cJSON_GetObjectItem(message, "document");
    if (doc) {
        cJSON *fid      = cJSON_GetObjectItem(doc, "file_id");
        cJSON *fname    = cJSON_GetObjectItem(doc, "file_name");
        cJSON *fsize    = cJSON_GetObjectItem(doc, "file_size");

        /* Reject files over Telegram's 20MB bot API limit */
        if (cJSON_IsNumber(fsize) &&
            fsize->valuedouble > MAX_FILE_MB * 1024 * 1024) {
            LOG_WARN("user %lld attempted to upload file exceeding 20MB limit", (long long)user_id);
            tg_send_message(bot, cid,
                "❌ File exceeds 20 MB — Telegram bot API limit.");
            return;
        }

        const char *name = cJSON_IsString(fname) ? fname->valuestring : "file";
        if (cJSON_IsString(fid)) {
            LOG_DEBUG("received document from user %lld: %s", (long long)user_id, name);
            handle_file(bot, cid, fid->valuestring, name);
        }
        return;
    }

    LOG_WARN("unsupported message type from user %lld", (long long)user_id);
    tg_send_message(bot, cid,
        "🤷 Unsupported message type. Please send an image or PDF.");
}

/* ── public API ───────────────────────────────────────────────────────────── */

TgBot *bot_new(const Config *cfg, Processor *processor, DBBackend *db)
{
    curl_global_init(CURL_GLOBAL_DEFAULT);

    TgBot *bot = calloc(1, sizeof(TgBot));
    if (!bot) return NULL;
    bot->cfg       = cfg;
    bot->processor = processor;
    bot->db        = db;
    bot->offset    = 0;
    atomic_store(&bot->running, 1);
    return bot;
}

void bot_free(TgBot *bot)
{
    if (bot) {
        free(bot);
        curl_global_cleanup();
    }
}

void bot_stop(TgBot *bot)
{
    atomic_store(&bot->running, 0);
}

void bot_start(TgBot *bot)
{
    char url[MAX_URL_LEN];
    LOG_INFO("starting long-poll loop (timeout=%ds)", POLL_TIMEOUT);

    while (atomic_load(&bot->running)) {
        /* Build getUpdates URL */
        snprintf(url, sizeof(url),
                 "%s%s/getUpdates?offset=%ld&timeout=%d&allowed_updates=[\"message\"]",
                 TG_API_BASE, bot->cfg->telegram_token,
                 bot->offset, POLL_TIMEOUT);

        char *body = http_get_poll(url, POLL_TIMEOUT);
        if (!body) {
            LOG_ERROR("getUpdates failed, retrying in 5s");
            sleep(5);
            continue;
        }

        cJSON *json = cJSON_Parse(body);
        free(body);

        if (!json) {
            LOG_ERROR("failed to parse getUpdates response");
            sleep(2);
            continue;
        }

        cJSON *ok = cJSON_GetObjectItem(json, "ok");
        if (!cJSON_IsTrue(ok)) {
            cJSON *desc = cJSON_GetObjectItem(json, "description");
            LOG_ERROR("getUpdates not ok: %s",
                    cJSON_IsString(desc) ? desc->valuestring : "?");
            cJSON_Delete(json);
            sleep(5);
            continue;
        }

        cJSON *result = cJSON_GetObjectItem(json, "result");
        if (cJSON_IsArray(result)) {
            int n = cJSON_GetArraySize(result);
            LOG_DEBUG("received %d updates", n);
            for (int i = 0; i < n; i++) {
                cJSON *update = cJSON_GetArrayItem(result, i);
                cJSON *uid    = cJSON_GetObjectItem(update, "update_id");
                if (cJSON_IsNumber(uid)) {
                    dispatch_update(bot, update);
                    bot->offset = (long)uid->valuedouble + 1;
                }
            }
        }

        cJSON_Delete(json);
    }

    LOG_INFO("polling loop stopped");
}
