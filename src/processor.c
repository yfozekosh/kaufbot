#include "processor.h"
#include "storage.h"
#include "gemini.h"
#include "db.h"
#include "../third_party/cjson/cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Processor {
    DB                 *db;
    const char         *storage_path;
    GeminiClient       *gemini;
    DuplicateStrategyFn dup_strategy;
};

/* ── built-in duplicate strategy ─────────────────────────────────────────── */

int strategy_notify_and_skip(const FileRecord *existing,
                              char *reply_buf, size_t buf_len)
{
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

Processor *processor_new(DB *db, const char *storage_path,
                         void *gemini, DuplicateStrategyFn dup_strategy)
{
    Processor *p = calloc(1, sizeof(Processor));
    if (!p) return NULL;
    p->db           = db;
    p->storage_path = storage_path;
    p->gemini       = (GeminiClient *)gemini;
    p->dup_strategy = dup_strategy ? dup_strategy : strategy_notify_and_skip;
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
    /* ── Step 1: Compute SHA-256 hash ────────────────────────────────────── */
    char hash[SHA256_HEX_LEN];
    storage_sha256_hex(data, data_len, hash);

    /* ── Step 2: Check for duplicate ─────────────────────────────────────── */
    FileRecord existing;
    int found = db_find_by_hash(p->db, hash, &existing);

    if (found == 0) {
        /* Duplicate – delegate to strategy */
        int should_continue = p->dup_strategy(&existing, reply_buf, reply_buf_len);
        if (!should_continue) return;
        /* Future: strategy returned 1 → continue with override logic here */
    } else if (found == -1) {
        snprintf(reply_buf, reply_buf_len,
                 "❌ Database error while checking for duplicates.");
        return;
    }

    /* ── Step 3: Determine file extension and generate saved filename ────── */
    const char *ext = strrchr(original_name, '.');
    char saved_name[MAX_FILENAME];
    storage_gen_filename(ext ? ext : "", saved_name, sizeof(saved_name));

    /* ── Step 4: Save original file to disk ─────────────────────────────── */
    if (storage_save_file(p->storage_path, saved_name, data, data_len) != 0) {
        snprintf(reply_buf, reply_buf_len,
                 "❌ Failed to save file to disk.");
        return;
    }

    /* ── Step 5: Insert record into DB (OCR not yet done) ────────────────── */
    FileRecord rec;
    memset(&rec, 0, sizeof(rec));
    strncpy(rec.original_file_name, original_name, DB_ORIG_NAME_LEN - 1);
    rec.file_size_bytes = (int64_t)data_len;
    strncpy(rec.saved_file_name, saved_name, DB_FILENAME_LEN - 1);
    strncpy(rec.file_hash, hash, DB_HASH_LEN - 1);
    rec.is_ocr_processed = 0;

    if (db_insert(p->db, &rec) != 0) {
        snprintf(reply_buf, reply_buf_len,
                 "❌ Database error while saving file record.");
        return;
    }

    /* ── Step 6: Run OCR via Gemini ──────────────────────────────────────── */
    snprintf(reply_buf, reply_buf_len,
             "✅ File saved as %s\n📤 Sending to Gemini for OCR…", saved_name);
    /* (reply_buf is overwritten below with OCR result) */

    char *ocr_text = gemini_extract_text(p->gemini, data, data_len, original_name);

    if (!ocr_text) {
        snprintf(reply_buf, reply_buf_len,
                 "✅ File saved as: %s\n"
                 "⚠️ OCR failed — file is saved, OCR can be retried later.\n"
                 "📋 Original: %s  |  Size: %zu bytes",
                 saved_name, original_name, data_len);
        return;
    }

    /* ── Step 7: Save OCR result as plain text file ───────────────────────── */
    char ocr_filename[MAX_FILENAME];
    storage_ocr_filename(saved_name, ocr_filename, sizeof(ocr_filename));

    if (storage_save_text(p->storage_path, ocr_filename, ocr_text) != 0) {
        fprintf(stderr, "[processor] failed to write OCR file %s\n", ocr_filename);
        /* Non-fatal: we still have the text in memory */
    }

    /* ── Step 8: Update DB record ────────────────────────────────────────── */
    db_mark_ocr_done(p->db, rec.id, ocr_filename);

    /* ── Step 9: Parse receipt via Gemini ────────────────────────────────── */
    snprintf(reply_buf, reply_buf_len,
             "✅ OCR done\n📤 Sending to Gemini for receipt parsing…");

    char *parsed_json = gemini_parse_receipt(p->gemini, ocr_text);

    if (!parsed_json) {
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
    if (db_mark_parsing_done(p->db, rec.id, parsed_json) != 0) {
        fprintf(stderr, "[processor] failed to save parsed receipt for %s\n", saved_name);
        /* Non-fatal: we still have the JSON in memory for the reply */
    }

    /* ── Step 11: Build reply (send parsed data preview to user) ─────────── */
    /* Extract a preview from the parsed JSON for the reply */
    cJSON *json = cJSON_Parse(parsed_json);
    if (json) {
        cJSON *store_info = cJSON_GetObjectItem(json, "store_information");
        cJSON *store_name = store_info ? cJSON_GetObjectItem(store_info, "name") : NULL;
        cJSON *total_sum = cJSON_GetObjectItem(json, "total_sum");
        cJSON *line_items = cJSON_GetObjectItem(json, "line_items");
        
        const char *name = store_name && store_name->valuestring ? store_name->valuestring : "Unknown";
        double total = total_sum && cJSON_IsNumber(total_sum) ? total_sum->valuedouble : 0;
        int item_count = line_items && cJSON_IsArray(line_items) ? cJSON_GetArraySize(line_items) : 0;

        snprintf(reply_buf, reply_buf_len,
                 "✅ File saved: %s\n"
                 "✅ OCR result saved: %s\n"
                 "✅ Receipt parsed successfully!\n\n"
                 "🏪 Store: %s\n"
                 "🛒 Items: %d\n"
                 "💰 Total: %.2f EUR\n\n"
                 "📊 Full parsed data saved to database.",
                 saved_name, ocr_filename, name, item_count, total);
        
        cJSON_Delete(json);
    } else {
        snprintf(reply_buf, reply_buf_len,
                 "✅ File saved: %s\n"
                 "✅ OCR result saved: %s\n"
                 "✅ Receipt parsed and saved to database.\n"
                 "📋 Original: %s  |  Size: %zu bytes",
                 saved_name, ocr_filename, original_name, data_len);
    }

    free(ocr_text);
    free(parsed_json);
}
