#include "processor.h"
#include "cJSON.h"
#include "config.h"
#include "storage.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Processor {
    FileRepository *repo;
    StorageBackend *storage;
    OCRService *ocr;
    DuplicateStrategyFn dup_strategy;
};

/* ── built-in duplicate strategy ─────────────────────────────────────────── */

int strategy_notify_and_skip(const FileRecord *existing, char *reply_buf, size_t buf_len) {
    LOG_INFO("duplicate detected: %s", existing->saved_file_name);
    snprintf(reply_buf, buf_len,
             "\xF0\x9F\x94\x84 *Duplicate detected!*\n\n"
             "\xF0\x9F\x93\x84 File: `%s`\n"
             "\xF0\x9F\x95\x90 Uploaded: `%s`\n"
             "\xE2\x9C\x85 OCR: `%s`\n\n"
             "\xF0\x9F\x94\x8D This file was already uploaded.",
             existing->saved_file_name, existing->created_at,
             existing->is_ocr_processed ? "done" : "not processed");
    return 0;
}

/* ── constructor / destructor ─────────────────────────────────────────────── */

Processor *processor_new(FileRepository *repo, StorageBackend *storage, OCRService *ocr,
                         DuplicateStrategyFn dup_strategy) {
    if (!repo || !storage || !ocr) {
        LOG_ERROR("processor_new: required parameter is NULL");
        return NULL;
    }

    Processor *p = calloc(1, sizeof(Processor));
    if (!p) {
        LOG_ERROR("failed to allocate processor");
        return NULL;
    }
    p->repo = repo;
    p->storage = storage;
    p->ocr = ocr;
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
    (void)saved_name;
    (void)ocr_filename;
    (void)original_name;
    (void)data_len;
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
                                "  `%d.` %s: `%.2f EUR` x `%.2f` = `%.2f EUR`\n", i + 1, item_name,
                                item_price, item_amount, line_total);
            }
        }
    }

    /* Build reply */
    pos = 0;
    pos += snprintf(reply_buf + pos, buf_len - pos,
                    "\xE2\x9C\x85 *Receipt parsed!*\n\n"
                    "\xF0\x9F\x8F\xAA Store: *%s*\n\n"
                    "\xF0\x9F\x9B\x92 *Line items (%d):*\n%s\n"
                    "\xF0\x9F\x92\xB0 Calculated total: `%.2f EUR`\n"
                    "\xF0\x9F\x92\xB0 Parsed total: `%.2f EUR`",
                    name, item_count, items_text, calculated_total, parsed_total);

    /* Add warning if totals differ by more than 1 cent (with floating point tolerance) */
    double diff = calculated_total - parsed_total;
    if (diff < 0)
        diff = -diff;

    if (diff > 0.015) { /* > 1 cent with FP tolerance */
        snprintf(reply_buf + pos, buf_len - pos,
                 "\n\n\xE2\x9A\xA0\xEF\xB8\x8F Calculated total (`%.2f EUR`) differs "
                 "from parsed total (`%.2f EUR`)",
                 calculated_total, parsed_total);
    }
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

    int64_t new_id = 0;
    int rc = file_repo_insert(p->repo, original_name, hash, saved_name, &new_id);
    if (rc != FILE_REPO_OK) {
        LOG_ERROR("repository error while saving file record");
        snprintf(reply_buf, reply_buf_len, "Database error while saving file record.");
        return -1;
    }
    rec->id = new_id;
    return 0;
}

static char *processor_run_ocr(Processor *p, const char *original_name, const uint8_t *data,
                               size_t data_len, const char *saved_name, const FileRecord *rec,
                               char *reply_buf, size_t reply_buf_len) {
    LOG_INFO("sending file to OCR service for text extraction");
    char *ocr_text = NULL;
    int rc = ocr_extract_text(p->ocr, data, data_len, original_name, &ocr_text);

    if (rc != OCR_OK || !ocr_text) {
        LOG_ERROR("OCR extraction failed");
        snprintf(reply_buf, reply_buf_len,
                 "\xF0\x9F\x94\xB4 *OCR Failed*\n\n"
                 "\xF0\x9F\x93\x84 File: `%s`\n"
                 "\xF0\x9F\x93\x8F Original: `%s`\n"
                 "\xF0\x9F\x93\x8F Size: `%zu bytes`\n\n"
                 "\xF0\x9F\x94\x84 Use the button below to retry OCR.",
                 saved_name, original_name, data_len);
        return NULL;
    }

    char ocr_filename[MAX_FILENAME];
    storage_ocr_filename(saved_name, ocr_filename, sizeof(ocr_filename));

    if (storage_backend_save_text(p->storage, ocr_filename, ocr_text) != 0) {
        LOG_ERROR("failed to write OCR file %s", ocr_filename);
    }

    rc = file_repo_mark_ocr_complete(p->repo, rec->id, ocr_filename);
    if (rc != FILE_REPO_OK) {
        LOG_ERROR("repository error marking OCR done for file id=%lld", (long long)rec->id);
    }
    return ocr_text;
}

static char *processor_parse_receipt(Processor *p, int64_t file_id, const char *ocr_text,
                                     const char *saved_name, const char *ocr_filename,
                                     const char *original_name, size_t data_len, char *reply_buf,
                                     size_t reply_buf_len) {
    LOG_INFO("sending OCR text to OCR service for parsing");
    char *parsed_json = NULL;
    int rc = ocr_parse_receipt(p->ocr, ocr_text, &parsed_json);

    if (rc != OCR_OK || !parsed_json) {
        LOG_ERROR("OCR parsing failed");
        snprintf(reply_buf, reply_buf_len,
                 "\xE2\x9A\xA0\xEF\xB8\x8F *Parsing Failed*\n\n"
                 "\xF0\x9F\x93\x84 File: `%s`\n"
                 "\xF0\x9F\x93\x84 OCR saved: `%s`\n"
                 "\xF0\x9F\x93\x8F Original: `%s`\n"
                 "\xF0\x9F\x93\x8F Size: `%zu bytes`\n\n"
                 "OCR data is saved, parsing can be retried later.",
                 saved_name, ocr_filename, original_name, data_len);
        return NULL;
    }

    rc = file_repo_mark_parsing_complete(p->repo, file_id, parsed_json);
    if (rc != FILE_REPO_OK) {
        LOG_ERROR("repository error saving parsed receipt");
    }
    return parsed_json;
}

void processor_handle_file(Processor *p, const char *original_name, const uint8_t *data,
                           size_t data_len, char *reply_buf, size_t reply_buf_len,
                           int64_t *out_file_id) {
    if (out_file_id)
        *out_file_id = 0;
    if (!p || !original_name || !data || !reply_buf || reply_buf_len == 0) {
        LOG_ERROR("processor_handle_file: invalid parameters");
        return;
    }

    LOG_INFO("processing file: %s (%zu bytes)", original_name, data_len);

    char hash[SHA256_HEX_LEN];
    storage_sha256_hex(data, data_len, hash);

    FileRecord existing;
    int found = file_repo_find_by_hash(p->repo, hash, &existing);
    if (found == FILE_REPO_OK) {
        LOG_WARN("duplicate file detected");
        if (!p->dup_strategy(&existing, reply_buf, reply_buf_len))
            return;
    } else if (found != FILE_REPO_ERR_NOT_FOUND) {
        LOG_ERROR("repository error while checking for duplicates");
        snprintf(reply_buf, reply_buf_len, "Database error while checking for duplicates.");
        return;
    }

    FileRecord rec;
    if (processor_save_file(p, original_name, data, data_len, hash, &rec, reply_buf,
                            reply_buf_len) != 0)
        return;

    if (out_file_id)
        *out_file_id = rec.id;

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
                 "\xE2\x9A\xA0\xEF\xB8\x8F *Parse Warning*\n\n"
                 "\xF0\x9F\x93\x84 File: `%s`\n"
                 "\xF0\x9F\x93\x84 OCR saved: `%s`\n"
                 "\xF0\x9F\x93\x8F Original: `%s`\n"
                 "\xF0\x9F\x93\x8F Size: `%zu bytes`\n\n"
                 "Saved to database but JSON was invalid.\n\n"
                 "\xF0\x9F\x94\x91 ID: `%lld`",
                 rec.saved_file_name, ocr_filename, original_name, data_len, (long long)rec.id);
        free(ocr_text);
        free(parsed_json);
        return;
    }
    processor_build_reply_ok(reply_buf, reply_buf_len, rec.saved_file_name, ocr_filename,
                             original_name, data_len, json);
    cJSON_Delete(json);
    size_t len = strlen(reply_buf);
    if (len + 32 < reply_buf_len) {
        snprintf(reply_buf + len, reply_buf_len - len, "\n\n\xF0\x9F\x94\x91 ID: `%lld`",
                 (long long)rec.id);
    }
}

/* ── retry OCR ────────────────────────────────────────────────────────────── */

int processor_retry_ocr(Processor *p, int64_t file_id, char *reply_buf, size_t reply_buf_len) {
    if (!p || !reply_buf || reply_buf_len == 0)
        return -1;

    /* Look up file record */
    FileRecord rec;
    int rc = file_repo_find_by_id(p->repo, file_id, &rec);
    if (rc != FILE_REPO_OK) {
        snprintf(reply_buf, reply_buf_len, "\xF0\x9F\x94\xB4 File with ID `%lld` not found.",
                 (long long)file_id);
        return -1;
    }

    LOG_INFO("retry OCR for file id=%lld: %s", (long long)file_id, rec.saved_file_name);

    /* Read the stored OCR text */
    if (rec.ocr_file_name[0] == '\0') {
        snprintf(reply_buf, reply_buf_len,
                 "\xF0\x9F\x94\xB4 *Cannot Retry*\n\n"
                 "\xF0\x9F\x93\x84 File: `%s`\n"
                 "\xF0\x9F\x94\x91 ID: `%lld`\n\n"
                 "No OCR text available. Please re-upload the file.",
                 rec.original_file_name, (long long)file_id);
        return -1;
    }

    char *ocr_text = storage_backend_read_text(file_repo_get_storage(p->repo), rec.ocr_file_name);
    if (!ocr_text) {
        snprintf(reply_buf, reply_buf_len,
                 "\xF0\x9F\x94\xB4 *Cannot Retry*\n\n"
                 "\xF0\x9F\x93\x84 File: `%s`\n"
                 "\xF0\x9F\x94\x91 ID: `%lld`\n\n"
                 "OCR text file is missing. Please re-upload the file.",
                 rec.original_file_name, (long long)file_id);
        return -1;
    }

    /* Re-parse the receipt */
    char *parsed_json = NULL;
    rc = ocr_parse_receipt(p->ocr, ocr_text, &parsed_json);
    free(ocr_text);

    if (rc != OCR_OK || !parsed_json) {
        snprintf(reply_buf, reply_buf_len,
                 "\xE2\x9A\xA0\xEF\xB8\x8F *Retry: Parsing Failed*\n\n"
                 "\xF0\x9F\x93\x84 File: `%s`\n"
                 "\xF0\x9F\x94\x91 ID: `%lld`\n\n"
                 "Parsing failed again.",
                 rec.original_file_name, (long long)file_id);
        return -1;
    }

    rc = file_repo_mark_parsing_complete(p->repo, file_id, parsed_json);
    if (rc != FILE_REPO_OK) {
        LOG_ERROR("repository error saving parsed receipt");
    }

    cJSON *json = cJSON_Parse(parsed_json);
    if (!json) {
        snprintf(reply_buf, reply_buf_len,
                 "\xE2\x9A\xA0\xEF\xB8\x8F *Retry: Invalid JSON*\n\n"
                 "\xF0\x9F\x93\x84 File: `%s`\n"
                 "\xF0\x9F\x94\x91 ID: `%lld`",
                 rec.original_file_name, (long long)file_id);
        free(parsed_json);
        return 0;
    }

    processor_build_reply_ok(reply_buf, reply_buf_len, rec.saved_file_name, rec.ocr_file_name,
                             rec.original_file_name, (size_t)rec.file_size_bytes, json);
    cJSON_Delete(json);
    free(parsed_json);

    size_t len = strlen(reply_buf);
    if (len + 32 < reply_buf_len) {
        snprintf(reply_buf + len, reply_buf_len - len, "\n\n\xF0\x9F\x94\x91 ID: `%lld`",
                 (long long)rec.id);
    }
    return 0;
}
