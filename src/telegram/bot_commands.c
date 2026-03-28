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

/* Constants */
#define MAX_LIST_ENTRIES 10
#define MAX_REPLY_LEN    4096
#define TG_CMD_START     "/start"
#define TG_CMD_HELP      "/help"
#define TG_CMD_LIST      "/list"
#define TG_CMD_DELETE    "/delete"
#define TG_CMD_RETRY     "/retry"
#define TG_CMD_RETRYWITH "/retrywith"
#define TG_CMD_MODELS    "/models"

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
        snprintf(c->buf + c->pos, (size_t)remaining, "#%lld - %s\n   %s | %lld bytes | %s\n\n",
                 (long long)rec->id, rec->saved_file_name, rec->original_file_name,
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

    if (bot->storage && rec.saved_file_name[0] != '\0')
        storage_backend_delete_file(bot->storage, rec.saved_file_name);
    if (bot->storage && rec.ocr_file_name[0] != '\0')
        storage_backend_delete_file(bot->storage, rec.ocr_file_name);

    int rc = db_backend_delete_file(bot->db, db_file_id);
    if (rc != 0) {
        snprintf(msg_buf, msg_len, "Failed to delete file %lld.", (long long)db_file_id);
        return -1;
    }

    snprintf(msg_buf, msg_len, "Deleted: %s (ID: %lld)", rec.original_file_name,
             (long long)db_file_id);
    return 0;
}

/* Build the help text with available models */
static void send_help(const TgBot *bot, int64_t chat_id) {
    tg_send_message(bot, chat_id,
                    "Kaufbot - Receipt Scanner\n\n"
                    "Send me a receipt photo or PDF and I will:\n"
                    "  1. Extract text via Gemini OCR\n"
                    "  2. Parse items, totals, and store name\n"
                    "  3. Save results for later\n\n"
                    "Commands:\n"
                    "  /help              - this message\n"
                    "  /list              - recent uploads\n"
                    "  /delete <id>       - delete a file\n"
                    "  /retry <id>        - re-parse OCR text\n"
                    "  /retrywith <model> <id> - re-run OCR with a model\n"
                    "  /models            - list available models");
}

/* Send list of available models */
static void send_models(const TgBot *bot, int64_t chat_id) {
    char reply[MAX_REPLY_LEN];
    int pos = 0;

    pos += snprintf(reply + pos, sizeof(reply) - pos, "Available Gemini models:\n\n");

    for (int i = 0; i < bot->cfg->gemini_model_count; i++) {
        int remaining = (int)sizeof(reply) - pos;
        if (remaining <= 0)
            break;
        pos += snprintf(reply + pos, (size_t)remaining, "  %d. `%s`\n", i + 1,
                        bot->cfg->gemini_models[i]);
    }

    snprintf(reply + pos, sizeof(reply) - pos,
             "\nUse: /retrywith <model> <id>\n"
             "Example: /retrywith %s <id>",
             bot->cfg->gemini_model_count > 0 ? bot->cfg->gemini_models[0] : "gemini-2.5-flash");

    tg_send_message(bot, chat_id, reply);
}

/* ── command handler ──────────────────────────────────────────────────────── */

static void handle_command(TgBot *bot, int64_t chat_id, const char *text) {
    if (str_starts_with(text, TG_CMD_START) || str_starts_with(text, TG_CMD_HELP)) {
        send_help(bot, chat_id);
        return;
    }

    if (str_starts_with(text, TG_CMD_MODELS)) {
        send_models(bot, chat_id);
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

    /* /retrywith <model> <id> */
    if (str_starts_with(text, TG_CMD_RETRYWITH)) {
        const char *arg = text + strlen(TG_CMD_RETRYWITH);
        while (*arg == ' ')
            arg++;

        if (*arg == '\0') {
            tg_send_message(bot, chat_id,
                            "Usage: /retrywith <model> <id>\n"
                            "Use /models to list available models.");
            return;
        }

        /* Parse model name (everything until space) */
        char model[GEMINI_MAX_MODEL_LEN];
        const char *model_end = strchr(arg, ' ');
        if (!model_end || model_end == arg) {
            tg_send_message(bot, chat_id,
                            "Usage: /retrywith <model> <id>\n"
                            "Example: /retrywith gemini-2.5-flash 42");
            return;
        }
        size_t model_len = (size_t)(model_end - arg);
        if (model_len >= sizeof(model))
            model_len = sizeof(model) - 1;
        memcpy(model, arg, model_len);
        model[model_len] = '\0';

        /* Parse ID */
        const char *id_str = model_end;
        while (*id_str == ' ')
            id_str++;

        char *end;
        long long id = strtoll(id_str, &end, 10);
        if (end == id_str || *end != '\0') {
            tg_send_message(bot, chat_id, "Invalid ID. Usage: /retrywith <model> <id>");
            return;
        }

        char reply[MAX_REPLY_LEN];
        int rc = processor_retry_ocr_with_model(bot->processor, (int64_t)id, model, reply,
                                                sizeof(reply));
        if (rc == 0) {
            tg_send_message_with_keyboard(bot, chat_id, reply, (int64_t)id);
        } else {
            tg_send_message(bot, chat_id, reply);
        }
        return;
    }

    /* /retry <id> */
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
        } else if (strncmp(data, "rwm:", 4) == 0) {
            /* retry with model: "rwm:<model>:<file_id>" */
            const char *rest = data + 4;
            const char *colon = strchr(rest, ':');
            if (colon) {
                char model[GEMINI_MAX_MODEL_LEN];
                size_t mlen = (size_t)(colon - rest);
                if (mlen >= sizeof(model))
                    mlen = sizeof(model) - 1;
                memcpy(model, rest, mlen);
                model[mlen] = '\0';

                long long file_id = strtoll(colon + 1, NULL, 10);

                cJSON *chat = cJSON_GetObjectItem(cb_message, "chat");
                cJSON *chat_id_obj = chat ? cJSON_GetObjectItem(chat, "id") : NULL;
                int64_t retry_chat_id =
                    cJSON_IsNumber(chat_id_obj) ? (int64_t)chat_id_obj->valuedouble : 0;

                tg_answer_callback_query(bot, cb_id->valuestring, "Retrying...");

                if (retry_chat_id != 0) {
                    char reply[MAX_REPLY_LEN];
                    int rc = processor_retry_ocr_with_model(bot->processor, (int64_t)file_id, model,
                                                            reply, sizeof(reply));
                    if (rc == 0) {
                        tg_send_message_with_keyboard(bot, retry_chat_id, reply, (int64_t)file_id);
                    } else {
                        tg_send_message(bot, retry_chat_id, reply);
                    }
                }
            } else {
                tg_answer_callback_query(bot, cb_id->valuestring, NULL);
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

    /* Photo */
    cJSON *photos = cJSON_GetObjectItem(message, "photo");
    if (cJSON_IsArray(photos)) {
        dispatch_photo(bot, chat_id, user_id, photos);
        return;
    }

    /* Document */
    cJSON *doc = cJSON_GetObjectItem(message, "document");
    if (doc) {
        dispatch_document(bot, chat_id, user_id, doc);
        return;
    }

    LOG_WARN("unsupported message type from user %lld", (long long)user_id);
    tg_send_message(bot, chat_id, "Unsupported message type. Please send an image or PDF.");
}
