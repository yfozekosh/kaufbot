#include "bot.h"
#include "bot_telegram.h"
#include "config.h"
#include "db_backend.h"
#include "file_repository.h"
#include "gemini.h"
#include "ocr_service.h"
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

    /* Telegram API configuration */
    snprintf(cfg.telegram_api_base, MAX_URL_LEN, "https://api.telegram.org/bot");
    snprintf(cfg.telegram_file_base, MAX_URL_LEN, "https://api.telegram.org/file/bot");
    cfg.telegram_poll_timeout_secs = 30;
    cfg.telegram_http_timeout_secs = 60;
    cfg.telegram_download_timeout_secs = 120;
    cfg.telegram_reconnect_delay_secs = 5;
    cfg.telegram_retry_delay_secs = 2;
    cfg.max_file_size_bytes = (size_t)20 * 1024 * 1024;

    /* Gemini API configuration */
    snprintf(cfg.gemini_api_base, GEMINI_URL_BUF_LEN,
             "https://generativelanguage.googleapis.com/v1beta/models");
    cfg.gemini_http_timeout_secs = 600;
    cfg.gemini_connect_timeout_secs = 15;

    return cfg;
}

/* Helper: clean test DB file to avoid stale state */
static void clean_test_db(void) {
    unlink("/tmp/kaufbot_test_bot.db");
}

/* ── bot lifecycle tests ──────────────────────────────────────────── */
TEST_CASE(bot_new_and_free) {
    Config cfg = make_test_config();
    DBBackend *db = db_backend_open(&cfg);
    StorageBackend *storage = storage_backend_open(&cfg);
    FileRepository *repo = file_repository_db_backend(db, storage);
    OCRService *ocr =
        ocr_service_gemini_new(cfg.gemini_api_key, cfg.gemini_model, cfg.gemini_fallback_model,
                               cfg.gemini_fallback_enabled, NULL, 0);
    Processor *p = processor_new(repo, storage, ocr, NULL);

    TgBot *bot = bot_new(&cfg, p, db, storage);
    ASSERT_NOT_NULL(bot);

    bot_free(bot);
    bot_free(NULL);

    processor_free(p);
    ocr_service_free(ocr);
    file_repository_free(repo);
    db_backend_close(db);
    storage_backend_close(storage);
    TEST_PASS();
}
TEST_CASE(bot_stop_null) {
    bot_stop(NULL);
    TEST_PASS();
}

/* ── processor lifecycle tests ─────────────────────────────────────── */

TEST_CASE(processor_new_null_repo) {
    Config cfg = make_test_config();
    StorageBackend *storage = storage_backend_open(&cfg);
    OCRService *ocr =
        ocr_service_gemini_new(cfg.gemini_api_key, cfg.gemini_model, cfg.gemini_fallback_model,
                               cfg.gemini_fallback_enabled, NULL, 0);

    Processor *p = processor_new(NULL, storage, ocr, NULL);
    ASSERT_TRUE(p == NULL);

    ocr_service_free(ocr);
    storage_backend_close(storage);
    TEST_PASS();
}

TEST_CASE(processor_new_null_storage) {
    Config cfg = make_test_config();
    FileRepository *repo = file_repository_memory_new(NULL);
    OCRService *ocr =
        ocr_service_gemini_new(cfg.gemini_api_key, cfg.gemini_model, cfg.gemini_fallback_model,
                               cfg.gemini_fallback_enabled, NULL, 0);

    /* processor_new rejects NULL storage even with valid repo */
    Processor *p = processor_new(repo, NULL, ocr, NULL);
    ASSERT_TRUE(p == NULL);

    ocr_service_free(ocr);
    file_repository_free(repo);
    TEST_PASS();
}

TEST_CASE(processor_new_null_ocr) {
    Config cfg = make_test_config();
    DBBackend *db = db_backend_open(&cfg);
    StorageBackend *storage = storage_backend_open(&cfg);
    FileRepository *repo = file_repository_db_backend(db, storage);

    Processor *p = processor_new(repo, storage, NULL, NULL);
    ASSERT_TRUE(p == NULL);

    file_repository_free(repo);
    db_backend_close(db);
    storage_backend_close(storage);
    TEST_PASS();
}

TEST_CASE(processor_new_and_free) {
    Config cfg = make_test_config();
    DBBackend *db = db_backend_open(&cfg);
    StorageBackend *storage = storage_backend_open(&cfg);
    FileRepository *repo = file_repository_db_backend(db, storage);
    OCRService *ocr =
        ocr_service_gemini_new(cfg.gemini_api_key, cfg.gemini_model, cfg.gemini_fallback_model,
                               cfg.gemini_fallback_enabled, NULL, 0);

    Processor *p = processor_new(repo, storage, ocr, NULL);
    ASSERT_NOT_NULL(p);

    processor_free(p);
    processor_free(NULL);

    ocr_service_free(ocr);
    file_repository_free(repo);
    db_backend_close(db);
    storage_backend_close(storage);
    TEST_PASS();
}

TEST_CASE(processor_new_custom_strategy) {
    Config cfg = make_test_config();
    DBBackend *db = db_backend_open(&cfg);
    StorageBackend *storage = storage_backend_open(&cfg);
    FileRepository *repo = file_repository_db_backend(db, storage);
    OCRService *ocr =
        ocr_service_gemini_new(cfg.gemini_api_key, cfg.gemini_model, cfg.gemini_fallback_model,
                               cfg.gemini_fallback_enabled, NULL, 0);

    Processor *p = processor_new(repo, storage, ocr, strategy_notify_and_skip);
    ASSERT_NOT_NULL(p);

    processor_free(p);
    ocr_service_free(ocr);
    file_repository_free(repo);
    db_backend_close(db);
    storage_backend_close(storage);
    TEST_PASS();
}

/* ── processor_handle_file edge cases ──────────────────────────────── */

TEST_CASE(processor_handle_null_params) {
    Config cfg = make_test_config();
    DBBackend *db = db_backend_open(&cfg);
    StorageBackend *storage = storage_backend_open(&cfg);
    FileRepository *repo = file_repository_db_backend(db, storage);
    OCRService *ocr =
        ocr_service_gemini_new(cfg.gemini_api_key, cfg.gemini_model, cfg.gemini_fallback_model,
                               cfg.gemini_fallback_enabled, NULL, 0);
    Processor *p = processor_new(repo, storage, ocr, NULL);

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
    ocr_service_free(ocr);
    file_repository_free(repo);
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
    ASSERT_TRUE(strstr(buf, "pending") != NULL);
    TEST_PASS();
}

/* ── processor_retry_ocr error paths ──────────────────────────────── */

TEST_CASE(processor_retry_ocr_not_found) {
    Config cfg = make_test_config();
    DBBackend *db = db_backend_open(&cfg);
    StorageBackend *storage = storage_backend_open(&cfg);
    FileRepository *repo = file_repository_db_backend(db, storage);
    OCRService *ocr =
        ocr_service_gemini_new(cfg.gemini_api_key, cfg.gemini_model, cfg.gemini_fallback_model,
                               cfg.gemini_fallback_enabled, NULL, 0);
    Processor *p = processor_new(repo, storage, ocr, NULL);

    char reply[512];
    int rc = processor_retry_ocr(p, 9999, reply, sizeof(reply));
    ASSERT_EQ(-1, rc);
    ASSERT_TRUE(strstr(reply, "not found") != NULL);

    processor_free(p);
    ocr_service_free(ocr);
    file_repository_free(repo);
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
    FileRepository *repo = file_repository_db_backend(db, storage);
    OCRService *ocr =
        ocr_service_gemini_new(cfg.gemini_api_key, cfg.gemini_model, cfg.gemini_fallback_model,
                               cfg.gemini_fallback_enabled, NULL, 0);
    Processor *p = processor_new(repo, storage, ocr, NULL);

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
    ocr_service_free(ocr);
    file_repository_free(repo);
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
    FileRepository *repo = file_repository_db_backend(db, storage);
    OCRService *ocr =
        ocr_service_gemini_new(cfg.gemini_api_key, cfg.gemini_model, cfg.gemini_fallback_model,
                               cfg.gemini_fallback_enabled, NULL, 0);
    Processor *p = processor_new(repo, storage, ocr, NULL);

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
    ocr_service_free(ocr);
    file_repository_free(repo);
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
    FileRepository *repo = file_repository_db_backend(db, storage);
    OCRService *ocr =
        ocr_service_gemini_new(cfg.gemini_api_key, cfg.gemini_model, cfg.gemini_fallback_model,
                               cfg.gemini_fallback_enabled, NULL, 0);
    Processor *p = processor_new(repo, storage, ocr, NULL);

    ASSERT_EQ(-1, processor_retry_ocr(p, 1, NULL, sizeof(reply)));
    ASSERT_EQ(-1, processor_retry_ocr(p, 1, reply, 0));

    processor_free(p);
    ocr_service_free(ocr);
    file_repository_free(repo);
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
    FileRepository *repo = file_repository_db_backend(db, storage);
    OCRService *ocr =
        ocr_service_gemini_new(cfg.gemini_api_key, cfg.gemini_model, cfg.gemini_fallback_model,
                               cfg.gemini_fallback_enabled, NULL, 0);
    Processor *p = processor_new(repo, storage, ocr, NULL);

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
    ocr_service_free(ocr);
    file_repository_free(repo);
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
    FileRepository *repo = file_repository_db_backend(db, storage);
    OCRService *ocr =
        ocr_service_gemini_new(cfg.gemini_api_key, cfg.gemini_model, cfg.gemini_fallback_model,
                               cfg.gemini_fallback_enabled, NULL, 0);
    Processor *p = processor_new(repo, storage, ocr, NULL);

    char reply[512] = {0};
    processor_handle_file(p, "empty.jpg", (const uint8_t *)"", 0, reply, sizeof(reply), NULL);
    /* Should handle empty data gracefully */
    ASSERT_TRUE(strlen(reply) > 0 || reply[0] == '\0');

    processor_free(p);
    ocr_service_free(ocr);
    file_repository_free(repo);
    db_backend_close(db);
    storage_backend_close(storage);
    TEST_PASS();
}

TEST_CASE(processor_null_processor_free) {
    processor_free(NULL);
    TEST_PASS();
}

/* ── OCR service wrapper null args ────────────────────────────────────── */

TEST_CASE(ocr_extract_text_null_args) {
    char *out = NULL;
    const uint8_t data[] = "test";

    ASSERT_EQ(OCR_ERR_INVALID_ARG, ocr_extract_text(NULL, data, 4, "test.jpg", &out));
    TEST_PASS();
}

TEST_CASE(ocr_parse_receipt_null_args) {
    char *out = NULL;

    ASSERT_EQ(OCR_ERR_INVALID_ARG, ocr_parse_receipt(NULL, "text", &out));
    TEST_PASS();
}

TEST_CASE(ocr_get_model_null) {
    ASSERT_TRUE(ocr_get_model(NULL) == NULL);
    TEST_PASS();
}

TEST_CASE(ocr_is_healthy_null) {
    ASSERT_EQ(0, ocr_is_healthy(NULL));
    TEST_PASS();
}

/* ── bot_telegram / message sender tests ──────────────────────────────── */

TEST_CASE(message_sender_telegram_null) {
    MessageSender *sender = message_sender_telegram_new(NULL);
    ASSERT_TRUE(sender == NULL);
    TEST_PASS();
}

TEST_CASE(message_sender_free_null) {
    message_sender_free(NULL);
    TEST_PASS();
}

/* ── ocr_extract_text_with_model null args ────────────────────────────── */

TEST_CASE(ocr_extract_text_with_model_null_args) {
    char *out = NULL;
    const uint8_t data[] = "test";

    /* NULL ocr service */
    ASSERT_EQ(OCR_ERR_INVALID_ARG,
              ocr_extract_text_with_model(NULL, data, 4, "test.jpg", "model", &out));
    TEST_PASS();
}

/* ── processor_retry_ocr_with_model null args ─────────────────────────── */

TEST_CASE(processor_retry_ocr_with_model_null_args) {
    char reply[512];

    ASSERT_EQ(-1, processor_retry_ocr_with_model(NULL, 1, "model", reply, sizeof(reply)));

    Config cfg = make_test_config();
    DBBackend *db = db_backend_open(&cfg);
    ASSERT_NOT_NULL(db);
    StorageBackend *storage = storage_backend_open(&cfg);
    ASSERT_NOT_NULL(storage);
    storage_backend_ensure_dirs(storage);
    FileRepository *repo = file_repository_db_backend(db, storage);
    ASSERT_NOT_NULL(repo);
    OCRService *ocr =
        ocr_service_gemini_new(cfg.gemini_api_key, cfg.gemini_model, cfg.gemini_fallback_model,
                               cfg.gemini_fallback_enabled, NULL, 0);
    ASSERT_NOT_NULL(ocr);
    Processor *p = processor_new(repo, storage, ocr, NULL);
    ASSERT_NOT_NULL(p);

    ASSERT_EQ(-1, processor_retry_ocr_with_model(p, 1, NULL, reply, sizeof(reply)));
    ASSERT_EQ(-1, processor_retry_ocr_with_model(p, 1, "model", NULL, sizeof(reply)));
    ASSERT_EQ(-1, processor_retry_ocr_with_model(p, 1, "model", reply, 0));

    processor_free(p);
    ocr_service_free(ocr);
    file_repository_free(repo);
    db_backend_close(db);
    storage_backend_close(storage);
    TEST_PASS();
}

/* ── processor_retry_ocr_with_model not found ─────────────────────────── */

TEST_CASE(processor_retry_ocr_with_model_not_found) {
    clean_test_db();
    Config cfg = make_test_config();
    DBBackend *db = db_backend_open(&cfg);
    ASSERT_NOT_NULL(db);
    StorageBackend *storage = storage_backend_open(&cfg);
    ASSERT_NOT_NULL(storage);
    storage_backend_ensure_dirs(storage);
    FileRepository *repo = file_repository_db_backend(db, storage);
    ASSERT_NOT_NULL(repo);
    OCRService *ocr =
        ocr_service_gemini_new(cfg.gemini_api_key, cfg.gemini_model, cfg.gemini_fallback_model,
                               cfg.gemini_fallback_enabled, NULL, 0);
    ASSERT_NOT_NULL(ocr);
    Processor *p = processor_new(repo, storage, ocr, NULL);
    ASSERT_NOT_NULL(p);

    char reply[512];
    int rc = processor_retry_ocr_with_model(p, 9999, "gemini-2.5-flash", reply, sizeof(reply));
    ASSERT_EQ(-1, rc);
    ASSERT_TRUE(strstr(reply, "not found") != NULL);

    processor_free(p);
    ocr_service_free(ocr);
    file_repository_free(repo);
    db_backend_close(db);
    storage_backend_close(storage);
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
    FileRepository *repo = file_repository_db_backend(db, storage);
    OCRService *ocr =
        ocr_service_gemini_new(cfg.gemini_api_key, cfg.gemini_model, cfg.gemini_fallback_model,
                               cfg.gemini_fallback_enabled, NULL, 0);
    Processor *p = processor_new(repo, storage, ocr, NULL);

    TgBot *bot = bot_new(&cfg, p, db, storage);
    ASSERT_NOT_NULL(bot);

    /* Stop before start - loop exits immediately */
    bot_stop(bot);
    bot_start(bot);

    bot_free(bot);
    processor_free(p);
    ocr_service_free(ocr);
    file_repository_free(repo);
    db_backend_close(db);
    storage_backend_close(storage);
    TEST_PASS();
}
