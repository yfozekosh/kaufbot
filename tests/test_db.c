/* ── Database module tests ────────────────────────────────────────────────── */

#include "db.h"
#include "test_helpers.h"
#include "test_runner.h"
#include <string.h>
#include <unistd.h>

static DBBackend *g_test_db = NULL;
static const char *g_test_db_path = "/tmp/kaufbot_test.db";

static void setup_db(void) {
    test_rm(g_test_db_path);
    g_test_db = test_db_open_sqlite(g_test_db_path);
    ASSERT_NOT_NULL(g_test_db);
}

static void teardown_db(void) {
    if (g_test_db) {
        test_db_close(g_test_db);
        g_test_db = NULL;
    }
    test_rm(g_test_db_path);
}

/* Test database open/close */
TEST_CASE(db_open_close) {
    setup_db();
    ASSERT_NOT_NULL(g_test_db);
    ASSERT_NOT_NULL(g_test_db->ops);
    teardown_db();
    TEST_PASS();
}

TEST_CASE(db_open_existing) {
    setup_db();
    test_db_close(g_test_db);
    g_test_db = NULL;

    /* Re-open the same database */
    g_test_db = test_db_open_sqlite(g_test_db_path);
    ASSERT_NOT_NULL(g_test_db);
    teardown_db();
    TEST_PASS();
}

/* Test insert and find by hash */
TEST_CASE(db_insert_and_find) {
    setup_db();

    FileRecord rec;
    memset(&rec, 0, sizeof(rec));
    snprintf(rec.original_file_name, DB_ORIG_NAME_LEN, "%s", "test_receipt.jpg");
    rec.file_size_bytes = 12345;
    snprintf(rec.saved_file_name, DB_FILENAME_LEN, "%s", "upload_2024-01-01_12_00_00.jpg");
    snprintf(rec.file_hash, DB_HASH_LEN, "%s", "abc123def456789");
    rec.is_ocr_processed = 0;

    int result = db_backend_insert(g_test_db, &rec);
    ASSERT_EQ(0, result);
    ASSERT_TRUE(rec.id > 0);

    /* Find by hash */
    FileRecord found;
    int found_result = db_backend_find_by_hash(g_test_db, "abc123def456789", &found);
    ASSERT_EQ(0, found_result);
    ASSERT_STR_EQ("test_receipt.jpg", found.original_file_name);
    ASSERT_EQ(12345, found.file_size_bytes);
    ASSERT_EQ(0, found.is_ocr_processed);

    teardown_db();
    TEST_PASS();
}

TEST_CASE(db_find_not_found) {
    setup_db();

    /* Insert a record */
    FileRecord rec;
    memset(&rec, 0, sizeof(rec));
    snprintf(rec.original_file_name, DB_ORIG_NAME_LEN, "%s", "test.jpg");
    rec.file_size_bytes = 100;
    snprintf(rec.saved_file_name, DB_FILENAME_LEN, "%s", "upload.jpg");
    snprintf(rec.file_hash, DB_HASH_LEN, "%s", "unique_hash_123");
    rec.is_ocr_processed = 0;
    db_backend_insert(g_test_db, &rec);

    /* Search for non-existent hash */
    FileRecord found;
    int result = db_backend_find_by_hash(g_test_db, "nonexistent", &found);
    ASSERT_EQ(1, result); /* 1 = not found */

    teardown_db();
    TEST_PASS();
}

/* Test duplicate hash detection */
TEST_CASE(db_duplicate_hash) {
    setup_db();

    /* Insert first record */
    FileRecord rec1;
    memset(&rec1, 0, sizeof(rec1));
    snprintf(rec1.original_file_name, DB_ORIG_NAME_LEN, "%s", "test1.jpg");
    rec1.file_size_bytes = 100;
    snprintf(rec1.saved_file_name, DB_FILENAME_LEN, "%s", "upload1.jpg");
    snprintf(rec1.file_hash, DB_HASH_LEN, "%s", "same_hash");
    rec1.is_ocr_processed = 0;
    ASSERT_EQ(0, db_backend_insert(g_test_db, &rec1));

    /* Insert second record with same hash - should fail */
    FileRecord rec2;
    memset(&rec2, 0, sizeof(rec2));
    snprintf(rec2.original_file_name, DB_ORIG_NAME_LEN, "%s", "test2.jpg");
    rec2.file_size_bytes = 200;
    snprintf(rec2.saved_file_name, DB_FILENAME_LEN, "%s", "upload2.jpg");
    snprintf(rec2.file_hash, DB_HASH_LEN, "%s", "same_hash");
    rec2.is_ocr_processed = 0;
    int result = db_backend_insert(g_test_db, &rec2);
    ASSERT_EQ(-1, result); /* UNIQUE constraint violation */

    teardown_db();
    TEST_PASS();
}

/* Test mark OCR done */
TEST_CASE(db_mark_ocr_done) {
    setup_db();

    /* Insert record */
    FileRecord rec;
    memset(&rec, 0, sizeof(rec));
    snprintf(rec.original_file_name, DB_ORIG_NAME_LEN, "%s", "test.jpg");
    rec.file_size_bytes = 100;
    snprintf(rec.saved_file_name, DB_FILENAME_LEN, "%s", "upload.jpg");
    snprintf(rec.file_hash, DB_HASH_LEN, "%s", "hash123");
    rec.is_ocr_processed = 0;
    db_backend_insert(g_test_db, &rec);

    /* Mark OCR done */
    int result = db_backend_mark_ocr_done(g_test_db, rec.id, "upload_ocr.txt");
    ASSERT_EQ(0, result);

    /* Verify */
    FileRecord found;
    db_backend_find_by_hash(g_test_db, "hash123", &found);
    ASSERT_EQ(1, found.is_ocr_processed);
    ASSERT_STR_EQ("upload_ocr.txt", found.ocr_file_name);

    teardown_db();
    TEST_PASS();
}

/* Test mark parsing done */
TEST_CASE(db_mark_parsing_done) {
    setup_db();

    /* Insert record */
    FileRecord rec;
    memset(&rec, 0, sizeof(rec));
    snprintf(rec.original_file_name, DB_ORIG_NAME_LEN, "%s", "test.jpg");
    rec.file_size_bytes = 100;
    snprintf(rec.saved_file_name, DB_FILENAME_LEN, "%s", "upload.jpg");
    snprintf(rec.file_hash, DB_HASH_LEN, "%s", "hash123");
    rec.is_ocr_processed = 1;
    db_backend_insert(g_test_db, &rec);

    /* Mark parsing done */
    const char *json = "{\"store\":\"Test\",\"total\":10.00}";
    int result = db_backend_mark_parsing_done(g_test_db, rec.id, json);
    ASSERT_EQ(0, result);

    teardown_db();
    TEST_PASS();
}

/* Test get parsed receipt not found */
TEST_CASE(db_get_parsed_receipt_not_found) {
    setup_db();

    /* Insert record but don't parse */
    FileRecord rec;
    memset(&rec, 0, sizeof(rec));
    snprintf(rec.original_file_name, DB_ORIG_NAME_LEN, "%s", "test.jpg");
    rec.file_size_bytes = 100;
    snprintf(rec.saved_file_name, DB_FILENAME_LEN, "%s", "upload.jpg");
    snprintf(rec.file_hash, DB_HASH_LEN, "%s", "hash123");
    rec.is_ocr_processed = 1;
    db_backend_insert(g_test_db, &rec);

    /* Try to get parsed receipt */
    ParsedReceipt parsed;
    int result = db_backend_get_parsed_receipt(g_test_db, rec.id, &parsed);
    ASSERT_EQ(1, result); /* 1 = not found */

    teardown_db();
    TEST_PASS();
}

/* Test get parsed receipt */
TEST_CASE(db_get_parsed_receipt) {
    setup_db();

    /* Insert and parse record */
    FileRecord rec;
    memset(&rec, 0, sizeof(rec));
    snprintf(rec.original_file_name, DB_ORIG_NAME_LEN, "%s", "test.jpg");
    rec.file_size_bytes = 100;
    snprintf(rec.saved_file_name, DB_FILENAME_LEN, "%s", "upload.jpg");
    snprintf(rec.file_hash, DB_HASH_LEN, "%s", "hash123");
    rec.is_ocr_processed = 1;
    db_backend_insert(g_test_db, &rec);

    const char *json = "{\"store\":\"Test\",\"total\":10.00}";
    db_backend_mark_parsing_done(g_test_db, rec.id, json);

    /* Get parsed receipt */
    ParsedReceipt parsed;
    int result = db_backend_get_parsed_receipt(g_test_db, rec.id, &parsed);
    ASSERT_EQ(0, result);
    ASSERT_STR_EQ(json, parsed.parsed_json);

    teardown_db();
    TEST_PASS();
}

/* Test mark parsing done update */
TEST_CASE(db_mark_parsing_done_update) {
    setup_db();

    /* Insert and parse record */
    FileRecord rec;
    memset(&rec, 0, sizeof(rec));
    snprintf(rec.original_file_name, DB_ORIG_NAME_LEN, "%s", "test.jpg");
    rec.file_size_bytes = 100;
    snprintf(rec.saved_file_name, DB_FILENAME_LEN, "%s", "upload.jpg");
    snprintf(rec.file_hash, DB_HASH_LEN, "%s", "hash123");
    rec.is_ocr_processed = 1;
    db_backend_insert(g_test_db, &rec);

    const char *json1 = "{\"store\":\"Test\",\"total\":10.00}";
    db_backend_mark_parsing_done(g_test_db, rec.id, json1);

    /* Update with new JSON */
    const char *json2 = "{\"store\":\"Updated\",\"total\":20.00}";
    int result = db_backend_mark_parsing_done(g_test_db, rec.id, json2);
    ASSERT_EQ(0, result);

    /* Verify update */
    ParsedReceipt parsed;
    db_backend_get_parsed_receipt(g_test_db, rec.id, &parsed);
    ASSERT_STR_EQ(json2, parsed.parsed_json);

    teardown_db();
    TEST_PASS();
}

TEST_CASE(db_open_invalid_path) {
    Config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.db_backend = DB_BACKEND_SQLITE;
    snprintf(cfg.db_path, MAX_PATH_LEN, "/nonexistent/path/to/db.sqlite");

    DBBackend *db = db_backend_open(&cfg);
    ASSERT_TRUE(db == NULL);
    TEST_PASS();
}
TEST_CASE(db_find_by_hash_not_found) {
    setup_db();

    FileRecord found;
    int result = db_backend_find_by_hash(g_test_db, "nonexistent_hash_12345", &found);
    ASSERT_EQ(1, result);

    teardown_db();
    TEST_PASS();
}

static void count_cb(const FileRecord *r, void *ud) {
    (void)r;
    (*(int *)ud)++;
}

TEST_CASE(db_list_callback) {
    setup_db();

    FileRecord rec;
    memset(&rec, 0, sizeof(rec));
    snprintf(rec.original_file_name, DB_ORIG_NAME_LEN, "list_test.jpg");
    rec.file_size_bytes = 100;
    snprintf(rec.saved_file_name, DB_FILENAME_LEN, "upload_list.jpg");
    snprintf(rec.file_hash, DB_HASH_LEN, "listhash123");
    db_backend_insert(g_test_db, &rec);

    int count = 0;
    db_backend_list(g_test_db, count_cb, &count);
    ASSERT_TRUE(count >= 1);

    teardown_db();
    TEST_PASS();
}

TEST_CASE(db_close_null) {
    db_backend_close(NULL);
    TEST_PASS();
}

/* ── find_by_id tests ─────────────────────────────────────────────────────── */

TEST_CASE(db_find_by_id) {
    setup_db();

    FileRecord rec;
    memset(&rec, 0, sizeof(rec));
    snprintf(rec.original_file_name, DB_ORIG_NAME_LEN, "%s", "test.jpg");
    rec.file_size_bytes = 100;
    snprintf(rec.saved_file_name, DB_FILENAME_LEN, "%s", "upload.jpg");
    snprintf(rec.file_hash, DB_HASH_LEN, "%s", "hash_by_id");
    rec.is_ocr_processed = 0;
    ASSERT_EQ(0, db_backend_insert(g_test_db, &rec));
    int64_t inserted_id = rec.id;

    FileRecord found;
    int result = db_backend_find_by_id(g_test_db, inserted_id, &found);
    ASSERT_EQ(0, result);
    ASSERT_EQ(inserted_id, found.id);
    ASSERT_STR_EQ("test.jpg", found.original_file_name);
    ASSERT_EQ(100, found.file_size_bytes);

    teardown_db();
    TEST_PASS();
}

TEST_CASE(db_find_by_id_not_found) {
    setup_db();

    FileRecord found;
    int result = db_backend_find_by_id(g_test_db, 9999, &found);
    ASSERT_EQ(1, result); /* 1 = not found */

    teardown_db();
    TEST_PASS();
}

TEST_CASE(db_find_by_id_null) {
    FileRecord found;
    int result = db_backend_find_by_id(NULL, 1, &found);
    ASSERT_EQ(-1, result);
    TEST_PASS();
}

/* ── delete_file tests ────────────────────────────────────────────────────── */

TEST_CASE(db_delete_file) {
    setup_db();

    FileRecord rec;
    memset(&rec, 0, sizeof(rec));
    snprintf(rec.original_file_name, DB_ORIG_NAME_LEN, "%s", "to_delete.jpg");
    rec.file_size_bytes = 100;
    snprintf(rec.saved_file_name, DB_FILENAME_LEN, "%s", "upload_del.jpg");
    snprintf(rec.file_hash, DB_HASH_LEN, "%s", "hash_del");
    rec.is_ocr_processed = 0;
    ASSERT_EQ(0, db_backend_insert(g_test_db, &rec));
    int64_t id = rec.id;

    /* Delete it */
    int result = db_backend_delete_file(g_test_db, id);
    ASSERT_EQ(0, result);

    /* Verify it's gone */
    FileRecord found;
    ASSERT_EQ(1, db_backend_find_by_id(g_test_db, id, &found));

    teardown_db();
    TEST_PASS();
}

TEST_CASE(db_delete_file_not_found) {
    setup_db();

    int result = db_backend_delete_file(g_test_db, 9999);
    ASSERT_EQ(1, result); /* 1 = not found */

    teardown_db();
    TEST_PASS();
}

TEST_CASE(db_delete_file_cascades_receipt) {
    setup_db();

    /* Insert file */
    FileRecord rec;
    memset(&rec, 0, sizeof(rec));
    snprintf(rec.original_file_name, DB_ORIG_NAME_LEN, "%s", "cascade.jpg");
    rec.file_size_bytes = 100;
    snprintf(rec.saved_file_name, DB_FILENAME_LEN, "%s", "upload_cascade.jpg");
    snprintf(rec.file_hash, DB_HASH_LEN, "%s", "hash_cascade");
    rec.is_ocr_processed = 1;
    ASSERT_EQ(0, db_backend_insert(g_test_db, &rec));

    /* Add parsed receipt */
    const char *json = "{\"store\":\"Test\"}";
    ASSERT_EQ(0, db_backend_mark_parsing_done(g_test_db, rec.id, json));

    /* Verify receipt exists */
    ParsedReceipt parsed;
    ASSERT_EQ(0, db_backend_get_parsed_receipt(g_test_db, rec.id, &parsed));

    /* Delete file - should cascade to parsed_receipts */
    ASSERT_EQ(0, db_backend_delete_file(g_test_db, rec.id));

    /* Verify receipt is also gone */
    ASSERT_EQ(1, db_backend_get_parsed_receipt(g_test_db, rec.id, &parsed));

    teardown_db();
    TEST_PASS();
}

TEST_CASE(db_delete_file_null) {
    int result = db_backend_delete_file(NULL, 1);
    ASSERT_EQ(-1, result);
    TEST_PASS();
}
