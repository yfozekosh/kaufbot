#include "processor.h"
#include "storage.h"
#include "gemini.h"
#include "config.h"
#include "../third_party/cjson/cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Processor {
    DBBackend        *db;
    StorageBackend   *storage;
    GeminiClient     *gemini;
    DuplicateStrategyFn dup_strategy;
};

/* ── built-in duplicate strategy ─────────────────────────────────────────── */

int strategy_notify_and_skip(const FileRecord *existing,
                              char *reply_buf, size_t buf_len)
{
    LOG_INFO("duplicate detected: %s", existing->saved_file_name);
    snprintf(reply_buf, buf_len,
             "⚠️ Duplicate detected — this file was already uploaded.\n"
             "📄 Saved as: %s\n"
             "🗓 Uploaded: %s\n"
             "🔍 OCR: %s",
             existing->saved_file_name,
             existing->created_at,
             existing->is_ocr_processed ? "✅ done" : "❌ not processed");
    return 0; /* stop processing */
}

/* ── constructor / destructor ─────────────────────────────────────────────── */

Processor *processor_new(DBBackend *db, StorageBackend *storage,
                         void *gemini, DuplicateStrategyFn dup_strategy)
{
    LOG_DEBUG("creating processor");
    Processor *p = calloc(1, sizeof(Processor));
    if (!p) {
        LOG_ERROR("failed to allocate processor");
        return NULL;
    }
    p->db           = db;
    p->storage      = storage;
    p->gemini       = (GeminiClient *)gemini;
    p->dup_strategy = dup_strategy ? dup_strategy : strategy_notify_and_skip;
    LOG_DEBUG("processor created successfully");
    return p;
}

void processor_free(Processor *p)
{
    free(p);
}

/* ── pipeline ─────────────────────────────────────────────────────────────── */

void processor_handle_file(Processor     *p,
                           const char    *original_name,
                           const uint8_t *data, size_t data_len,
                           char          *reply_buf, size_t reply_buf_len)
{
    LOG_INFO("processing file: %s (%zu bytes)", original_name, data_len);
    
    /* ── Step 1: Compute SHA-256 hash ────────────────────────────────────── */
    char hash[SHA256_HEX_LEN];
    storage_sha256_hex(data, data_len, hash);

    /* ── Step 2: Check for duplicate ─────────────────────────────────────── */
    FileRecord existing;
    int found = db_backend_find_by_hash(p->db, hash, &existing);

    if (found == 0) {
        /* Duplicate – delegate to strategy */
        LOG_WARN("duplicate file detected");
        int should_continue = p->dup_strategy(&existing, reply_buf, reply_buf_len);
        if (!should_continue) return;
    } else if (found == -1) {
        LOG_ERROR("database error while checking for duplicates");
        snprintf(reply_buf, reply_buf_len,
                 "❌ Database error while checking for duplicates.");
        return;
    }

    /* ── Step 3: Determine file extension and generate saved filename ────── */
    const char *ext = strrchr(original_name, '.');
    char saved_name[MAX_FILENAME];
    storage_gen_filename(ext ? ext : "", saved_name, sizeof(saved_name));
    LOG_DEBUG("generated filename: %s", saved_name);

    /* ── Step 4: Save file via storage backend ───────────────────────────── */
    if (storage_backend_save_file(p->storage, saved_name, data, data_len) != 0) {
        LOG_ERROR("failed to save file to storage");
        snprintf(reply_buf, reply_buf_len,
                 "❌ Failed to save file to storage.");
        return;
    }

    /* ── Step 5: Insert record into DB (OCR not yet done) ────────────────── */
    FileRecord rec;
    memset(&rec, 0, sizeof(rec));
    snprintf(rec.original_file_name, DB_ORIG_NAME_LEN, "%s", original_name);
    rec.file_size_bytes = (int64_t)data_len;
    snprintf(rec.saved_file_name, DB_FILENAME_LEN, "%s", saved_name);
    snprintf(rec.file_hash, DB_HASH_LEN, "%s", hash);
    rec.is_ocr_processed = 0;

    if (db_backend_insert(p->db, &rec) != 0) {
        LOG_ERROR("database error while saving file record");
        snprintf(reply_buf, reply_buf_len,
                 "❌ Database error while saving file record.");
        return;
    }
    LOG_DEBUG("file record inserted with id=%lld", (long long)rec.id);

    /* ── Step 6: Run OCR via Gemini ──────────────────────────────────────── */
    LOG_INFO("sending file to Gemini for OCR");
    snprintf(reply_buf, reply_buf_len,
             "✅ File saved as %s\n📤 Sending to Gemini for OCR…", saved_name);

    char *ocr_text = gemini_extract_text(p->gemini, data, data_len, original_name);

    if (!ocr_text) {
        LOG_ERROR("OCR extraction failed");
        snprintf(reply_buf, reply_buf_len,
                 "✅ File saved as: %s\n"
                 "⚠️ OCR failed — file is saved, OCR can be retried later.\n"
                 "📋 Original: %s  |  Size: %zu bytes",
                 saved_name, original_name, data_len);
        return;
    }
    LOG_DEBUG("OCR text extracted (%zu chars)", strlen(ocr_text));

    /* ── Step 7: Save OCR result via storage backend ─────────────────────── */
    char ocr_filename[MAX_FILENAME];
    storage_ocr_filename(saved_name, ocr_filename, sizeof(ocr_filename));

    if (storage_backend_save_text(p->storage, ocr_filename, ocr_text) != 0) {
        LOG_ERROR("failed to write OCR file %s", ocr_filename);
    } else {
        LOG_DEBUG("OCR text saved to: %s", ocr_filename);
    }

    /* ── Step 8: Update DB record ────────────────────────────────────────── */
    db_backend_mark_ocr_done(p->db, rec.id, ocr_filename);

    /* ── Step 9: Parse receipt via Gemini ────────────────────────────────── */
    LOG_INFO("sending OCR text to Gemini for parsing");
    char *parsed_json = gemini_parse_receipt(p->gemini, ocr_text);

    if (!parsed_json) {
        LOG_ERROR("gemini_parse_receipt returned NULL");
        snprintf(reply_buf, reply_buf_len,
                 "✅ File saved: %s\n"
                 "✅ OCR result saved: %s\n"
                 "⚠️ Parsing failed — OCR data is saved, parsing can be retried later.\n"
                 "📋 Original: %s  |  Size: %zu bytes",
                 saved_name, ocr_filename, original_name, data_len);
        free(ocr_text);
        return;
    }

    /* ── Step 10: Save parsed JSON to DB ─────────────────────────────────── */
    if (db_backend_mark_parsing_done(p->db, rec.id, parsed_json) != 0) {
        LOG_ERROR("failed to save parsed receipt");
    }

    /* ── Step 11: Build reply ────────────────────────────────────────────── */
    cJSON *json = cJSON_Parse(parsed_json);
    if (!json) {
        LOG_ERROR("cJSON_Parse failed - invalid JSON: %.200s", parsed_json);
        snprintf(reply_buf, reply_buf_len,
                 "✅ File saved: %s\n"
                 "✅ OCR result saved: %s\n"
                 "✅ Receipt parsed and saved to database.\n"
                 "📋 Original: %s  |  Size: %zu bytes",
                 saved_name, ocr_filename, original_name, data_len);
        free(ocr_text);
        free(parsed_json);
        return;
    }

    cJSON *store_info = cJSON_GetObjectItem(json, "store_information");
    cJSON *store_name = store_info ? cJSON_GetObjectItem(store_info, "name") : NULL;
    cJSON *total_sum = cJSON_GetObjectItem(json, "total_sum");
    cJSON *line_items = cJSON_GetObjectItem(json, "line_items");

    const char *name = store_name && store_name->valuestring ? store_name->valuestring : "Unknown";
    double total = total_sum && cJSON_IsNumber(total_sum) ? total_sum->valuedouble : 0;
    int item_count = line_items && cJSON_IsArray(line_items) ? cJSON_GetArraySize(line_items) : 0;

    if (!store_info || !store_name || !total_sum || !line_items) {
        LOG_WARN("parsed JSON missing expected fields");
    }

    snprintf(reply_buf, reply_buf_len,
             "✅ File saved: %s\n"
             "✅ OCR result saved: %s\n"
             "✅ Receipt parsed successfully!\n\n"
             "🏪 Store: %s\n"
             "🛒 Items: %d\n"
             "💰 Total: %.2f EUR\n\n"
             "📊 Full parsed data saved to database.",
             saved_name, ocr_filename, name, item_count, total);

    LOG_INFO("file processing completed successfully");
    cJSON_Delete(json);
    free(ocr_text);
    free(parsed_json);
}
