/* ── Config module tests ──────────────────────────────────────────────────── */

#include "config.h"
#include "test_runner.h"
#include <stdlib.h>
#include <string.h>

/* Helper to set environment variable for testing */
static void set_env(const char *key, const char *value) {
    char *buf = malloc(strlen(key) + strlen(value) + 2);
    sprintf(buf, "%s=%s", key, value);
    putenv(buf);
    /* Note: we leak buf here intentionally - putenv expects it to persist */
}

static void unset_env(const char *key) {
    unsetenv(key);
}

/* Test token validation */
TEST_CASE(config_required_token) {
    unset_env("TELEGRAM_TOKEN");
    set_env("GEMINI_API_KEY", "test_key");
    set_env("ALLOWED_USER_IDS", "12345");

    Config cfg;
    int result = config_load(&cfg);
    ASSERT_EQ(-1, result);

    TEST_PASS();
}

TEST_CASE(config_required_gemini_key) {
    set_env("TELEGRAM_TOKEN", "test_token");
    unset_env("GEMINI_API_KEY");
    set_env("ALLOWED_USER_IDS", "12345");

    Config cfg;
    int result = config_load(&cfg);
    ASSERT_EQ(-1, result);

    TEST_PASS();
}

TEST_CASE(config_required_user_ids) {
    set_env("TELEGRAM_TOKEN", "test_token");
    set_env("GEMINI_API_KEY", "test_key");
    unset_env("ALLOWED_USER_IDS");

    Config cfg;
    int result = config_load(&cfg);
    ASSERT_EQ(-1, result);

    TEST_PASS();
}

TEST_CASE(config_empty_token) {
    set_env("TELEGRAM_TOKEN", "");
    set_env("GEMINI_API_KEY", "test_key");
    set_env("ALLOWED_USER_IDS", "12345");

    Config cfg;
    int result = config_load(&cfg);
    ASSERT_EQ(-1, result);

    TEST_PASS();
}

TEST_CASE(config_valid_minimal) {
    set_env("TELEGRAM_TOKEN", "123456:ABC-DEF1234ghIkl-zyx57J2v1uJ");
    set_env("GEMINI_API_KEY", "test_api_key_12345");
    set_env("ALLOWED_USER_IDS", "123456789");
    unset_env("GEMINI_MODEL");
    unset_env("STORAGE_PATH");
    unset_env("DB_PATH");

    Config cfg;
    int result = config_load(&cfg);
    ASSERT_EQ(0, result);

    ASSERT_STR_EQ("123456:ABC-DEF1234ghIkl-zyx57J2v1uJ", cfg.telegram_token);
    ASSERT_STR_EQ("test_api_key_12345", cfg.gemini_api_key);
    ASSERT_EQ(1, cfg.allowed_users_count);
    ASSERT_EQ(123456789, cfg.allowed_users[0]);
    ASSERT_STR_EQ("gemini-2.5-flash", cfg.gemini_model);
    ASSERT_STR_EQ("/data/files", cfg.storage_path);
    ASSERT_STR_EQ("/data/bot.db", cfg.db_path);

    TEST_PASS();
}

TEST_CASE(config_multiple_user_ids) {
    set_env("TELEGRAM_TOKEN", "test_token");
    set_env("GEMINI_API_KEY", "test_key");
    set_env("ALLOWED_USER_IDS", "111,222,333,444,555");

    Config cfg;
    int result = config_load(&cfg);
    ASSERT_EQ(0, result);

    ASSERT_EQ(5, cfg.allowed_users_count);
    ASSERT_EQ(111, cfg.allowed_users[0]);
    ASSERT_EQ(222, cfg.allowed_users[1]);
    ASSERT_EQ(333, cfg.allowed_users[2]);
    ASSERT_EQ(444, cfg.allowed_users[3]);
    ASSERT_EQ(555, cfg.allowed_users[4]);

    TEST_PASS();
}

TEST_CASE(config_user_ids_with_spaces) {
    set_env("TELEGRAM_TOKEN", "test_token");
    set_env("GEMINI_API_KEY", "test_key");
    set_env("ALLOWED_USER_IDS", "111, 222, 333");

    Config cfg;
    int result = config_load(&cfg);
    ASSERT_EQ(0, result);

    ASSERT_EQ(3, cfg.allowed_users_count);
    ASSERT_EQ(111, cfg.allowed_users[0]);
    ASSERT_EQ(222, cfg.allowed_users[1]);
    ASSERT_EQ(333, cfg.allowed_users[2]);

    TEST_PASS();
}

TEST_CASE(config_custom_paths) {
    set_env("TELEGRAM_TOKEN", "test_token");
    set_env("GEMINI_API_KEY", "test_key");
    set_env("ALLOWED_USER_IDS", "123");
    set_env("STORAGE_PATH", "/custom/storage");
    set_env("DB_PATH", "/custom/bot.db");
    set_env("GEMINI_MODEL", "gemini-pro");

    Config cfg;
    int result = config_load(&cfg);
    ASSERT_EQ(0, result);

    ASSERT_STR_EQ("/custom/storage", cfg.storage_path);
    ASSERT_STR_EQ("/custom/bot.db", cfg.db_path);
    ASSERT_STR_EQ("gemini-pro", cfg.gemini_model);

    TEST_PASS();
}

TEST_CASE(config_is_allowed_positive) {
    set_env("TELEGRAM_TOKEN", "test_token");
    set_env("GEMINI_API_KEY", "test_key");
    set_env("ALLOWED_USER_IDS", "100,200,300");

    Config cfg;
    config_load(&cfg);

    ASSERT_EQ(1, config_is_allowed(&cfg, 100));
    ASSERT_EQ(1, config_is_allowed(&cfg, 200));
    ASSERT_EQ(1, config_is_allowed(&cfg, 300));

    TEST_PASS();
}

TEST_CASE(config_is_allowed_negative) {
    set_env("TELEGRAM_TOKEN", "test_token");
    set_env("GEMINI_API_KEY", "test_key");
    set_env("ALLOWED_USER_IDS", "100,200,300");

    Config cfg;
    config_load(&cfg);

    ASSERT_EQ(0, config_is_allowed(&cfg, 999));
    ASSERT_EQ(0, config_is_allowed(&cfg, 0));
    ASSERT_EQ(0, config_is_allowed(&cfg, -1));

    TEST_PASS();
}

TEST_CASE(config_invalid_user_id) {
    set_env("TELEGRAM_TOKEN", "test_token");
    set_env("GEMINI_API_KEY", "test_key");
    set_env("ALLOWED_USER_IDS", "abc");

    Config cfg;
    int result = config_load(&cfg);
    ASSERT_EQ(-1, result);

    TEST_PASS();
}

TEST_CASE(config_mixed_valid_invalid_user_ids) {
    set_env("TELEGRAM_TOKEN", "test_token");
    set_env("GEMINI_API_KEY", "test_key");
    set_env("ALLOWED_USER_IDS", "100,invalid,200");

    Config cfg;
    int result = config_load(&cfg);
    /* Should fail on invalid ID */
    ASSERT_TRUE(result == -1);

    TEST_PASS();
}

TEST_CASE(config_negative_user_id) {
    set_env("TELEGRAM_TOKEN", "test_token");
    set_env("GEMINI_API_KEY", "test_key");
    set_env("ALLOWED_USER_IDS", "-123");

    Config cfg;
    int result = config_load(&cfg);
    /* Negative IDs are valid int64 */
    ASSERT_EQ(0, result);
    ASSERT_EQ(-123, cfg.allowed_users[0]);

    TEST_PASS();
}

TEST_CASE(config_large_user_id) {
    set_env("TELEGRAM_TOKEN", "test_token");
    set_env("GEMINI_API_KEY", "test_key");
    set_env("ALLOWED_USER_IDS", "9223372036854775807"); /* INT64_MAX */

    Config cfg;
    int result = config_load(&cfg);
    ASSERT_EQ(0, result);
    ASSERT_EQ(9223372036854775807LL, cfg.allowed_users[0]);

    TEST_PASS();
}

TEST_CASE(config_token_truncation) {
    /* Create a very long token */
    char long_token[MAX_TOKEN_LEN + 100];
    memset(long_token, 'A', sizeof(long_token) - 1);
    long_token[sizeof(long_token) - 1] = '\0';

    set_env("TELEGRAM_TOKEN", long_token);
    set_env("GEMINI_API_KEY", "test_key");
    set_env("ALLOWED_USER_IDS", "123");

    Config cfg;
    int result = config_load(&cfg);
    ASSERT_EQ(0, result);

    /* Token should be truncated to MAX_TOKEN_LEN - 1 */
    ASSERT_EQ(MAX_TOKEN_LEN - 1, strlen(cfg.telegram_token));

    TEST_PASS();
}
