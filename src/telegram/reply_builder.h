#ifndef REPLY_BUILDER_H
#define REPLY_BUILDER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ── Input: parsed receipt data ───────────────────────────────────────────── */

#define MAX_REPLY_ITEMS    30
#define MAX_ITEM_NAME_LEN  128
#define MAX_STORE_NAME_LEN 128
#define MAX_DOWNLOAD_URL   512

typedef struct {
    char original_name[MAX_ITEM_NAME_LEN];
    char english_translation[MAX_ITEM_NAME_LEN];
    char unit_of_measure[32];
    char category[64];
    double price;
    double amount;
    double discount;
} ReplyLineItem;

typedef struct {
    char store_name[MAX_STORE_NAME_LEN];
    bool is_receipt;
    int image_quality;       /* 0-100 */
    int reported_item_count; /* -1 if not reported */
    double total_sum;

    ReplyLineItem items[MAX_REPLY_ITEMS];
    int item_count;

    int64_t file_id;
    int tokens_used;

    /* Download links (NULL if not available) */
    char original_url[MAX_DOWNLOAD_URL];
    char ocr_url[MAX_DOWNLOAD_URL];
    char json_url[MAX_DOWNLOAD_URL];
} ReceiptData;

/* ── Output: formatted message for Telegram ───────────────────────────────── */

#define MAX_REPLY_TEXT_LEN 4096

typedef struct {
    char text[MAX_REPLY_TEXT_LEN];
    int64_t file_id; /* for inline keyboard attachment */
} ReplyMessage;

/* ── Builder API ──────────────────────────────────────────────────────────── */

/* Format receipt data into a Telegram-ready reply message.
 * Caller must call reply_message_free() when done. */
ReplyMessage *reply_builder_format_receipt(const ReceiptData *data);

/* Free a reply message. Safe to call with NULL. */
void reply_message_free(ReplyMessage *msg);

#endif /* REPLY_BUILDER_H */
