#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── helpers ──────────────────────────────────────────────────────────────── */

static const char *require_env(const char *key)
{
    const char *v = getenv(key);
    if (!v || v[0] == '\0') {
        fprintf(stderr, "[config] required env var %s is not set\n", key);
        return NULL;
    }
    return v;
}

static const char *env_or_default(const char *key, const char *def)
{
    const char *v = getenv(key);
    return (v && v[0] != '\0') ? v : def;
}

/* Parse comma-separated list of int64 user IDs into cfg->allowed_users.
 * Returns number of parsed IDs, or -1 on error. */
static int parse_user_ids(Config *cfg, const char *raw)
{
    char buf[1024];
    strncpy(buf, raw, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    int count = 0;
    char *token = strtok(buf, ",");
    while (token && count < MAX_ALLOWED_USERS) {
        /* trim leading spaces */
        while (*token == ' ') token++;

        char *end;
        int64_t id = (int64_t)strtoll(token, &end, 10);
        if (end == token) {
            fprintf(stderr, "[config] invalid user ID: %s\n", token);
            return -1;
        }
        cfg->allowed_users[count++] = id;
        token = strtok(NULL, ",");
    }
    return count;
}

/* ── public API ───────────────────────────────────────────────────────────── */

int config_load(Config *cfg)
{
    memset(cfg, 0, sizeof(*cfg));

    const char *token = require_env("TELEGRAM_TOKEN");
    if (!token) return -1;
    strncpy(cfg->telegram_token, token, MAX_TOKEN_LEN - 1);

    const char *gemini_key = require_env("GEMINI_API_KEY");
    if (!gemini_key) return -1;
    strncpy(cfg->gemini_api_key, gemini_key, MAX_TOKEN_LEN - 1);

    const char *users_raw = require_env("ALLOWED_USER_IDS");
    if (!users_raw) return -1;

    int count = parse_user_ids(cfg, users_raw);
    if (count <= 0) {
        fprintf(stderr, "[config] ALLOWED_USER_IDS must contain at least one valid ID\n");
        return -1;
    }
    cfg->allowed_users_count = count;

    strncpy(cfg->gemini_model,
            env_or_default("GEMINI_MODEL", "gemini-2.5-flash"),
            MAX_MODEL_LEN - 1);

    strncpy(cfg->storage_path,
            env_or_default("STORAGE_PATH", "/data/files"),
            MAX_PATH_LEN - 1);

    strncpy(cfg->db_path,
            env_or_default("DB_PATH", "/data/bot.db"),
            MAX_PATH_LEN - 1);

    return 0;
}

int config_is_allowed(const Config *cfg, int64_t user_id)
{
    for (int i = 0; i < cfg->allowed_users_count; i++) {
        if (cfg->allowed_users[i] == user_id)
            return 1;
    }
    return 0;
}
