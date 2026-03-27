#include "bot_telegram.h"
#include "bot.h"

#include <stdlib.h>

/* ── Internal Structure ───────────────────────────────────────────────────── */

typedef struct {
    TgBot *bot;
} TelegramMessageSender;

/* ── Implementation ───────────────────────────────────────────────────────── */

static void tg_sender_send_message(void *ctx, int64_t chat_id, const char *text) {
    TelegramMessageSender *sender = (TelegramMessageSender *)ctx;
    if (sender && sender->bot) {
        tg_send_message(sender->bot, chat_id, text);
    }
}

static void tg_sender_send_with_keyboard(void *ctx, int64_t chat_id, const char *text,
                                         int64_t file_id) {
    TelegramMessageSender *sender = (TelegramMessageSender *)ctx;
    if (sender && sender->bot) {
        tg_send_message_with_keyboard(sender->bot, chat_id, text, file_id);
    }
}

static void tg_sender_answer_callback(void *ctx, const char *query_id, const char *text) {
    TelegramMessageSender *sender = (TelegramMessageSender *)ctx;
    if (sender && sender->bot) {
        tg_answer_callback_query(sender->bot, query_id, text);
    }
}

static void tg_sender_delete_message(void *ctx, int64_t chat_id, int64_t message_id) {
    TelegramMessageSender *sender = (TelegramMessageSender *)ctx;
    if (sender && sender->bot) {
        tg_delete_message(sender->bot, chat_id, message_id);
    }
}

/* ── VTable ───────────────────────────────────────────────────────────────── */

static const MessageSenderOps tg_sender_ops = {
    .send_message = tg_sender_send_message,
    .send_with_keyboard = tg_sender_send_with_keyboard,
    .answer_callback = tg_sender_answer_callback,
    .delete_message = tg_sender_delete_message,
};

/* ── Public API ───────────────────────────────────────────────────────────── */

MessageSender *message_sender_telegram_new(TgBot *bot) {
    if (!bot)
        return NULL;

    TelegramMessageSender *impl = calloc(1, sizeof(TelegramMessageSender));
    if (!impl)
        return NULL;

    impl->bot = bot;

    MessageSender *sender = calloc(1, sizeof(MessageSender));
    if (!sender) {
        free(impl);
        return NULL;
    }

    sender->ops = &tg_sender_ops;
    sender->internal = impl;

    return sender;
}

void message_sender_free(MessageSender *sender) {
    if (!sender)
        return;
    free(sender->internal);
    free(sender);
}
