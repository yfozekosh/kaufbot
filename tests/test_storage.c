/* ── Storage module tests ─────────────────────────────────────────────────── */

#include "test_runner.h"
#include "test_helpers.h"
#include "storage.h"
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Test SHA-256 hash computation */
TEST_CASE(storage_sha256_empty)
{
    char hash[SHA256_HEX_LEN];
    const uint8_t empty[] = "";
    storage_sha256_hex(empty, 0, hash);
    /* SHA-256 of empty string */
    ASSERT_STR_EQ("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", hash);
    TEST_PASS();
}

TEST_CASE(storage_sha256_simple)
{
    char hash[SHA256_HEX_LEN];
    const uint8_t data[] = "hello";
    storage_sha256_hex(data, 5, hash);
    /* SHA-256 of "hello" */
    ASSERT_STR_EQ("2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824", hash);
    TEST_PASS();
}

TEST_CASE(storage_sha256_length)
{
    char hash[SHA256_HEX_LEN];
    const uint8_t data[] = "test data";
    storage_sha256_hex(data, strlen((char*)data), hash);
    /* Should be 64 hex chars + null terminator */
    ASSERT_EQ(64, strlen(hash));
    TEST_PASS();
}

/* Test filename generation */
TEST_CASE(storage_gen_filename_format)
{
    char filename[MAX_FILENAME];
    storage_gen_filename(".jpg", filename, sizeof(filename));
    /* Should start with "upload_" */
    ASSERT_TRUE(strncmp(filename, "upload_", 7) == 0);
    /* Should end with ".jpg" */
    ASSERT_TRUE(strstr(filename, ".jpg") != NULL);
    TEST_PASS();
}

TEST_CASE(storage_gen_filename_no_ext)
{
    char filename[MAX_FILENAME];
    storage_gen_filename(NULL, filename, sizeof(filename));
    /* Should still start with "upload_" */
    ASSERT_TRUE(strncmp(filename, "upload_", 7) == 0);
    TEST_PASS();
}

TEST_CASE(storage_gen_filename_empty_ext)
{
    char filename[MAX_FILENAME];
    storage_gen_filename("", filename, sizeof(filename));
    /* Should start with "upload_" */
    ASSERT_TRUE(strncmp(filename, "upload_", 7) == 0);
    TEST_PASS();
}

/* Test OCR filename generation */
TEST_CASE(storage_ocr_filename)
{
    char ocr_filename[MAX_FILENAME];
    storage_ocr_filename("upload_2024-01-01_12_00_00.jpg", ocr_filename, sizeof(ocr_filename));
    ASSERT_STR_EQ("upload_2024-01-01_12_00_00_ocr_result.txt", ocr_filename);
    TEST_PASS();
}

TEST_CASE(storage_ocr_filename_no_ext)
{
    char ocr_filename[MAX_FILENAME];
    storage_ocr_filename("upload_2024-01-01_12_00_00", ocr_filename, sizeof(ocr_filename));
    ASSERT_STR_EQ("upload_2024-01-01_12_00_00_ocr_result.txt", ocr_filename);
    TEST_PASS();
}

/* Test MIME type detection */
TEST_CASE(storage_mime_jpg)
{
    ASSERT_STR_EQ("image/jpeg", storage_mime_type("test.jpg"));
    TEST_PASS();
}

TEST_CASE(storage_mime_jpeg)
{
    ASSERT_STR_EQ("image/jpeg", storage_mime_type("test.jpeg"));
    TEST_PASS();
}

TEST_CASE(storage_mime_png)
{
    ASSERT_STR_EQ("image/png", storage_mime_type("test.png"));
    TEST_PASS();
}

TEST_CASE(storage_mime_pdf)
{
    ASSERT_STR_EQ("application/pdf", storage_mime_type("test.pdf"));
    TEST_PASS();
}

TEST_CASE(storage_mime_unknown)
{
    ASSERT_STR_EQ("application/octet-stream", storage_mime_type("test.xyz"));
    TEST_PASS();
}

TEST_CASE(storage_mime_no_ext)
{
    ASSERT_STR_EQ("application/octet-stream", storage_mime_type("testfile"));
    TEST_PASS();
}

TEST_CASE(storage_mime_case_insensitive)
{
    ASSERT_STR_EQ("image/png", storage_mime_type("test.PNG"));
    TEST_PASS();
}

/* Test directory creation */
TEST_CASE(storage_ensure_dirs_new)
{
    const char *test_path = "/tmp/kaufbot_test_new_dir/subdir";
    /* Clean up first if exists */
    system("rm -rf /tmp/kaufbot_test_new_dir");
    
    int result = storage_ensure_dirs(test_path);
    ASSERT_EQ(0, result);
    
    /* Verify directory exists */
    struct stat st;
    ASSERT_EQ(0, stat(test_path, &st));
    ASSERT_TRUE(S_ISDIR(st.st_mode));
    
    /* Cleanup */
    system("rm -rf /tmp/kaufbot_test_new_dir");
    TEST_PASS();
}

TEST_CASE(storage_ensure_dirs_existing)
{
    const char *test_path = "/tmp/kaufbot_test_existing";
    system("mkdir -p /tmp/kaufbot_test_existing");
    
    int result = storage_ensure_dirs(test_path);
    ASSERT_EQ(0, result);
    
    /* Cleanup */
    system("rm -rf /tmp/kaufbot_test_existing");
    TEST_PASS();
}

/* Test file save/load */
TEST_CASE(storage_save_and_read_file)
{
    const char *test_dir = "/tmp/kaufbot_save_test";
    const char *test_file = "test.bin";
    const uint8_t data[] = {0x48, 0x65, 0x6c, 0x6c, 0x6f}; /* "Hello" */
    
    system("rm -rf /tmp/kaufbot_save_test");
    system("mkdir -p /tmp/kaufbot_save_test");
    
    int result = storage_save_file(test_dir, test_file, data, sizeof(data));
    ASSERT_EQ(0, result);
    
    /* Read back and verify */
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", test_dir, test_file);
    FILE *f = fopen(path, "rb");
    ASSERT_NOT_NULL(f);
    
    uint8_t read_data[sizeof(data)];
    size_t read_len = fread(read_data, 1, sizeof(read_data), f);
    fclose(f);
    
    ASSERT_EQ(sizeof(data), read_len);
    ASSERT_TRUE(memcmp(data, read_data, sizeof(data)) == 0);
    
    /* Cleanup */
    system("rm -rf /tmp/kaufbot_save_test");
    TEST_PASS();
}

TEST_CASE(storage_save_and_read_text)
{
    const char *test_dir = "/tmp/kaufbot_text_test";
    const char *test_file = "test.txt";
    const char *text = "Hello, World!";
    
    system("rm -rf /tmp/kaufbot_text_test");
    system("mkdir -p /tmp/kaufbot_text_test");
    
    int result = storage_save_text(test_dir, test_file, text);
    ASSERT_EQ(0, result);
    
    /* Read back and verify */
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", test_dir, test_file);
    FILE *f = fopen(path, "r");
    ASSERT_NOT_NULL(f);
    
    char read_text[256];
    fgets(read_text, sizeof(read_text), f);
    fclose(f);
    
    /* Remove newline if present */
    size_t len = strlen(read_text);
    if (len > 0 && read_text[len-1] == '\n') {
        read_text[len-1] = '\0';
    }
    
    ASSERT_STR_EQ(text, read_text);
    
    /* Cleanup */
    system("rm -rf /tmp/kaufbot_text_test");
    TEST_PASS();
}
