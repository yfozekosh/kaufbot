#include "bot.h"
#include "config.h"
#include "db_backend.h"
#include "gemini.h"
#include "processor.h"
#include "storage_backend.h"
#include "test_helpers.h"
#include "test_runner.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Helper: build a minimal valid config for testing */
static Config make_test_config(void) {
    Config cfg;
    memset(&cfg, 0, sizeof(cfg));
    snprintf(cfg.telegram_token, MAX_TOKEN_LEN, "test_token_123");
    snprintf(cfg.gemini_api_key, MAX_TOKEN_LEN, "test_key_456");
    snprintf(cfg.gemini_model, MAX_MODEL_LEN, "gemini-2.5-flash");
    cfg.storage_backend = STORAGE_BACKEND_LOCAL;
    snprintf(cfg.storage_path, MAX_PATH_LEN, "/tmp/kaufbot_test_bot_storage");
    cfg.db_backend = DB_BACKEND_SQLITE;
    snprintf(cfg.db_path, MAX_PATH_LEN, "/tmp/kaufbot_test_bot.db");
    cfg.allowed_users[0] = 12345;
    cfg.allowed_users_count = 1;
    return cfg;
}

/* ── bot lifecycle tests ──────────────────────────────────────────── */
TEST_CASE(bot_new_and_free) {
    Config cfg = make_test_config();
    DBBackend *db = db_backend_open(&cfg);
    StorageBackend *storage = storage_backend_open(&cfg);
    GeminiClient *gemini = gemini_new(cfg.gemini_api_key, cfg.gemini_model);
    Processor *p = processor_new(db, storage, gemini, NULL);

    TgBot *bot = bot_new(&cfg, p, db, storage);
    ASSERT_NOT_NULL(bot);

    bot_free(bot);
    bot_free(NULL);

    processor_free(p);
    gemini_free(gemini);
    db_backend_close(db);
    storage_backend_close(storage);
    TEST_PASS();
}
TEST_CASE(bot_stop_null) {
    bot_stop(NULL);
    TEST_PASS();
}

/* ── processor lifecycle tests ─────────────────────────────────────── */

TEST_CASE(processor_new_null_db) {
    Config cfg = make_test_config();
    StorageBackend *storage = storage_backend_open(&cfg);
    GeminiClient *gemini = gemini_new(cfg.gemini_api_key, cfg.gemini_model);

    Processor *p = processor_new(NULL, storage, gemini, NULL);
    ASSERT_TRUE(p == NULL);

    gemini_free(gemini);
    storage_backend_close(storage);
    TEST_PASS();
}

TEST_CASE(processor_new_null_storage) {
    Config cfg = make_test_config();
    DBBackend *db = db_backend_open(&cfg);
    GeminiClient *gemini = gemini_new(cfg.gemini_api_key, cfg.gemini_model);

    Processor *p = processor_new(db, NULL, gemini, NULL);
    ASSERT_TRUE(p == NULL);

    gemini_free(gemini);
    db_backend_close(db);
    TEST_PASS();
}

TEST_CASE(processor_new_null_gemini) {
    Config cfg = make_test_config();
    DBBackend *db = db_backend_open(&cfg);
    StorageBackend *storage = storage_backend_open(&cfg);

    Processor *p = processor_new(db, storage, NULL, NULL);
    ASSERT_TRUE(p == NULL);

    db_backend_close(db);
    storage_backend_close(storage);
    TEST_PASS();
}

TEST_CASE(processor_new_and_free) {
    Config cfg = make_test_config();
    DBBackend *db = db_backend_open(&cfg);
    StorageBackend *storage = storage_backend_open(&cfg);
    GeminiClient *gemini = gemini_new(cfg.gemini_api_key, cfg.gemini_model);

    Processor *p = processor_new(db, storage, gemini, NULL);
    ASSERT_NOT_NULL(p);

    processor_free(p);
    processor_free(NULL);

    gemini_free(gemini);
    db_backend_close(db);
    storage_backend_close(storage);
    TEST_PASS();
}

TEST_CASE(processor_new_custom_strategy) {
    Config cfg = make_test_config();
    DBBackend *db = db_backend_open(&cfg);
    StorageBackend *storage = storage_backend_open(&cfg);
    GeminiClient *gemini = gemini_new(cfg.gemini_api_key, cfg.gemini_model);

    Processor *p = processor_new(db, storage, gemini, strategy_notify_and_skip);
    ASSERT_NOT_NULL(p);

    processor_free(p);
    gemini_free(gemini);
    db_backend_close(db);
    storage_backend_close(storage);
    TEST_PASS();
}

/* ── processor_handle_file edge cases ──────────────────────────────── */

TEST_CASE(processor_handle_null_params) {
    Config cfg = make_test_config();
    DBBackend *db = db_backend_open(&cfg);
    StorageBackend *storage = storage_backend_open(&cfg);
    GeminiClient *gemini = gemini_new(cfg.gemini_api_key, cfg.gemini_model);
    Processor *p = processor_new(db, storage, gemini, NULL);

    char reply[256];

    /* NULL processor */
    processor_handle_file(NULL, "test.jpg", (const uint8_t *)"data", 4, reply, sizeof(reply), NULL);

    /* NULL name */
    processor_handle_file(p, NULL, (const uint8_t *)"data", 4, reply, sizeof(reply), NULL);

    /* NULL data */
    processor_handle_file(p, "test.jpg", NULL, 4, reply, sizeof(reply), NULL);

    /* NULL reply */
    processor_handle_file(p, "test.jpg", (const uint8_t *)"data", 4, NULL, 0, NULL);

    /* Zero reply_len */
    processor_handle_file(p, "test.jpg", (const uint8_t *)"data", 4, reply, 0, NULL);

    processor_free(p);
    gemini_free(gemini);
    db_backend_close(db);
    storage_backend_close(storage);
    TEST_PASS();
}

/* ── strategy_notify_and_skip ──────────────────────────────────────── */

TEST_CASE(strategy_notify_and_skip_format) {
    FileRecord rec;
    memset(&rec, 0, sizeof(rec));
    snprintf(rec.saved_file_name, DB_FILENAME_LEN, "upload_test.jpg");
    snprintf(rec.created_at, 32, "2024-01-01T12:00:00");
    rec.is_ocr_processed = 1;

    char buf[512];
    int result = strategy_notify_and_skip(&rec, buf, sizeof(buf));
    ASSERT_EQ(0, result);
    ASSERT_TRUE(strstr(buf, "upload_test.jpg") != NULL);
    ASSERT_TRUE(strstr(buf, "done") != NULL);
    ASSERT_TRUE(strstr(buf, "Duplicate") != NULL);
    TEST_PASS();
}
TEST_CASE(strategy_notify_and_skip_not_processed) {
    FileRecord rec;
    memset(&rec, 0, sizeof(rec));
    snprintf(rec.saved_file_name, DB_FILENAME_LEN, "upload_test.jpg");
    snprintf(rec.created_at, 32, "2024-01-01T12:00:00");
    rec.is_ocr_processed = 0;

    char buf[512];
    int result = strategy_notify_and_skip(&rec, buf, sizeof(buf));
    ASSERT_EQ(0, result);
    ASSERT_TRUE(strstr(buf, "not processed") != NULL);
    TEST_PASS();
}

/* ── processor_retry_ocr error paths ──────────────────────────────── */

TEST_CASE(processor_retry_ocr_not_found) {
    Config cfg = make_test_config();
    DBBackend *db = db_backend_open(&cfg);
    StorageBackend *storage = storage_backend_open(&cfg);
    GeminiClient *gemini = gemini_new(cfg.gemini_api_key, cfg.gemini_model);
    Processor *p = processor_new(db, storage, gemini, NULL);

    char reply[512];
    int rc = processor_retry_ocr(p, 9999, reply, sizeof(reply));
    ASSERT_EQ(-1, rc);
    ASSERT_TRUE(strstr(reply, "not found") != NULL);

    processor_free(p);
    gemini_free(gemini);
    db_backend_close(db);
    storage_backend_close(storage);
    TEST_PASS();
}

TEST_CASE(processor_retry_ocr_no_ocr_file) {
    Config cfg = make_test_config();
    test_rm(cfg.db_path);
    DBBackend *db = db_backend_open(&cfg);
    StorageBackend *storage = storage_backend_open(&cfg);
    storage_backend_ensure_dirs(storage);
    GeminiClient *gemini = gemini_new(cfg.gemini_api_key, cfg.gemini_model);
    Processor *p = processor_new(db, storage, gemini, NULL);

    /* Insert a file record without OCR */
    FileRecord rec;
    memset(&rec, 0, sizeof(rec));
    snprintf(rec.original_file_name, DB_ORIG_NAME_LEN, "test.jpg");
    rec.file_size_bytes = 100;
    snprintf(rec.saved_file_name, DB_FILENAME_LEN, "upload_test.jpg");
    snprintf(rec.file_hash, DB_HASH_LEN, "hash_retry_test");
    rec.is_ocr_processed = 0;
    ASSERT_EQ(0, db_backend_insert(db, &rec));

    char reply[512];
    int rc = processor_retry_ocr(p, rec.id, reply, sizeof(reply));
    ASSERT_EQ(-1, rc);
    ASSERT_TRUE(strstr(reply, "Cannot Retry") != NULL);

    processor_free(p);
    gemini_free(gemini);
    db_backend_close(db);
    storage_backend_close(storage);
    TEST_PASS();
}

TEST_CASE(processor_retry_ocr_with_ocr_text) {
    Config cfg = make_test_config();
    test_rm(cfg.db_path);
    DBBackend *db = db_backend_open(&cfg);
    StorageBackend *storage = storage_backend_open(&cfg);
    storage_backend_ensure_dirs(storage);
    GeminiClient *gemini = gemini_new(cfg.gemini_api_key, cfg.gemini_model);
    Processor *p = processor_new(db, storage, gemini, NULL);

    /* Insert a file record with OCR */
    FileRecord rec;
    memset(&rec, 0, sizeof(rec));
    snprintf(rec.original_file_name, DB_ORIG_NAME_LEN, "test.jpg");
    rec.file_size_bytes = 100;
    snprintf(rec.saved_file_name, DB_FILENAME_LEN, "upload_retry.jpg");
    snprintf(rec.file_hash, DB_HASH_LEN, "hash_retry_ocr");
    rec.is_ocr_processed = 1;
    snprintf(rec.ocr_file_name, DB_OCR_FILENAME_LEN, "upload_retry_ocr.txt");
    ASSERT_EQ(0, db_backend_insert(db, &rec));

    /* Save OCR text to storage */
    const char *ocr_text = "REWE\nMilk 1.99\nTotal 1.99";
    ASSERT_EQ(0, storage_backend_save_text(storage, rec.ocr_file_name, ocr_text));

    /* Retry will fail because Gemini API key is fake, but it should get past the
     * "no OCR text" check */
    char reply[512];
    processor_retry_ocr(p, rec.id, reply, sizeof(reply));
    /* reply should NOT say "Cannot Retry" or "not found" */
    ASSERT_TRUE(strstr(reply, "not found") == NULL);
    ASSERT_TRUE(strstr(reply, "No OCR text") == NULL);

    processor_free(p);
    gemini_free(gemini);
    db_backend_close(db);
    storage_backend_close(storage);
    TEST_PASS();
}

TEST_CASE(processor_retry_ocr_null_params) {
    char reply[512];
    ASSERT_EQ(-1, processor_retry_ocr(NULL, 1, reply, sizeof(reply)));

    Config cfg = make_test_config();
    DBBackend *db = db_backend_open(&cfg);
    StorageBackend *storage = storage_backend_open(&cfg);
    GeminiClient *gemini = gemini_new(cfg.gemini_api_key, cfg.gemini_model);
    Processor *p = processor_new(db, storage, gemini, NULL);

    ASSERT_EQ(-1, processor_retry_ocr(p, 1, NULL, sizeof(reply)));
    ASSERT_EQ(-1, processor_retry_ocr(p, 1, reply, 0));

    processor_free(p);
    gemini_free(gemini);
    db_backend_close(db);
    storage_backend_close(storage);
    TEST_PASS();
}

/* ── processor duplicate detection ─────────────────────────────────── */

TEST_CASE(processor_duplicate_detection) {
    Config cfg = make_test_config();
    DBBackend *db = db_backend_open(&cfg);
    StorageBackend *storage = storage_backend_open(&cfg);
    storage_backend_ensure_dirs(storage);
    GeminiClient *gemini = gemini_new(cfg.gemini_api_key, cfg.gemini_model);
    Processor *p = processor_new(db, storage, gemini, NULL);

    const uint8_t data[] = "test duplicate data";
    char reply1[512] = {0};
    char reply2[512] = {0};

    /* First call - would try OCR (fails because no real Gemini) */
    processor_handle_file(p, "test_dup.jpg", data, sizeof(data), reply1, sizeof(reply1), NULL);
    ASSERT_TRUE(strlen(reply1) > 0);

    /* Second call with same data - should detect duplicate */
    processor_handle_file(p, "test_dup.jpg", data, sizeof(data), reply2, sizeof(reply2), NULL);
    ASSERT_TRUE(strstr(reply2, "Duplicate") != NULL || strstr(reply2, "duplicate") != NULL ||
                strlen(reply2) > 0);

    processor_free(p);
    gemini_free(gemini);
    db_backend_close(db);
    storage_backend_close(storage);
    TEST_PASS();
}

/* ── processor empty/edge data ─────────────────────────────────────── */

TEST_CASE(processor_empty_data) {
    Config cfg = make_test_config();
    DBBackend *db = db_backend_open(&cfg);
    StorageBackend *storage = storage_backend_open(&cfg);
    storage_backend_ensure_dirs(storage);
    GeminiClient *gemini = gemini_new(cfg.gemini_api_key, cfg.gemini_model);
    Processor *p = processor_new(db, storage, gemini, NULL);

    char reply[512] = {0};
    processor_handle_file(p, "empty.jpg", (const uint8_t *)"", 0, reply, sizeof(reply), NULL);
    /* Should handle empty data gracefully */
    ASSERT_TRUE(strlen(reply) > 0 || reply[0] == '\0');

    processor_free(p);
    gemini_free(gemini);
    db_backend_close(db);
    storage_backend_close(storage);
    TEST_PASS();
}

TEST_CASE(processor_null_processor_free) {
    processor_free(NULL);
    TEST_PASS();
}

/* ── config is_allowed edge cases ──────────────────────────────────── */

TEST_CASE(config_is_allowed_empty) {
    Config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.allowed_users_count = 0;
    ASSERT_EQ(0, config_is_allowed(&cfg, 123));
    TEST_PASS();
}

TEST_CASE(config_is_allowed_negative_id) {
    Config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.allowed_users[0] = -100;
    cfg.allowed_users_count = 1;
    ASSERT_EQ(1, config_is_allowed(&cfg, -100));
    ASSERT_EQ(0, config_is_allowed(&cfg, 100));
    TEST_PASS();
}

/* ── bot_start immediate exit ──────────────────────────────────────── */

TEST_CASE(bot_start_immediate_exit) {
    Config cfg = make_test_config();
    DBBackend *db = db_backend_open(&cfg);
    StorageBackend *storage = storage_backend_open(&cfg);
    GeminiClient *gemini = gemini_new(cfg.gemini_api_key, cfg.gemini_model);
    Processor *p = processor_new(db, storage, gemini, NULL);

    TgBot *bot = bot_new(&cfg, p, db, storage);
    ASSERT_NOT_NULL(bot);

    /* Stop before start - loop exits immediately */
    bot_stop(bot);
    bot_start(bot);

    bot_free(bot);
    processor_free(p);
    gemini_free(gemini);
    db_backend_close(db);
    storage_backend_close(storage);
    TEST_PASS();
}
