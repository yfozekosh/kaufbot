/* ── Additional edge case and integration tests ──────────────────────────── */

#include "../third_party/cjson/cJSON.h"
#include "db_backend.h"
#include "storage.h"
#include "test_helpers.h"
#include "test_runner.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* ── Storage edge cases ──────────────────────────────────────────────────── */

TEST_CASE(storage_sha256_binary_data) {
    char hash[SHA256_HEX_LEN];
    const uint8_t data[] = {0x00, 0x01, 0x02, 0x03, 0xFF, 0xFE, 0xFD};
    storage_sha256_hex(data, sizeof(data), hash);

    ASSERT_EQ(64, strlen(hash));

    for (int i = 0; i < 64; i++) {
        char c = hash[i];
        ASSERT_TRUE((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'));
    }

    TEST_PASS();
}

TEST_CASE(storage_sha256_consistency) {
    char hash1[SHA256_HEX_LEN];
    char hash2[SHA256_HEX_LEN];
    const uint8_t data[] = "consistent test";

    storage_sha256_hex(data, strlen((char *)data), hash1);
    storage_sha256_hex(data, strlen((char *)data), hash2);

    ASSERT_STR_EQ(hash1, hash2);
    TEST_PASS();
}

TEST_CASE(storage_mime_all_extensions) {
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

TEST_CASE(storage_ocr_filename_long_name) {
    char ocr_filename[MAX_FILENAME];
    const char *long_name = "upload_2024-01-01_12_00_00_very_long_name.jpg";
    storage_ocr_filename(long_name, ocr_filename, sizeof(ocr_filename));

    ASSERT_TRUE(strstr(ocr_filename, "_ocr_result.txt") != NULL);
    TEST_PASS();
}

TEST_CASE(storage_save_empty_file) {
    const char *test_dir = "/tmp/kaufbot_empty_file";
    const char *test_file = "empty.bin";
    const uint8_t data[1] = {0};

    system("rm -rf /tmp/kaufbot_empty_file");
    system("mkdir -p /tmp/kaufbot_empty_file");

    int result = storage_save_file(test_dir, test_file, data, 0);
    ASSERT_EQ(0, result);

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

TEST_CASE(storage_save_large_file) {
    const char *test_dir = "/tmp/kaufbot_large_file";
    const char *test_file = "large.bin";

    size_t size = 1024 * 1024;
    uint8_t *data = malloc(size);
    ASSERT_NOT_NULL(data);
    memset(data, 0xAB, size);

    system("rm -rf /tmp/kaufbot_large_file");
    system("mkdir -p /tmp/kaufbot_large_file");

    int result = storage_save_file(test_dir, test_file, data, size);
    ASSERT_EQ(0, result);

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

TEST_CASE(storage_text_empty) {
    const char *test_dir = "/tmp/kaufbot_empty_text";
    const char *test_file = "empty.txt";

    system("rm -rf /tmp/kaufbot_empty_text");
    system("mkdir -p /tmp/kaufbot_empty_text");

    int result = storage_save_text(test_dir, test_file, "");
    ASSERT_EQ(0, result);

    system("rm -rf /tmp/kaufbot_empty_text");
    TEST_PASS();
}

TEST_CASE(storage_text_multiline) {
    const char *test_dir = "/tmp/kaufbot_multiline";
    const char *test_file = "multiline.txt";
    const char *text = "Line 1\nLine 2\nLine 3\n";

    system("rm -rf /tmp/kaufbot_multiline");
    system("mkdir -p /tmp/kaufbot_multiline");

    int result = storage_save_text(test_dir, test_file, text);
    ASSERT_EQ(0, result);

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

/* ── Database edge cases (using backend API) ─────────────────────────────── */

static DBBackend *open_test_db(const char *path) {
    system("rm -f /tmp/kaufbot_edge.db");
    system("rm -f /tmp/kaufbot_long.db");
    system("rm -f /tmp/kaufbot_zero.db");
    system("rm -f /tmp/kaufbot_special.db");
    system("rm -f /tmp/kaufbot_unicode.db");
    system("rm -f /tmp/kaufbot_multi_parse.db");
    return test_db_open_sqlite(path);
}

TEST_CASE(db_empty_filename) {
    DBBackend *db = open_test_db("/tmp/kaufbot_edge.db");
    ASSERT_NOT_NULL(db);

    FileRecord rec;
    memset(&rec, 0, sizeof(rec));
    snprintf(rec.original_file_name, DB_ORIG_NAME_LEN, "%s", "");
    rec.file_size_bytes = 0;
    snprintf(rec.saved_file_name, DB_FILENAME_LEN, "%s", "empty.jpg");
    snprintf(rec.file_hash, DB_HASH_LEN, "%s", "empty_hash");

    int result = db_backend_insert(db, &rec);
    ASSERT_EQ(0, result);

    test_db_close(db);
    system("rm -f /tmp/kaufbot_edge.db");
    TEST_PASS();
}

TEST_CASE(db_very_long_filename) {
    DBBackend *db = open_test_db("/tmp/kaufbot_long.db");
    ASSERT_NOT_NULL(db);

    FileRecord rec;
    memset(&rec, 0, sizeof(rec));
    memset(rec.original_file_name, 'A', DB_ORIG_NAME_LEN - 1);
    rec.original_file_name[DB_ORIG_NAME_LEN - 1] = '\0';
    rec.file_size_bytes = 1000;
    snprintf(rec.saved_file_name, DB_FILENAME_LEN, "%s", "long.jpg");
    snprintf(rec.file_hash, DB_HASH_LEN, "%s", "long_hash");

    int result = db_backend_insert(db, &rec);
    ASSERT_EQ(0, result);

    FileRecord found;
    db_backend_find_by_hash(db, "long_hash", &found);
    ASSERT_EQ(DB_ORIG_NAME_LEN - 1, (int)strlen(found.original_file_name));

    test_db_close(db);
    system("rm -f /tmp/kaufbot_long.db");
    TEST_PASS();
}

TEST_CASE(db_zero_file_size) {
    DBBackend *db = open_test_db("/tmp/kaufbot_zero.db");
    ASSERT_NOT_NULL(db);

    FileRecord rec;
    memset(&rec, 0, sizeof(rec));
    snprintf(rec.original_file_name, DB_ORIG_NAME_LEN, "%s", "zero.bin");
    rec.file_size_bytes = 0;
    snprintf(rec.saved_file_name, DB_FILENAME_LEN, "%s", "zero.jpg");
    snprintf(rec.file_hash, DB_HASH_LEN, "%s", "zero_hash");

    int result = db_backend_insert(db, &rec);
    ASSERT_EQ(0, result);
    ASSERT_EQ(0, rec.file_size_bytes);

    test_db_close(db);
    system("rm -f /tmp/kaufbot_zero.db");
    TEST_PASS();
}

TEST_CASE(db_special_characters_in_filename) {
    DBBackend *db = open_test_db("/tmp/kaufbot_special.db");
    ASSERT_NOT_NULL(db);

    FileRecord rec;
    memset(&rec, 0, sizeof(rec));
    snprintf(rec.original_file_name, DB_ORIG_NAME_LEN, "%s",
             "file with spaces & special (chars).jpg");
    rec.file_size_bytes = 100;
    snprintf(rec.saved_file_name, DB_FILENAME_LEN, "%s", "special.jpg");
    snprintf(rec.file_hash, DB_HASH_LEN, "%s", "special_hash");

    int result = db_backend_insert(db, &rec);
    ASSERT_EQ(0, result);

    FileRecord found;
    db_backend_find_by_hash(db, "special_hash", &found);
    ASSERT_STR_EQ("file with spaces & special (chars).jpg", found.original_file_name);

    test_db_close(db);
    system("rm -f /tmp/kaufbot_special.db");
    TEST_PASS();
}

TEST_CASE(db_unicode_in_filename) {
    DBBackend *db = open_test_db("/tmp/kaufbot_unicode.db");
    ASSERT_NOT_NULL(db);

    FileRecord rec;
    memset(&rec, 0, sizeof(rec));
    snprintf(
        rec.original_file_name, DB_ORIG_NAME_LEN, "%s",
        "\xd1\x84\xd0\xb0\xd0\xb9\xd0\xbb_\xe6\x96\x87\xe4\xbb\xb6_\xd9\x85\xd9\x84\xd9\x81.jpg");
    rec.file_size_bytes = 100;
    snprintf(rec.saved_file_name, DB_FILENAME_LEN, "%s", "unicode.jpg");
    snprintf(rec.file_hash, DB_HASH_LEN, "%s", "unicode_hash");

    int result = db_backend_insert(db, &rec);
    ASSERT_EQ(0, result);

    FileRecord found;
    db_backend_find_by_hash(db, "unicode_hash", &found);
    ASSERT_EQ(0, strncmp(found.original_file_name, rec.original_file_name, DB_ORIG_NAME_LEN));

    test_db_close(db);
    system("rm -f /tmp/kaufbot_unicode.db");
    TEST_PASS();
}

TEST_CASE(db_multiple_parsed_receipts) {
    DBBackend *db = open_test_db("/tmp/kaufbot_multi_parse.db");
    ASSERT_NOT_NULL(db);

    for (int i = 0; i < 5; i++) {
        FileRecord rec;
        memset(&rec, 0, sizeof(rec));
        char name[64], hash[64];
        snprintf(name, sizeof(name), "file_%d.jpg", i);
        snprintf(hash, sizeof(hash), "hash_%d", i);

        snprintf(rec.original_file_name, DB_ORIG_NAME_LEN, "%s", name);
        rec.file_size_bytes = 100;
        snprintf(rec.saved_file_name, DB_FILENAME_LEN, "%s", name);
        snprintf(rec.file_hash, DB_HASH_LEN, "%s", hash);
        db_backend_insert(db, &rec);

        char json[128];
        snprintf(json, sizeof(json), "{\"total\":%d.00}", i * 10);
        db_backend_mark_parsing_done(db, rec.id, json);
    }

    for (int i = 0; i < 5; i++) {
        FileRecord rec;
        char hash[64];
        snprintf(hash, sizeof(hash), "hash_%d", i);
        db_backend_find_by_hash(db, hash, &rec);

        ParsedReceipt parsed;
        int result = db_backend_get_parsed_receipt(db, rec.id, &parsed);
        ASSERT_EQ(0, result);

        char expected[32];
        snprintf(expected, sizeof(expected), "{\"total\":%d.00}", i * 10);
        ASSERT_TRUE(strstr(parsed.parsed_json, expected) != NULL);
    }

    test_db_close(db);
    system("rm -f /tmp/kaufbot_multi_parse.db");
    TEST_PASS();
}

/* ── JSON edge cases ─────────────────────────────────────────────────────── */

TEST_CASE(json_empty_object) {
    cJSON *json = cJSON_Parse("{}");
    ASSERT_NOT_NULL(json);
    ASSERT_TRUE(cJSON_IsObject(json));
    ASSERT_EQ(0, cJSON_GetArraySize(json));
    cJSON_Delete(json);
    TEST_PASS();
}

TEST_CASE(json_empty_array) {
    cJSON *json = cJSON_Parse("[]");
    ASSERT_NOT_NULL(json);
    ASSERT_TRUE(cJSON_IsArray(json));
    ASSERT_EQ(0, cJSON_GetArraySize(json));
    cJSON_Delete(json);
    TEST_PASS();
}

TEST_CASE(json_deeply_nested) {
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

TEST_CASE(json_boolean_values) {
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

TEST_CASE(json_number_types) {
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

TEST_CASE(json_array_iteration) {
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

TEST_CASE(json_special_characters_in_string) {
    const char *json_str = "{\"text\":\"hello\\nworld\\ttab\\\"quote\\\\backslash\"}";
    cJSON *json = cJSON_Parse(json_str);
    ASSERT_NOT_NULL(json);

    cJSON *text = cJSON_GetObjectItem(json, "text");
    ASSERT_NOT_NULL(text);
    ASSERT_TRUE(strstr(text->valuestring, "hello") != NULL);

    cJSON_Delete(json);
    TEST_PASS();
}

TEST_CASE(json_receipt_validation) {
    const char *valid = "{"
                        "\"store_information\":{\"name\":\"REWE\",\"address\":\"Berlin\"},"
                        "\"line_items\":[{\"original_name\":\"Milk\",\"price\":1.99,\"amount\":1}],"
                        "\"total_sum\":1.99,"
                        "\"number_of_items\":1,"
                        "\"other\":{}"
                        "}";

    cJSON *json = cJSON_Parse(valid);
    ASSERT_NOT_NULL(json);

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
