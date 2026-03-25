#ifndef BOT_H
#define BOT_H

#include "config.h"
#include "db_backend.h"
#include "processor.h"

typedef struct TgBot TgBot;

/* Create the bot. Borrowed pointers – bot does not free them.
 * Returns NULL on error. */
TgBot *bot_new(const Config *cfg, Processor *processor, DBBackend *db);

/* Free the bot. */
void bot_free(TgBot *bot);

/* Start the long-polling loop. Blocks until bot_stop() is called. */
void bot_start(TgBot *bot);

/* Signal the polling loop to stop (safe to call from signal handler). */
void bot_stop(TgBot *bot);

/* Send startup notification to all allowed users. */
void bot_notify_startup(const TgBot *bot);

/* Send a message to a specific chat. Internal helper exposed for bot_notify_startup. */
void tg_send_message(const TgBot *bot, int64_t chat_id, const char *text);

#endif /* BOT_H */
