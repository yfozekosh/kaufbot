#ifndef BOT_H
#define BOT_H

#include "config.h"
#include "processor.h"
#include "db_backend.h"

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

#endif /* BOT_H */
