#include "processor.h"
#include "cJSON.h"
#include "config.h"
#include "gemini.h"
#include "storage.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Processor {
    DBBackend *db;
    StorageBackend *storage;
    GeminiClient *gemini;
    DuplicateStrategyFn dup_strategy;
};

/* ── built-in duplicate strategy ─────────────────────────────────────────── */

int strategy_notify_and_skip(const FileRecord *existing, char *reply_buf, size_t buf_len) {
    LOG_INFO("duplicate detected: %s", existing->saved_file_name);
    snprintf(reply_buf, buf_len,
             "Duplicate detected - this file was already uploaded.\n"
             "Saved as: %s\n"
             "Uploaded: %s\n"
             "OCR: %s",
             existing->saved_file_name, existing->created_at,
             existing->is_ocr_processed ? "done" : "not processed");
    return 0;
}

/* ── constructor / destructor ─────────────────────────────────────────────── */

Processor *processor_new(DBBackend *db, StorageBackend *storage, void *gemini,
                         DuplicateStrategyFn dup_strategy) {
    if (!db || !storage || !gemini) {
        LOG_ERROR("processor_new: required parameter is NULL");
        return NULL;
    }

    Processor *p = calloc(1, sizeof(Processor));
    if (!p) {
        LOG_ERROR("failed to allocate processor");
        return NULL;
    }
    p->db = db;
    p->storage = storage;
    p->gemini = (GeminiClient *)gemini;
    p->dup_strategy = dup_strategy ? dup_strategy : strategy_notify_and_skip;
    return p;
}

void processor_free(Processor *p) {
    free(p);
}

/* ── pipeline sub-steps ──────────────────────────────────────────────────── */

void processor_build_reply_ok(char *reply_buf, size_t buf_len, const char *saved_name,
                              const char *ocr_filename, const char *original_name, size_t data_len,
                              cJSON *json) {
    cJSON *store_info = cJSON_GetObjectItem(json, "store_information");
    cJSON *store_name = store_info ? cJSON_GetObjectItem(store_info, "name") : NULL;
    cJSON *total_sum = cJSON_GetObjectItem(json, "total_sum");
    cJSON *line_items = cJSON_GetObjectItem(json, "line_items");

    const char *name = (store_name && cJSON_IsString(store_name) && store_name->valuestring)
                           ? store_name->valuestring
                           : "Unknown";
    double parsed_total = (total_sum && cJSON_IsNumber(total_sum)) ? total_sum->valuedouble : 0;
    int item_count = (line_items && cJSON_IsArray(line_items)) ? cJSON_GetArraySize(line_items) : 0;

    /* Calculate total from line items */
    double calculated_total = 0.0;
    char items_text[2048] = {0};
    int pos = 0;

    if (line_items && cJSON_IsArray(line_items)) {
        for (int i = 0; i < item_count; i++) {
            cJSON *item = cJSON_GetArrayItem(line_items, i);
            if (!item)
                continue;

            cJSON *orig_name = cJSON_GetObjectItem(item, "original_name");
            cJSON *price = cJSON_GetObjectItem(item, "price");
            cJSON *amount = cJSON_GetObjectItem(item, "amount");

            const char *item_name =
                (orig_name && cJSON_IsString(orig_name) && orig_name->valuestring)
                    ? orig_name->valuestring
                    : "Unknown";
            double item_price = (price && cJSON_IsNumber(price)) ? price->valuedouble : 0.0;
            double item_amount = (amount && cJSON_IsNumber(amount)) ? amount->valuedouble : 1.0;
            double line_total = item_price * item_amount;
            calculated_total += line_total;

            int remaining = (int)sizeof(items_text) - pos;
            if (remaining > 0) {
                pos += snprintf(items_text + pos, (size_t)remaining,
                                "  %d. %s: %.2f EUR x %.2f = %.2f EUR\n", i + 1, item_name,
                                item_price, item_amount, line_total);
            }
        }
    }

    /* Build reply */
    pos = 0;
    pos += snprintf(reply_buf + pos, buf_len - pos,
                    "File saved: %s\n"
                    "OCR result saved: %s\n"
                    "Receipt parsed successfully!\n\n"
                    "Store: %s\n\n"
                    "Line items:\n%s\n"
                    "Calculated total: %.2f EUR\n"
                    "Parsed total: %.2f EUR\n\n"
                    "Full parsed data saved to database.",
                    saved_name, ocr_filename, name, items_text, calculated_total, parsed_total);

    /* Add warning if totals differ by more than 1 cent (with floating point tolerance) */
    double diff = calculated_total - parsed_total;
    if (diff < 0)
        diff = -diff;

    if (diff > 0.015) { /* > 1 cent with FP tolerance */
        snprintf(reply_buf + pos, buf_len - pos,
                 "\n⚠️ Warning: Calculated total (%.2f EUR) differs from parsed total (%.2f EUR)",
                 calculated_total, parsed_total);
    }

    (void)original_name;
    (void)data_len;
}

/* ── pipeline ─────────────────────────────────────────────────────────────── */

static int processor_save_file(Processor *p, const char *original_name, const uint8_t *data,
                               size_t data_len, const char *hash, FileRecord *rec, char *reply_buf,
                               size_t reply_buf_len) {
    const char *ext = strrchr(original_name, '.');
    char saved_name[MAX_FILENAME];
    storage_gen_filename(ext ? ext : "", saved_name, sizeof(saved_name));

    if (storage_backend_save_file(p->storage, saved_name, data, data_len) != 0) {
        LOG_ERROR("failed to save file to storage");
        snprintf(reply_buf, reply_buf_len, "Failed to save file to storage.");
        return -1;
    }

    memset(rec, 0, sizeof(*rec));
    snprintf(rec->original_file_name, DB_ORIG_NAME_LEN, "%s", original_name);
    rec->file_size_bytes = (int64_t)data_len;
    snprintf(rec->saved_file_name, DB_FILENAME_LEN, "%s", saved_name);
    snprintf(rec->file_hash, DB_HASH_LEN, "%s", hash);
    rec->is_ocr_processed = 0;

    if (db_backend_insert(p->db, rec) != 0) {
        LOG_ERROR("database error while saving file record");
        snprintf(reply_buf, reply_buf_len, "Database error while saving file record.");
        return -1;
    }
    return 0;
}

static char *processor_run_ocr(Processor *p, const char *original_name, const uint8_t *data,
                               size_t data_len, const char *saved_name, const FileRecord *rec,
                               char *reply_buf, size_t reply_buf_len) {
    LOG_INFO("sending file to Gemini for OCR");
    char *ocr_text = gemini_extract_text(p->gemini, data, data_len, original_name);

    if (!ocr_text) {
        LOG_ERROR("OCR extraction failed");
        snprintf(reply_buf, reply_buf_len,
                 "File saved as: %s\n"
                 "OCR failed - file is saved, OCR can be retried later.\n"
                 "Original: %s  |  Size: %zu bytes",
                 saved_name, original_name, data_len);
        return NULL;
    }

    char ocr_filename[MAX_FILENAME];
    storage_ocr_filename(saved_name, ocr_filename, sizeof(ocr_filename));

    if (storage_backend_save_text(p->storage, ocr_filename, ocr_text) != 0) {
        LOG_ERROR("failed to write OCR file %s", ocr_filename);
    }

    if (db_backend_mark_ocr_done(p->db, rec->id, ocr_filename) != 0) {
        LOG_ERROR("failed to mark OCR done in DB for file id=%lld", (long long)rec->id);
    }
    return ocr_text;
}

static char *processor_parse_receipt(Processor *p, int64_t file_id, const char *ocr_text,
                                     const char *saved_name, const char *ocr_filename,
                                     const char *original_name, size_t data_len, char *reply_buf,
                                     size_t reply_buf_len) {
    LOG_INFO("sending OCR text to Gemini for parsing");
    char *parsed_json = gemini_parse_receipt(p->gemini, ocr_text);

    if (!parsed_json) {
        LOG_ERROR("gemini_parse_receipt returned NULL");
        snprintf(reply_buf, reply_buf_len,
                 "File saved: %s\n"
                 "OCR result saved: %s\n"
                 "Parsing failed - OCR data is saved, parsing can be retried later.\n"
                 "Original: %s  |  Size: %zu bytes",
                 saved_name, ocr_filename, original_name, data_len);
        return NULL;
    }

    if (db_backend_mark_parsing_done(p->db, file_id, parsed_json) != 0) {
        LOG_ERROR("failed to save parsed receipt");
    }
    return parsed_json;
}

void processor_handle_file(Processor *p, const char *original_name, const uint8_t *data,
                           size_t data_len, char *reply_buf, size_t reply_buf_len) {
    if (!p || !original_name || !data || !reply_buf || reply_buf_len == 0) {
        LOG_ERROR("processor_handle_file: invalid parameters");
        return;
    }

    LOG_INFO("processing file: %s (%zu bytes)", original_name, data_len);

    char hash[SHA256_HEX_LEN];
    storage_sha256_hex(data, data_len, hash);

    FileRecord existing;
    int found = db_backend_find_by_hash(p->db, hash, &existing);
    if (found == 0) {
        LOG_WARN("duplicate file detected");
        if (!p->dup_strategy(&existing, reply_buf, reply_buf_len))
            return;
    } else if (found == -1) {
        LOG_ERROR("database error while checking for duplicates");
        snprintf(reply_buf, reply_buf_len, "Database error while checking for duplicates.");
        return;
    }

    FileRecord rec;
    if (processor_save_file(p, original_name, data, data_len, hash, &rec, reply_buf,
                            reply_buf_len) != 0)
        return;

    char *ocr_text = processor_run_ocr(p, original_name, data, data_len, rec.saved_file_name, &rec,
                                       reply_buf, reply_buf_len);
    if (!ocr_text)
        return;

    char ocr_filename[MAX_FILENAME];
    storage_ocr_filename(rec.saved_file_name, ocr_filename, sizeof(ocr_filename));

    char *parsed_json =
        processor_parse_receipt(p, rec.id, ocr_text, rec.saved_file_name, ocr_filename,
                                original_name, data_len, reply_buf, reply_buf_len);
    if (!parsed_json) {
        free(ocr_text);
        return;
    }

    cJSON *json = cJSON_Parse(parsed_json);
    if (!json) {
        LOG_ERROR("cJSON_Parse failed - invalid JSON: %.200s", parsed_json);
        snprintf(reply_buf, reply_buf_len,
                 "File saved: %s\n"
                 "OCR result saved: %s\n"
                 "Receipt parsed and saved to database.\n"
                 "Original: %s  |  Size: %zu bytes",
                 rec.saved_file_name, ocr_filename, original_name, data_len);
    } else {
        processor_build_reply_ok(reply_buf, reply_buf_len, rec.saved_file_name, ocr_filename,
                                 original_name, data_len, json);
        cJSON_Delete(json);
    }

    free(ocr_text);
    free(parsed_json);
}
