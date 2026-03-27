#ifndef BOT_H
#define BOT_H

#include "config.h"
#include "db_backend.h"
#include "prompt_fetcher.h"
#include "storage_backend.h"

#include <stdatomic.h>
#include <stdint.h>

/* ── TgBot structure ──────────────────────────────────────────────────────── */

typedef struct TgBot {
    const Config *cfg;
    struct Processor *processor;
    DBBackend *db;
    StorageBackend *storage;
    PromptFetcher *prompt_fetcher;
    atomic_int running;
    long offset;
} TgBot;

/* ── Bot lifecycle ────────────────────────────────────────────────────────── */

TgBot *bot_new(const Config *cfg, struct Processor *processor, DBBackend *db,
               StorageBackend *storage);
void bot_free(TgBot *bot);
void bot_start(TgBot *bot);
void bot_stop(TgBot *bot);

/* ── Notifications ────────────────────────────────────────────────────────── */

void bot_notify_startup(const TgBot *bot);
void bot_notify_prompt_change(const TgBot *bot, const char *prompt_name, const char *new_content);

/* ── Telegram HTTP protocol ───────────────────────────────────────────────── */

void tg_send_message(const TgBot *bot, int64_t chat_id, const char *text);
void tg_send_message_with_keyboard(const TgBot *bot, int64_t chat_id, const char *text,
                                   int64_t db_file_id);
void tg_answer_callback_query(const TgBot *bot, const char *query_id, const char *text);
void tg_delete_message(const TgBot *bot, int64_t chat_id, int64_t message_id);

/* ── File operations (used by bot_commands) ───────────────────────────────── */

char *tg_get_file_path(const TgBot *bot, const char *file_id);
uint8_t *tg_download_file(const TgBot *bot, const char *file_path, size_t *out_len);

#endif /* BOT_H */
