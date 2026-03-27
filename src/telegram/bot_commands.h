#ifndef BOT_COMMANDS_H
#define BOT_COMMANDS_H

#include "bot.h"
#include "cJSON.h"

/* Process a Telegram update (message or callback_query).
 * Called from the polling loop. */
void bot_dispatch_update(TgBot *bot, cJSON *update);

#endif /* BOT_COMMANDS_H */
