#include "bot_commands.h"

#include "bot.h"
#include "cJSON.h"
#include "config.h"
#include "db_backend.h"
#include "processor.h"
#include "storage_backend.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Access TgBot internals — bot.c exposes these fields via bot.h include */
/* We need the struct definition; bot.c will include bot_commands.h after defining TgBot.
 * To avoid circular dependency, bot_commands.h uses forward decl only.
 * We duplicate the struct access via an opaque pointer and accessor functions
 * provided by bot.c. However, for simplicity, we #include bot_internal.h. */

/* Instead, we access the struct via the full definition which bot.h already provides
 * indirectly through processor.h and db_backend.h. We declare the struct here
 * but bot.c owns the canonical definition. To break the circular dependency,
 * we re-declare just the fields we need as a local struct and cast.
 * Actually, the simplest approach: bot.h declares TgBot as opaque, and we add
 * accessor functions in bot.c. But that's over-engineered for now.
 *
 * Simplest correct approach: bot.h includes the struct definition (it already does
 * indirectly). We just use it. */

#include "bot.h" /* Pulls in processor.h → file_repository.h → storage_backend.h */

/* Constants used by command handling */
#define MAX_LIST_ENTRIES 10
#define MAX_REPLY_LEN    4096
#define TG_CMD_START     "/start"
#define TG_CMD_HELP      "/help"
#define TG_CMD_LIST      "/list"
#define TG_CMD_DELETE    "/delete"
#define TG_CMD_RETRY     "/retry"

/* ── helper types ─────────────────────────────────────────────────────────── */

typedef struct {
    char *buf;
    int pos;
    int cap;
    int count;
} ListCtx;

/* ── helper functions ─────────────────────────────────────────────────────── */

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

/* ── command handler ──────────────────────────────────────────────────────── */

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

/* ── file handler ─────────────────────────────────────────────────────────── */

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

/* ── dispatch helpers ─────────────────────────────────────────────────────── */

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

    if (cJSON_IsNumber(fsize) && fsize->valuedouble > (double)bot->cfg->max_file_size_bytes) {
        LOG_WARN("user %lld attempted to upload file exceeding limit", (long long)user_id);
        tg_send_message(bot, chat_id, "File exceeds 20 MB - Telegram bot API limit.");
        return;
    }

    const char *name = cJSON_IsString(fname) ? fname->valuestring : "file";
    if (cJSON_IsString(fid)) {
        LOG_DEBUG("received document from user %lld: %s", (long long)user_id, name);
        handle_file(bot, chat_id, fid->valuestring, name);
    }
}

/* ── main dispatcher ──────────────────────────────────────────────────────── */

void bot_dispatch_update(TgBot *bot, cJSON *update) {
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
