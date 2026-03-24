#include "test_runner.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* ── GrowBuf tests ─────────────────────────────────────────────────── */

TEST_CASE(growbuf_init_zero) {
    GrowBuf buf = {0};
    ASSERT_EQ(0, (int)buf.len);
    ASSERT_EQ(0, (int)buf.cap);
    growbuf_free(&buf);
    TEST_PASS();
}

TEST_CASE(growbuf_write_cb_basic) {
    GrowBuf buf = {0};
    const char *data = "hello";
    size_t written = growbuf_write_cb((void *)data, 1, 5, &buf);
    ASSERT_EQ(5, (int)written);
    ASSERT_EQ(5, (int)buf.len);
    ASSERT_STR_EQ("hello", buf.data);
    growbuf_free(&buf);
    TEST_PASS();
}

TEST_CASE(growbuf_write_cb_append) {
    GrowBuf buf = {0};
    growbuf_write_cb((void *)"hello", 1, 5, &buf);
    growbuf_write_cb((void *)" world", 1, 6, &buf);
    ASSERT_EQ(11, (int)buf.len);
    ASSERT_STR_EQ("hello world", buf.data);
    growbuf_free(&buf);
    TEST_PASS();
}

TEST_CASE(growbuf_write_cb_empty) {
    GrowBuf buf = {0};
    size_t written = growbuf_write_cb((void *)"", 1, 0, &buf);
    ASSERT_EQ(0, (int)written);
    ASSERT_EQ(0, (int)buf.len);
    growbuf_free(&buf);
    TEST_PASS();
}

TEST_CASE(growbuf_free_null) {
    growbuf_free(NULL);
    GrowBuf buf = {0};
    growbuf_free(&buf);
    ASSERT_EQ(0, (int)buf.len);
    ASSERT_EQ(0, (int)buf.cap);
    TEST_PASS();
}

/* ── Base64 tests ──────────────────────────────────────────────────── */

TEST_CASE(base64_empty) {
    char *result = base64_encode(NULL, 0);
    ASSERT_NOT_NULL(result);
    ASSERT_STR_EQ("", result);
    free(result);
    TEST_PASS();
}

TEST_CASE(base64_single_byte) {
    const uint8_t data[] = {0x00};
    char *result = base64_encode(data, 1);
    ASSERT_NOT_NULL(result);
    ASSERT_STR_EQ("AA==", result);
    free(result);
    TEST_PASS();
}

TEST_CASE(base64_two_bytes) {
    const uint8_t data[] = {0x66, 0x6F};
    char *result = base64_encode(data, 2);
    ASSERT_NOT_NULL(result);
    ASSERT_STR_EQ("Zm8=", result);
    free(result);
    TEST_PASS();
}

TEST_CASE(base64_three_bytes) {
    const uint8_t data[] = {0x66, 0x6F, 0x6F};
    char *result = base64_encode(data, 3);
    ASSERT_NOT_NULL(result);
    ASSERT_STR_EQ("Zm9v", result);
    free(result);
    TEST_PASS();
}

TEST_CASE(base64_hello_world) {
    const uint8_t *data = (const uint8_t *)"Hello, World!";
    char *result = base64_encode(data, 13);
    ASSERT_NOT_NULL(result);
    ASSERT_STR_EQ("SGVsbG8sIFdvcmxkIQ==", result);
    free(result);
    TEST_PASS();
}

TEST_CASE(base64_rfc4648_vectors) {
    /* RFC 4648 test vectors */
    const uint8_t *empty = (const uint8_t *)"";
    const uint8_t *f = (const uint8_t *)"f";
    const uint8_t *fo = (const uint8_t *)"fo";
    const uint8_t *foo = (const uint8_t *)"foo";
    const uint8_t *foob = (const uint8_t *)"foob";
    const uint8_t *fooba = (const uint8_t *)"fooba";
    const uint8_t *foobar = (const uint8_t *)"foobar";

    char *r;
    r = base64_encode(empty, 0);
    ASSERT_STR_EQ("", r);
    free(r);
    r = base64_encode(f, 1);
    ASSERT_STR_EQ("Zg==", r);
    free(r);
    r = base64_encode(fo, 2);
    ASSERT_STR_EQ("Zm8=", r);
    free(r);
    r = base64_encode(foo, 3);
    ASSERT_STR_EQ("Zm9v", r);
    free(r);
    r = base64_encode(foob, 4);
    ASSERT_STR_EQ("Zm9vYg==", r);
    free(r);
    r = base64_encode(fooba, 5);
    ASSERT_STR_EQ("Zm9vYmE=", r);
    free(r);
    r = base64_encode(foobar, 6);
    ASSERT_STR_EQ("Zm9vYmFy", r);
    free(r);
    TEST_PASS();
}

/* ── URL percent-encode tests ──────────────────────────────────────── */

TEST_CASE(url_encode_null) {
    char *result = url_percent_encode(NULL);
    ASSERT_TRUE(result == NULL);
    TEST_PASS();
}

TEST_CASE(url_encode_empty) {
    char *result = url_percent_encode("");
    ASSERT_NOT_NULL(result);
    ASSERT_STR_EQ("", result);
    free(result);
    TEST_PASS();
}

TEST_CASE(url_encode_unreserved) {
    char *result = url_percent_encode("hello_world-123.test~");
    ASSERT_NOT_NULL(result);
    ASSERT_STR_EQ("hello_world-123.test~", result);
    free(result);
    TEST_PASS();
}

TEST_CASE(url_encode_spaces) {
    char *result = url_percent_encode("hello world");
    ASSERT_NOT_NULL(result);
    ASSERT_STR_EQ("hello%20world", result);
    free(result);
    TEST_PASS();
}

TEST_CASE(url_encode_special_chars) {
    char *result = url_percent_encode("a&b=c?d#e");
    ASSERT_NOT_NULL(result);
    /* All special chars should be percent-encoded */
    ASSERT_TRUE(strchr(result, '%') != NULL);
    free(result);
    TEST_PASS();
}

TEST_CASE(url_encode_unicode) {
    const char *input = "caf\u00e9";
    char *result = url_percent_encode(input);
    ASSERT_NOT_NULL(result);
    free(result);
    TEST_PASS();
}

TEST_CASE(url_encode_slash) {
    char *result = url_percent_encode("path/to/file");
    ASSERT_NOT_NULL(result);
    ASSERT_STR_EQ("path%2Fto%2Ffile", result);
    free(result);
    TEST_PASS();
}

/* ── safe_remove_path tests ────────────────────────────────────────── */

TEST_CASE(safe_remove_null) {
    int result = safe_remove_path(NULL);
    ASSERT_EQ(-1, result);
    TEST_PASS();
}

TEST_CASE(safe_remove_empty) {
    int result = safe_remove_path("");
    ASSERT_EQ(-1, result);
    TEST_PASS();
}

TEST_CASE(safe_remove_nonexistent) {
    int result = safe_remove_path("/tmp/kaufbot_test_nonexistent_xyz_12345");
    ASSERT_EQ(-1, result);
    TEST_PASS();
}

TEST_CASE(safe_remove_file) {
    const char *path = "/tmp/kaufbot_test_remove_file.txt";
    FILE *f = fopen(path, "w");
    ASSERT_NOT_NULL(f);
    fprintf(f, "test");
    fclose(f);

    int result = safe_remove_path(path);
    ASSERT_EQ(0, result);
    TEST_PASS();
}

TEST_CASE(safe_remove_dir) {
    const char *dir = "/tmp/kaufbot_test_remove_dir";
    system("rm -rf /tmp/kaufbot_test_remove_dir");
    mkdir(dir, 0755);

    int result = safe_remove_path(dir);
    ASSERT_EQ(0, result);
    TEST_PASS();
}
