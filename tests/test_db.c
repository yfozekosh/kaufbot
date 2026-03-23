/* ── Database module tests ────────────────────────────────────────────────── */

#include "test_runner.h"
#include "db.h"
#include <string.h>
#include <unistd.h>

static DB *g_test_db = NULL;
static const char *g_test_db_path = "/tmp/kaufbot_test.db";

static void setup_db(void)
{
    system("rm -f /tmp/kaufbot_test.db");
    g_test_db = db_open(g_test_db_path);
    ASSERT_NOT_NULL(g_test_db);
}

static void teardown_db(void)
{
    if (g_test_db) {
        db_close(g_test_db);
        g_test_db = NULL;
    }
    system("rm -f /tmp/kaufbot_test.db");
}

/* Test database open/close */
TEST_CASE(db_open_close)
{
    setup_db();
    ASSERT_NOT_NULL(g_test_db);
    teardown_db();
    TEST_PASS();
}

TEST_CASE(db_open_existing)
{
    setup_db();
    db_close(g_test_db);
    g_test_db = NULL;
    
    /* Re-open the same database */
    g_test_db = db_open(g_test_db_path);
    ASSERT_NOT_NULL(g_test_db);
    teardown_db();
    TEST_PASS();
}

/* Test insert and find by hash */
TEST_CASE(db_insert_and_find)
{
    setup_db();
    
    FileRecord rec;
    memset(&rec, 0, sizeof(rec));
    strncpy(rec.original_file_name, "test_receipt.jpg", DB_ORIG_NAME_LEN - 1);
    rec.file_size_bytes = 12345;
    strncpy(rec.saved_file_name, "upload_2024-01-01_12_00_00.jpg", DB_FILENAME_LEN - 1);
    strncpy(rec.file_hash, "abc123def456789", DB_HASH_LEN - 1);
    rec.is_ocr_processed = 0;
    
    int result = db_insert(g_test_db, &rec);
    ASSERT_EQ(0, result);
    ASSERT_TRUE(rec.id > 0);
    
    /* Find by hash */
    FileRecord found;
    int found_result = db_find_by_hash(g_test_db, "abc123def456789", &found);
    ASSERT_EQ(0, found_result);
    ASSERT_STR_EQ("test_receipt.jpg", found.original_file_name);
    ASSERT_EQ(12345, found.file_size_bytes);
    ASSERT_EQ(0, found.is_ocr_processed);
    
    teardown_db();
    TEST_PASS();
}

TEST_CASE(db_find_not_found)
{
    setup_db();
    
    FileRecord rec;
    memset(&rec, 0, sizeof(rec));
    strncpy(rec.original_file_name, "test.jpg", DB_ORIG_NAME_LEN - 1);
    rec.file_size_bytes = 100;
    strncpy(rec.saved_file_name, "upload_test.jpg", DB_FILENAME_LEN - 1);
    strncpy(rec.file_hash, "unique_hash_xyz", DB_HASH_LEN - 1);
    
    db_insert(g_test_db, &rec);
    
    /* Search for non-existent hash */
    FileRecord found;
    int result = db_find_by_hash(g_test_db, "nonexistent_hash", &found);
    ASSERT_EQ(1, result); /* 1 means not found */
    
    teardown_db();
    TEST_PASS();
}

/* Test OCR mark done */
TEST_CASE(db_mark_ocr_done)
{
    setup_db();
    
    FileRecord rec;
    memset(&rec, 0, sizeof(rec));
    strncpy(rec.original_file_name, "ocr_test.jpg", DB_ORIG_NAME_LEN - 1);
    rec.file_size_bytes = 500;
    strncpy(rec.saved_file_name, "upload_ocr_test.jpg", DB_FILENAME_LEN - 1);
    strncpy(rec.file_hash, "ocr_hash_123", DB_HASH_LEN - 1);
    rec.is_ocr_processed = 0;
    
    db_insert(g_test_db, &rec);
    ASSERT_EQ(0, rec.is_ocr_processed);
    
    /* Mark OCR as done */
    int result = db_mark_ocr_done(g_test_db, rec.id, "ocr_result.txt");
    ASSERT_EQ(0, result);
    
    /* Verify */
    FileRecord updated;
    db_find_by_hash(g_test_db, "ocr_hash_123", &updated);
    ASSERT_EQ(1, updated.is_ocr_processed);
    ASSERT_STR_EQ("ocr_result.txt", updated.ocr_file_name);
    
    teardown_db();
    TEST_PASS();
}

/* Test parsed receipts */
TEST_CASE(db_mark_parsing_done)
{
    setup_db();
    
    /* First insert a file record */
    FileRecord rec;
    memset(&rec, 0, sizeof(rec));
    strncpy(rec.original_file_name, "parsed_test.jpg", DB_ORIG_NAME_LEN - 1);
    rec.file_size_bytes = 600;
    strncpy(rec.saved_file_name, "upload_parsed_test.jpg", DB_FILENAME_LEN - 1);
    strncpy(rec.file_hash, "parsed_hash_456", DB_HASH_LEN - 1);
    db_insert(g_test_db, &rec);
    
    /* Mark parsing as done */
    const char *json = "{\"store_information\":{\"name\":\"Test Store\"},\"total_sum\":42.50}";
    int result = db_mark_parsing_done(g_test_db, rec.id, json);
    ASSERT_EQ(0, result);
    
    teardown_db();
    TEST_PASS();
}

TEST_CASE(db_get_parsed_receipt)
{
    setup_db();
    
    /* Insert file record */
    FileRecord rec;
    memset(&rec, 0, sizeof(rec));
    strncpy(rec.original_file_name, "get_parsed_test.jpg", DB_ORIG_NAME_LEN - 1);
    rec.file_size_bytes = 700;
    strncpy(rec.saved_file_name, "upload_get_parsed_test.jpg", DB_FILENAME_LEN - 1);
    strncpy(rec.file_hash, "get_parsed_hash", DB_HASH_LEN - 1);
    db_insert(g_test_db, &rec);
    
    /* Mark parsing as done */
    const char *json = "{\"store\":{\"name\":\"REWE\"},\"total\":99.99}";
    db_mark_parsing_done(g_test_db, rec.id, json);
    
    /* Get parsed receipt */
    ParsedReceipt parsed;
    int result = db_get_parsed_receipt(g_test_db, rec.id, &parsed);
    ASSERT_EQ(0, result);
    ASSERT_TRUE(strstr(parsed.parsed_json, "REWE") != NULL);
    
    teardown_db();
    TEST_PASS();
}

TEST_CASE(db_get_parsed_receipt_not_found)
{
    setup_db();
    
    /* Insert file record but don't add parsed data */
    FileRecord rec;
    memset(&rec, 0, sizeof(rec));
    strncpy(rec.original_file_name, "no_parse_test.jpg", DB_ORIG_NAME_LEN - 1);
    rec.file_size_bytes = 800;
    strncpy(rec.saved_file_name, "upload_no_parse_test.jpg", DB_FILENAME_LEN - 1);
    strncpy(rec.file_hash, "no_parse_hash", DB_HASH_LEN - 1);
    db_insert(g_test_db, &rec);
    
    /* Try to get non-existent parsed receipt */
    ParsedReceipt parsed;
    int result = db_get_parsed_receipt(g_test_db, rec.id, &parsed);
    ASSERT_EQ(1, result); /* 1 means not found */
    
    teardown_db();
    TEST_PASS();
}

TEST_CASE(db_mark_parsing_done_update)
{
    setup_db();
    
    /* Insert file record */
    FileRecord rec;
    memset(&rec, 0, sizeof(rec));
    strncpy(rec.original_file_name, "update_test.jpg", DB_ORIG_NAME_LEN - 1);
    rec.file_size_bytes = 900;
    strncpy(rec.saved_file_name, "upload_update_test.jpg", DB_FILENAME_LEN - 1);
    strncpy(rec.file_hash, "update_hash", DB_HASH_LEN - 1);
    db_insert(g_test_db, &rec);
    
    /* First parse */
    const char *json1 = "{\"total\":10.00}";
    db_mark_parsing_done(g_test_db, rec.id, json1);
    
    /* Update parse */
    const char *json2 = "{\"total\":20.00}";
    db_mark_parsing_done(g_test_db, rec.id, json2);
    
    /* Verify update */
    ParsedReceipt parsed;
    db_get_parsed_receipt(g_test_db, rec.id, &parsed);
    ASSERT_TRUE(strstr(parsed.parsed_json, "20.00") != NULL);
    ASSERT_FALSE(strstr(parsed.parsed_json, "10.00") != NULL);
    
    teardown_db();
    TEST_PASS();
}

/* Test list */
static void list_counter_cb(const FileRecord *rec, void *userdata)
{
    (void)rec;
    int *count = (int *)userdata;
    (*count)++;
}

TEST_CASE(db_list)
{
    setup_db();
    
    /* Insert multiple records */
    for (int i = 0; i < 5; i++) {
        FileRecord rec;
        memset(&rec, 0, sizeof(rec));
        char name[64];
        snprintf(name, sizeof(name), "test_%d.jpg", i);
        char hash[64];
        snprintf(hash, sizeof(hash), "hash_%d", i);
        
        strncpy(rec.original_file_name, name, DB_ORIG_NAME_LEN - 1);
        rec.file_size_bytes = 100 * (i + 1);
        strncpy(rec.saved_file_name, name, DB_FILENAME_LEN - 1);
        strncpy(rec.file_hash, hash, DB_HASH_LEN - 1);
        db_insert(g_test_db, &rec);
    }
    
    /* List records */
    int count = 0;
    db_list(g_test_db, list_counter_cb, &count);
    
    ASSERT_EQ(5, count);
    
    teardown_db();
    TEST_PASS();
}

/* Test duplicate detection */
TEST_CASE(db_duplicate_hash)
{
    setup_db();
    
    /* Insert first record */
    FileRecord rec1;
    memset(&rec1, 0, sizeof(rec1));
    strncpy(rec1.original_file_name, "first.jpg", DB_ORIG_NAME_LEN - 1);
    rec1.file_size_bytes = 100;
    strncpy(rec1.saved_file_name, "upload_first.jpg", DB_FILENAME_LEN - 1);
    strncpy(rec1.file_hash, "duplicate_hash", DB_HASH_LEN - 1);
    db_insert(g_test_db, &rec1);
    
    /* Try to insert duplicate hash - should fail */
    FileRecord rec2;
    memset(&rec2, 0, sizeof(rec2));
    strncpy(rec2.original_file_name, "second.jpg", DB_ORIG_NAME_LEN - 1);
    rec2.file_size_bytes = 200;
    strncpy(rec2.saved_file_name, "upload_second.jpg", DB_FILENAME_LEN - 1);
    strncpy(rec2.file_hash, "duplicate_hash", DB_HASH_LEN - 1);
    int result = db_insert(g_test_db, &rec2);
    
    /* Insert should fail due to unique constraint */
    ASSERT_TRUE(result != 0);
    
    teardown_db();
    TEST_PASS();
}
