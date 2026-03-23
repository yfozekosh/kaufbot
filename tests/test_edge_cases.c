/* ── Additional edge case and integration tests ──────────────────────────── */

#include "test_runner.h"
#include "storage.h"
#include "db.h"
#include "../third_party/cjson/cJSON.h"
#include <string.h>
#include <stdlib.h>

/* ── Storage edge cases ──────────────────────────────────────────────────── */

TEST_CASE(storage_sha256_binary_data)
{
    char hash[SHA256_HEX_LEN];
    /* Binary data with null bytes */
    const uint8_t data[] = {0x00, 0x01, 0x02, 0x03, 0xFF, 0xFE, 0xFD};
    storage_sha256_hex(data, sizeof(data), hash);
    
    /* Should produce valid 64-char hex */
    ASSERT_EQ(64, strlen(hash));
    
    /* Verify it's valid hex */
    for (int i = 0; i < 64; i++) {
        char c = hash[i];
        ASSERT_TRUE((c >= '0' && c <= '9') || 
                    (c >= 'a' && c <= 'f') ||
                    (c >= 'A' && c <= 'F'));
    }
    
    TEST_PASS();
}

TEST_CASE(storage_sha256_consistency)
{
    char hash1[SHA256_HEX_LEN];
    char hash2[SHA256_HEX_LEN];
    const uint8_t data[] = "consistent test";
    
    storage_sha256_hex(data, strlen((char*)data), hash1);
    storage_sha256_hex(data, strlen((char*)data), hash2);
    
    ASSERT_STR_EQ(hash1, hash2);
    TEST_PASS();
}

TEST_CASE(storage_mime_all_extensions)
{
    ASSERT_STR_EQ("image/jpeg", storage_mime_type("file.JPG"));
    ASSERT_STR_EQ("image/jpeg", storage_mime_type("file.Jpeg"));
    ASSERT_STR_EQ("image/png", storage_mime_type("file.Png"));
    ASSERT_STR_EQ("image/gif", storage_mime_type("file.GIF"));
    ASSERT_STR_EQ("image/webp", storage_mime_type("file.WebP"));
    ASSERT_STR_EQ("image/bmp", storage_mime_type("file.BMP"));
    ASSERT_STR_EQ("image/tiff", storage_mime_type("file.TIFF"));
    ASSERT_STR_EQ("image/tiff", storage_mime_type("file.Tif"));
    ASSERT_STR_EQ("application/pdf", storage_mime_type("file.PDF"));
    TEST_PASS();
}

TEST_CASE(storage_ocr_filename_long_name)
{
    char ocr_filename[MAX_FILENAME];
    /* Long filename with extension */
    const char *long_name = "upload_2024-01-01_12_00_00_very_long_name.jpg";
    storage_ocr_filename(long_name, ocr_filename, sizeof(ocr_filename));
    
    ASSERT_TRUE(strstr(ocr_filename, "_ocr_result.txt") != NULL);
    TEST_PASS();
}

TEST_CASE(storage_save_empty_file)
{
    const char *test_dir = "/tmp/kaufbot_empty_file";
    const char *test_file = "empty.bin";
    const uint8_t data[1] = {0};
    
    system("rm -rf /tmp/kaufbot_empty_file");
    system("mkdir -p /tmp/kaufbot_empty_file");
    
    /* Save with 0 length */
    int result = storage_save_file(test_dir, test_file, data, 0);
    ASSERT_EQ(0, result);
    
    /* Verify file exists and is empty */
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", test_dir, test_file);
    FILE *f = fopen(path, "rb");
    ASSERT_NOT_NULL(f);
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fclose(f);
    
    ASSERT_EQ(0, size);
    
    system("rm -rf /tmp/kaufbot_empty_file");
    TEST_PASS();
}

TEST_CASE(storage_save_large_file)
{
    const char *test_dir = "/tmp/kaufbot_large_file";
    const char *test_file = "large.bin";
    
    /* Create 1MB of data */
    size_t size = 1024 * 1024;
    uint8_t *data = malloc(size);
    ASSERT_NOT_NULL(data);
    memset(data, 0xAB, size);
    
    system("rm -rf /tmp/kaufbot_large_file");
    system("mkdir -p /tmp/kaufbot_large_file");
    
    int result = storage_save_file(test_dir, test_file, data, size);
    ASSERT_EQ(0, result);
    
    /* Verify file size */
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", test_dir, test_file);
    FILE *f = fopen(path, "rb");
    ASSERT_NOT_NULL(f);
    
    fseek(f, 0, SEEK_END);
    long actual_size = ftell(f);
    fclose(f);
    
    ASSERT_EQ((long)size, actual_size);
    
    free(data);
    system("rm -rf /tmp/kaufbot_large_file");
    TEST_PASS();
}

TEST_CASE(storage_text_empty)
{
    const char *test_dir = "/tmp/kaufbot_empty_text";
    const char *test_file = "empty.txt";
    
    system("rm -rf /tmp/kaufbot_empty_text");
    system("mkdir -p /tmp/kaufbot_empty_text");
    
    int result = storage_save_text(test_dir, test_file, "");
    ASSERT_EQ(0, result);
    
    system("rm -rf /tmp/kaufbot_empty_text");
    TEST_PASS();
}

TEST_CASE(storage_text_multiline)
{
    const char *test_dir = "/tmp/kaufbot_multiline";
    const char *test_file = "multiline.txt";
    const char *text = "Line 1\nLine 2\nLine 3\n";
    
    system("rm -rf /tmp/kaufbot_multiline");
    system("mkdir -p /tmp/kaufbot_multiline");
    
    int result = storage_save_text(test_dir, test_file, text);
    ASSERT_EQ(0, result);
    
    /* Read back */
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", test_dir, test_file);
    FILE *f = fopen(path, "r");
    ASSERT_NOT_NULL(f);
    
    char buffer[256];
    size_t read_len = fread(buffer, 1, sizeof(buffer) - 1, f);
    buffer[read_len] = '\0';
    fclose(f);
    
    ASSERT_STR_EQ(text, buffer);
    
    system("rm -rf /tmp/kaufbot_multiline");
    TEST_PASS();
}

/* ── Database edge cases ─────────────────────────────────────────────────── */

TEST_CASE(db_empty_filename)
{
    system("rm -f /tmp/kaufbot_edge.db");
    DB *db = db_open("/tmp/kaufbot_edge.db");
    ASSERT_NOT_NULL(db);
    
    FileRecord rec;
    memset(&rec, 0, sizeof(rec));
    strncpy(rec.original_file_name, "", DB_ORIG_NAME_LEN - 1);
    rec.file_size_bytes = 0;
    strncpy(rec.saved_file_name, "empty.jpg", DB_FILENAME_LEN - 1);
    strncpy(rec.file_hash, "empty_hash", DB_HASH_LEN - 1);
    
    int result = db_insert(db, &rec);
    ASSERT_EQ(0, result);
    
    db_close(db);
    system("rm -f /tmp/kaufbot_edge.db");
    TEST_PASS();
}

TEST_CASE(db_very_long_filename)
{
    system("rm -f /tmp/kaufbot_long.db");
    DB *db = db_open("/tmp/kaufbot_long.db");
    ASSERT_NOT_NULL(db);
    
    FileRecord rec;
    memset(&rec, 0, sizeof(rec));
    /* Fill with 'A's */
    memset(rec.original_file_name, 'A', DB_ORIG_NAME_LEN - 1);
    rec.original_file_name[DB_ORIG_NAME_LEN - 1] = '\0';
    rec.file_size_bytes = 1000;
    strncpy(rec.saved_file_name, "long.jpg", DB_FILENAME_LEN - 1);
    strncpy(rec.file_hash, "long_hash", DB_HASH_LEN - 1);
    
    int result = db_insert(db, &rec);
    ASSERT_EQ(0, result);
    
    /* Verify */
    FileRecord found;
    db_find_by_hash(db, "long_hash", &found);
    ASSERT_EQ(DB_ORIG_NAME_LEN - 1, strlen(found.original_file_name));
    
    db_close(db);
    system("rm -f /tmp/kaufbot_long.db");
    TEST_PASS();
}

TEST_CASE(db_zero_file_size)
{
    system("rm -f /tmp/kaufbot_zero.db");
    DB *db = db_open("/tmp/kaufbot_zero.db");
    ASSERT_NOT_NULL(db);
    
    FileRecord rec;
    memset(&rec, 0, sizeof(rec));
    strncpy(rec.original_file_name, "zero.bin", DB_ORIG_NAME_LEN - 1);
    rec.file_size_bytes = 0;
    strncpy(rec.saved_file_name, "zero.jpg", DB_FILENAME_LEN - 1);
    strncpy(rec.file_hash, "zero_hash", DB_HASH_LEN - 1);
    
    int result = db_insert(db, &rec);
    ASSERT_EQ(0, result);
    ASSERT_EQ(0, rec.file_size_bytes);
    
    db_close(db);
    system("rm -f /tmp/kaufbot_zero.db");
    TEST_PASS();
}

TEST_CASE(db_special_characters_in_filename)
{
    system("rm -f /tmp/kaufbot_special.db");
    DB *db = db_open("/tmp/kaufbot_special.db");
    ASSERT_NOT_NULL(db);
    
    FileRecord rec;
    memset(&rec, 0, sizeof(rec));
    strncpy(rec.original_file_name, "file with spaces & special (chars).jpg", DB_ORIG_NAME_LEN - 1);
    rec.file_size_bytes = 100;
    strncpy(rec.saved_file_name, "special.jpg", DB_FILENAME_LEN - 1);
    strncpy(rec.file_hash, "special_hash", DB_HASH_LEN - 1);
    
    int result = db_insert(db, &rec);
    ASSERT_EQ(0, result);
    
    /* Verify */
    FileRecord found;
    db_find_by_hash(db, "special_hash", &found);
    ASSERT_STR_EQ("file with spaces & special (chars).jpg", found.original_file_name);
    
    db_close(db);
    system("rm -f /tmp/kaufbot_special.db");
    TEST_PASS();
}

TEST_CASE(db_unicode_in_filename)
{
    system("rm -f /tmp/kaufbot_unicode.db");
    DB *db = db_open("/tmp/kaufbot_unicode.db");
    ASSERT_NOT_NULL(db);
    
    FileRecord rec;
    memset(&rec, 0, sizeof(rec));
    strncpy(rec.original_file_name, "файл_文件_ملف.jpg", DB_ORIG_NAME_LEN - 1);
    rec.file_size_bytes = 100;
    strncpy(rec.saved_file_name, "unicode.jpg", DB_FILENAME_LEN - 1);
    strncpy(rec.file_hash, "unicode_hash", DB_HASH_LEN - 1);
    
    int result = db_insert(db, &rec);
    ASSERT_EQ(0, result);
    
    /* Verify */
    FileRecord found;
    db_find_by_hash(db, "unicode_hash", &found);
    ASSERT_STR_EQ("файл_文件_ملف.jpg", found.original_file_name);
    
    db_close(db);
    system("rm -f /tmp/kaufbot_unicode.db");
    TEST_PASS();
}

TEST_CASE(db_multiple_parsed_receipts)
{
    system("rm -f /tmp/kaufbot_multi_parse.db");
    DB *db = db_open("/tmp/kaufbot_multi_parse.db");
    ASSERT_NOT_NULL(db);
    
    /* Insert multiple files */
    for (int i = 0; i < 5; i++) {
        FileRecord rec;
        memset(&rec, 0, sizeof(rec));
        char name[64], hash[64];
        snprintf(name, sizeof(name), "file_%d.jpg", i);
        snprintf(hash, sizeof(hash), "hash_%d", i);
        
        strncpy(rec.original_file_name, name, DB_ORIG_NAME_LEN - 1);
        rec.file_size_bytes = 100;
        strncpy(rec.saved_file_name, name, DB_FILENAME_LEN - 1);
        strncpy(rec.file_hash, hash, DB_HASH_LEN - 1);
        db_insert(db, &rec);
        
        /* Add parsed data */
        char json[128];
        snprintf(json, sizeof(json), "{\"total\":%d.00}", i * 10);
        db_mark_parsing_done(db, rec.id, json);
    }
    
    /* Verify all parsed receipts */
    for (int i = 0; i < 5; i++) {
        FileRecord rec;
        char hash[64];
        snprintf(hash, sizeof(hash), "hash_%d", i);
        db_find_by_hash(db, hash, &rec);
        
        ParsedReceipt parsed;
        int result = db_get_parsed_receipt(db, rec.id, &parsed);
        ASSERT_EQ(0, result);
        
        char expected[32];
        snprintf(expected, sizeof(expected), "{\"total\":%d.00}", i * 10);
        ASSERT_TRUE(strstr(parsed.parsed_json, expected) != NULL);
    }
    
    db_close(db);
    system("rm -f /tmp/kaufbot_multi_parse.db");
    TEST_PASS();
}

/* ── JSON edge cases ─────────────────────────────────────────────────────── */

TEST_CASE(json_empty_object)
{
    cJSON *json = cJSON_Parse("{}");
    ASSERT_NOT_NULL(json);
    ASSERT_TRUE(cJSON_IsObject(json));
    ASSERT_EQ(0, cJSON_GetArraySize(json));
    cJSON_Delete(json);
    TEST_PASS();
}

TEST_CASE(json_empty_array)
{
    cJSON *json = cJSON_Parse("[]");
    ASSERT_NOT_NULL(json);
    ASSERT_TRUE(cJSON_IsArray(json));
    ASSERT_EQ(0, cJSON_GetArraySize(json));
    cJSON_Delete(json);
    TEST_PASS();
}

TEST_CASE(json_deeply_nested)
{
    const char *json_str = "{\"a\":{\"b\":{\"c\":{\"d\":{\"e\":123}}}}}";
    cJSON *json = cJSON_Parse(json_str);
    ASSERT_NOT_NULL(json);
    
    cJSON *a = cJSON_GetObjectItem(json, "a");
    cJSON *b = cJSON_GetObjectItem(a, "b");
    cJSON *c = cJSON_GetObjectItem(b, "c");
    cJSON *d = cJSON_GetObjectItem(c, "d");
    cJSON *e = cJSON_GetObjectItem(d, "e");
    
    ASSERT_TRUE(cJSON_IsNumber(e));
    ASSERT_EQ(123, (int)e->valuedouble);
    
    cJSON_Delete(json);
    TEST_PASS();
}

TEST_CASE(json_boolean_values)
{
    const char *json_str = "{\"is_true\":true,\"is_false\":false}";
    cJSON *json = cJSON_Parse(json_str);
    ASSERT_NOT_NULL(json);
    
    cJSON *is_true = cJSON_GetObjectItem(json, "is_true");
    cJSON *is_false = cJSON_GetObjectItem(json, "is_false");
    
    ASSERT_TRUE(cJSON_IsTrue(is_true));
    ASSERT_TRUE(cJSON_IsFalse(is_false));
    
    cJSON_Delete(json);
    TEST_PASS();
}

TEST_CASE(json_number_types)
{
    const char *json_str = "{\"int\":42,\"negative\":-17,\"float\":3.14159,\"scientific\":1.23e10}";
    cJSON *json = cJSON_Parse(json_str);
    ASSERT_NOT_NULL(json);
    
    cJSON *int_val = cJSON_GetObjectItem(json, "int");
    cJSON *neg_val = cJSON_GetObjectItem(json, "negative");
    cJSON *float_val = cJSON_GetObjectItem(json, "float");
    cJSON *sci_val = cJSON_GetObjectItem(json, "scientific");
    
    ASSERT_TRUE(cJSON_IsNumber(int_val));
    ASSERT_TRUE(cJSON_IsNumber(neg_val));
    ASSERT_TRUE(cJSON_IsNumber(float_val));
    ASSERT_TRUE(cJSON_IsNumber(sci_val));
    
    cJSON_Delete(json);
    TEST_PASS();
}

TEST_CASE(json_array_iteration)
{
    const char *json_str = "[1,2,3,4,5]";
    cJSON *json = cJSON_Parse(json_str);
    ASSERT_NOT_NULL(json);
    
    int sum = 0;
    cJSON *item;
    cJSON_ArrayForEach(item, json) {
        sum += (int)item->valuedouble;
    }
    
    ASSERT_EQ(15, sum);
    cJSON_Delete(json);
    TEST_PASS();
}

TEST_CASE(json_special_characters_in_string)
{
    const char *json_str = "{\"text\":\"hello\\nworld\\ttab\\\"quote\\\\backslash\"}";
    cJSON *json = cJSON_Parse(json_str);
    ASSERT_NOT_NULL(json);
    
    cJSON *text = cJSON_GetObjectItem(json, "text");
    ASSERT_NOT_NULL(text);
    ASSERT_TRUE(strstr(text->valuestring, "hello") != NULL);
    
    cJSON_Delete(json);
    TEST_PASS();
}

TEST_CASE(json_receipt_validation)
{
    /* Valid complete receipt */
    const char *valid = 
        "{"
        "\"store_information\":{\"name\":\"REWE\",\"address\":\"Berlin\"},"
        "\"line_items\":[{\"original_name\":\"Milk\",\"price\":1.99,\"amount\":1}],"
        "\"total_sum\":1.99,"
        "\"number_of_items\":1,"
        "\"other\":{}"
        "}";
    
    cJSON *json = cJSON_Parse(valid);
    ASSERT_NOT_NULL(json);
    
    /* Validate structure */
    cJSON *store = cJSON_GetObjectItem(json, "store_information");
    cJSON *items = cJSON_GetObjectItem(json, "line_items");
    cJSON *total = cJSON_GetObjectItem(json, "total_sum");
    
    ASSERT_NOT_NULL(store);
    ASSERT_NOT_NULL(items);
    ASSERT_NOT_NULL(total);
    ASSERT_TRUE(cJSON_IsArray(items));
    ASSERT_TRUE(cJSON_IsNumber(total));
    
    cJSON_Delete(json);
    TEST_PASS();
}
