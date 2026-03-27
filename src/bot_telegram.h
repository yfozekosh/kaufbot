#ifndef BOT_TELEGRAM_H
#define BOT_TELEGRAM_H

#include <stddef.h>
#include <stdint.h>

/* ── Message Sender Interface ─────────────────────────────────────────────── */

typedef struct MessageSender MessageSender;

typedef struct {
    /* Send a plain text message to a chat */
    void (*send_message)(void *ctx, int64_t chat_id, const char *text);

    /* Send a message with inline keyboard (retry/delete buttons) */
    void (*send_with_keyboard)(void *ctx, int64_t chat_id, const char *text, int64_t file_id);

    /* Answer a callback query (inline keyboard button press) */
    void (*answer_callback)(void *ctx, const char *query_id, const char *text);

    /* Delete a message from a chat */
    void (*delete_message)(void *ctx, int64_t chat_id, int64_t message_id);
} MessageSenderOps;

struct MessageSender {
    const MessageSenderOps *ops;
    void *internal;
};

/* ── Convenience Wrappers ─────────────────────────────────────────────────── */

static inline void msg_sender_send(MessageSender *sender, int64_t chat_id, const char *text) {
    if (sender && sender->ops && sender->ops->send_message) {
        sender->ops->send_message(sender->internal, chat_id, text);
    }
}

static inline void msg_sender_send_keyboard(MessageSender *sender, int64_t chat_id,
                                            const char *text, int64_t file_id) {
    if (sender && sender->ops && sender->ops->send_with_keyboard) {
        sender->ops->send_with_keyboard(sender->internal, chat_id, text, file_id);
    }
}

static inline void msg_sender_answer_callback(MessageSender *sender, const char *query_id,
                                              const char *text) {
    if (sender && sender->ops && sender->ops->answer_callback) {
        sender->ops->answer_callback(sender->internal, query_id, text);
    }
}

static inline void msg_sender_delete_message(MessageSender *sender, int64_t chat_id,
                                             int64_t message_id) {
    if (sender && sender->ops && sender->ops->delete_message) {
        sender->ops->delete_message(sender->internal, chat_id, message_id);
    }
}

/* ── Telegram Implementation ──────────────────────────────────────────────── */

typedef struct TgBot TgBot;

/* Create a MessageSender backed by Telegram bot */
MessageSender *message_sender_telegram_new(TgBot *bot);

/* Free the message sender (does not free the underlying bot) */
void message_sender_free(MessageSender *sender);

#endif /* BOT_TELEGRAM_H */
