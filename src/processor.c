#include "processor.h"
#include "cJSON.h"
#include "config.h"
#include "reply_builder.h"
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
             "Duplicate detected\n\n"
             "File: %s\n"
             "Uploaded: %s\n"
             "OCR: %s\n"
             "ID: %lld\n\n"
             "This file was already uploaded.",
             existing->saved_file_name, existing->created_at,
             existing->is_ocr_processed ? "done" : "pending", (long long)existing->id);
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

static void fill_receipt_data(ReceiptData *data, cJSON *json, int64_t file_id, int tokens) {
    memset(data, 0, sizeof(ReceiptData));
    data->file_id = file_id;
    data->tokens_used = tokens;
    data->image_quality = -1;
    data->reported_item_count = -1;
    data->is_receipt = true;

    cJSON *store_info = cJSON_GetObjectItem(json, "store_information");
    cJSON *store_name = store_info ? cJSON_GetObjectItem(store_info, "name") : NULL;
    if (store_name && cJSON_IsString(store_name) && store_name->valuestring) {
        snprintf(data->store_name, sizeof(data->store_name), "%s", store_name->valuestring);
    }

    cJSON *total = cJSON_GetObjectItem(json, "total_sum");
    if (total && cJSON_IsNumber(total))
        data->total_sum = total->valuedouble;

    cJSON *is_receipt = cJSON_GetObjectItem(json, "is_receipt");
    if (is_receipt && cJSON_IsFalse(is_receipt))
        data->is_receipt = false;

    cJSON *quality = cJSON_GetObjectItem(json, "image_quality");
    if (quality && cJSON_IsNumber(quality))
        data->image_quality = quality->valueint;

    cJSON *reported = cJSON_GetObjectItem(json, "reported_item_count");
    if (reported && cJSON_IsNumber(reported))
        data->reported_item_count = reported->valueint;

    cJSON *items = cJSON_GetObjectItem(json, "line_items");
    if (items && cJSON_IsArray(items)) {
        int n = cJSON_GetArraySize(items);
        if (n > MAX_REPLY_ITEMS)
            n = MAX_REPLY_ITEMS;
        data->item_count = n;
        for (int i = 0; i < n; i++) {
            cJSON *item = cJSON_GetArrayItem(items, i);
            if (!item)
                continue;
            ReplyLineItem *li = &data->items[i];
            cJSON *name = cJSON_GetObjectItem(item, "original_name");
            if (name && cJSON_IsString(name))
                snprintf(li->original_name, sizeof(li->original_name), "%s", name->valuestring);
            cJSON *price = cJSON_GetObjectItem(item, "price");
            if (price && cJSON_IsNumber(price))
                li->price = price->valuedouble;
            cJSON *amount = cJSON_GetObjectItem(item, "amount");
            if (amount && cJSON_IsNumber(amount))
                li->amount = amount->valuedouble;
            else
                li->amount = 1.0;
            cJSON *discount = cJSON_GetObjectItem(item, "discount");
            if (discount && cJSON_IsNumber(discount))
                li->discount = discount->valuedouble;
        }
    }
}

ReplyMessage *processor_build_reply(cJSON *json, int64_t file_id, int tokens) {
    ReceiptData data;
    fill_receipt_data(&data, json, file_id, tokens);
    return reply_builder_format_receipt(&data);
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
    ReplyMessage *msg = processor_build_reply(json, rec.id, ocr_get_last_tokens(p->ocr));
    if (msg) {
        snprintf(reply_buf, reply_buf_len, "%s", msg->text);
        reply_message_free(msg);
    }
    cJSON_Delete(json);
    free(ocr_text);
    free(parsed_json);
}

int processor_retry_ocr(Processor *p, int64_t file_id, char *reply_buf, size_t reply_buf_len) {
    if (!p || !reply_buf || reply_buf_len == 0)
        return -1;

    /* Look up file record */
    FileRecord rec;
    int rc = file_repo_find_by_id(p->repo, file_id, &rec);
    if (rc != FILE_REPO_OK) {
        snprintf(reply_buf, reply_buf_len, "File with ID %lld not found.", (long long)file_id);
        return -1;
    }

    LOG_INFO("retry OCR for file id=%lld: %s", (long long)file_id, rec.saved_file_name);

    /* Read the stored OCR text */
    if (rec.ocr_file_name[0] == '\0') {
        snprintf(reply_buf, reply_buf_len,
                 "Cannot Retry\n\n"
                 "File: %s\n"
                 "ID: %lld\n\n"
                 "No OCR text available. Please re-upload the file.",
                 rec.original_file_name, (long long)file_id);
        return -1;
    }

    char *ocr_text = storage_backend_read_text(file_repo_get_storage(p->repo), rec.ocr_file_name);
    if (!ocr_text) {
        snprintf(reply_buf, reply_buf_len,
                 "Cannot Retry\n\n"
                 "File: %s\n"
                 "ID: %lld\n\n"
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
                 "Retry failed\n\n"
                 "File: %s\n"
                 "ID: %lld\n\n"
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
                 "Retry: Invalid JSON\n\n"
                 "File: %s\n"
                 "ID: %lld",
                 rec.original_file_name, (long long)file_id);
        free(parsed_json);
        return 0;
    }

    ReplyMessage *reply_msg = processor_build_reply(json, rec.id, ocr_get_last_tokens(p->ocr));
    if (reply_msg) {
        snprintf(reply_buf, reply_buf_len, "%s", reply_msg->text);
        reply_message_free(reply_msg);
    }
    cJSON_Delete(json);
    free(parsed_json);
    return 0;
}

int processor_retry_ocr_with_model(Processor *p, int64_t file_id, const char *model,
                                   char *reply_buf, size_t reply_buf_len) {
    if (!p || !model || !reply_buf || reply_buf_len == 0)
        return -1;

    /* Look up file record */
    FileRecord rec;
    int rc = file_repo_find_by_id(p->repo, file_id, &rec);
    if (rc != FILE_REPO_OK) {
        snprintf(reply_buf, reply_buf_len, "\xF0\x9F\x94\xB4 File with ID `%lld` not found.",
                 (long long)file_id);
        return -1;
    }

    LOG_INFO("retry OCR with model %s for file id=%lld: %s", model, (long long)file_id,
             rec.saved_file_name);

    /* Read original file data from storage */
    size_t data_len = 0;
    uint8_t *data =
        storage_backend_read_binary(file_repo_get_storage(p->repo), rec.saved_file_name, &data_len);
    if (!data || data_len == 0) {
        free(data);
        snprintf(reply_buf, reply_buf_len,
                 "\xF0\x9F\x94\xB4 *Cannot Retry*\n\n"
                 "\xF0\x9F\x93\x84 File: `%s`\n"
                 "\xF0\x9F\x94\x91 ID: `%lld`\n\n"
                 "Original file is missing from storage.",
                 rec.original_file_name, (long long)file_id);
        return -1;
    }

    /* Run OCR with the specified model */
    char *new_ocr_text = NULL;
    rc = ocr_extract_text_with_model(p->ocr, data, data_len, rec.original_file_name, model,
                                     &new_ocr_text);
    free(data);

    if (rc != OCR_OK || !new_ocr_text) {
        snprintf(reply_buf, reply_buf_len,
                 "\xF0\x9F\x94\xB4 *OCR Failed*\n\n"
                 "\xF0\x9F\x93\x84 File: `%s`\n"
                 "\xF0\x9F\x94\x91 ID: `%lld`\n"
                 "\xF0\x9F\xA4\x96 Model: `%s`\n\n"
                 "OCR extraction failed with model `%s`.",
                 rec.original_file_name, (long long)file_id, model, model);
        return -1;
    }

    /* Save new OCR text */
    char ocr_filename[MAX_FILENAME];
    storage_ocr_filename(rec.saved_file_name, ocr_filename, sizeof(ocr_filename));

    if (storage_backend_save_text(p->storage, ocr_filename, new_ocr_text) != 0) {
        LOG_ERROR("failed to write OCR file %s", ocr_filename);
    }

    rc = file_repo_mark_ocr_complete(p->repo, rec.id, ocr_filename);
    if (rc != FILE_REPO_OK) {
        LOG_ERROR("repository error marking OCR done");
    }

    /* Parse the receipt */
    char *parsed_json = NULL;
    rc = ocr_parse_receipt(p->ocr, new_ocr_text, &parsed_json);
    free(new_ocr_text);

    if (rc != OCR_OK || !parsed_json) {
        snprintf(reply_buf, reply_buf_len,
                 "\xE2\x9A\xA0\xEF\xB8\x8F *Parsing Failed*\n\n"
                 "\xF0\x9F\x93\x84 File: `%s`\n"
                 "\xF0\x9F\x94\x91 ID: `%lld`\n"
                 "\xF0\x9F\xA4\x96 Model: `%s`\n\n"
                 "OCR succeeded but parsing failed.",
                 rec.original_file_name, (long long)file_id, model);
        return -1;
    }

    rc = file_repo_mark_parsing_complete(p->repo, file_id, parsed_json);
    if (rc != FILE_REPO_OK) {
        LOG_ERROR("repository error saving parsed receipt");
    }

    cJSON *json = cJSON_Parse(parsed_json);
    if (!json) {
        snprintf(reply_buf, reply_buf_len,
                 "\xE2\x9A\xA0\xEF\xB8\x8F *Invalid JSON*\n\n"
                 "\xF0\x9F\x93\x84 File: `%s`\n"
                 "\xF0\x9F\x94\x91 ID: `%lld`\n"
                 "\xF0\x9F\xA4\x96 Model: `%s`",
                 rec.original_file_name, (long long)file_id, model);
        free(parsed_json);
        return 0;
    }

    ReplyMessage *rmsg = processor_build_reply(json, rec.id, ocr_get_last_tokens(p->ocr));
    if (rmsg) {
        size_t rlen = strlen(rmsg->text);
        if (rlen + 64 < reply_buf_len) {
            snprintf(rmsg->text + rlen, sizeof(rmsg->text) - rlen,
                     "\n\n\xF0\x9F\x94\x91 ID: `%lld`\n\xF0\x9F\xA4\x96 Model: `%s`",
                     (long long)rec.id, model);
        }
        snprintf(reply_buf, reply_buf_len, "%s", rmsg->text);
        reply_message_free(rmsg);
    }
    cJSON_Delete(json);
    free(parsed_json);
    return 0;
}
