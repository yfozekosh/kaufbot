#include "reply_builder.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int diff_is_significant(double a, double b) {
    double diff = a - b;
    if (diff < 0)
        diff = -diff;
    return diff > 0.015;
}

ReplyMessage *reply_builder_format_receipt(const ReceiptData *data) {
    if (!data)
        return NULL;

    ReplyMessage *msg = calloc(1, sizeof(ReplyMessage));
    if (!msg)
        return NULL;

    msg->file_id = data->file_id;

    char *buf = msg->text;
    size_t len = sizeof(msg->text);
    int pos = 0;

    /* Not a receipt warning */
    if (!data->is_receipt) {
        pos += snprintf(buf + pos, len - pos, "\xE2\x9D\x8C *Not a receipt*\n\n");
    }

    /* Store name */
    pos += snprintf(buf + pos, len - pos, "\xF0\x9F\x8F\xAA *%s*\n",
                    data->store_name[0] ? data->store_name : "Unknown");

    /* Quality */
    if (data->image_quality >= 0) {
        int q = data->image_quality;
        const char *q_emoji = q >= 80   ? "\xF0\x9F\x9F\xA2"
                              : q >= 50 ? "\xF0\x9F\x9F\xA1"
                                        : "\xF0\x9F\x94\xB4";
        pos += snprintf(buf + pos, len - pos, "%s Quality: `%d%%`\n", q_emoji, q);
    }

    /* Items count */
    if (data->reported_item_count >= 0) {
        pos += snprintf(buf + pos, len - pos, "\xF0\x9F\x93\x8B Items: `%d` (receipt says `%d`)\n",
                        data->item_count, data->reported_item_count);
    } else {
        pos += snprintf(buf + pos, len - pos, "\xF0\x9F\x93\x8B Items: `%d`\n", data->item_count);
    }

    /* Line items */
    pos += snprintf(buf + pos, len - pos, "\n");

    double calculated_total = 0.0;
    for (int i = 0; i < data->item_count; i++) {
        const ReplyLineItem *item = &data->items[i];
        double lt = item->price * item->amount - item->discount;
        calculated_total += lt;

        int rem = (int)len - pos;
        if (rem <= 0)
            break;

        const char *display_name =
            item->english_translation[0] ? item->english_translation : item->original_name;

        /* Item header with category tag */
        if (item->category[0]) {
            pos += snprintf(buf + pos, (size_t)rem, "  `%d.` %s _%s_\n", i + 1, display_name,
                            item->category);
        } else {
            pos += snprintf(buf + pos, (size_t)rem, "  `%d.` %s\n", i + 1, display_name);
        }

        /* Price line */
        rem = (int)len - pos;
        if (rem <= 0)
            break;

        const char *uom = item->unit_of_measure[0] ? item->unit_of_measure : "pcs";

        if (item->discount > 0.005 && item->amount > 1.005) {
            pos +=
                snprintf(buf + pos, (size_t)rem, "      `%.2f` x `%.2f %s` - `%.2f` = `%.2f EUR`\n",
                         item->price, item->amount, uom, item->discount, lt);
        } else if (item->discount > 0.005) {
            pos += snprintf(buf + pos, (size_t)rem, "      `%.2f EUR` - `%.2f` = `%.2f EUR`\n",
                            item->price, item->discount, lt);
        } else if (item->amount > 1.005) {
            pos += snprintf(buf + pos, (size_t)rem, "      `%.2f` x `%.2f %s` = `%.2f EUR`\n",
                            item->price, item->amount, uom, lt);
        } else {
            pos += snprintf(buf + pos, (size_t)rem, "      `%.2f EUR`\n", lt);
        }
    }

    /* Totals */
    pos +=
        snprintf(buf + pos, len - pos, "\n\xF0\x9F\x92\xB0 *Total:* `%.2f EUR`", data->total_sum);

    if (diff_is_significant(calculated_total, data->total_sum)) {
        pos +=
            snprintf(buf + pos, len - pos,
                     "\n\xE2\x9A\xA0\xEF\xB8\x8F Calculated `%.2f EUR` \xe2\x80\x94 check totals",
                     calculated_total);
    }

    /* Tokens */
    if (data->tokens_used > 0) {
        pos += snprintf(buf + pos, len - pos, "\n\xE2\x9A\xA1 Tokens: `%d`", data->tokens_used);
    }

    /* Download links */
    if (data->original_url[0] || data->ocr_url[0] || data->json_url[0]) {
        pos += snprintf(buf + pos, len - pos, "\n");
        if (data->original_url[0]) {
            pos += snprintf(buf + pos, len - pos, "\n\xF0\x9F\x93\x84 [Original](%s)",
                            data->original_url);
        }
        if (data->ocr_url[0]) {
            pos +=
                snprintf(buf + pos, len - pos, "\n\xF0\x9F\x93\x84 [OCR text](%s)", data->ocr_url);
        }
        if (data->json_url[0]) {
            pos += snprintf(buf + pos, len - pos, "\n\xF0\x9F\x93\x84 [JSON](%s)", data->json_url);
        }
    }

    return msg;
}

void reply_message_free(ReplyMessage *msg) {
    free(msg);
}
