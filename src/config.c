#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── helpers ──────────────────────────────────────────────────────────────── */

static const char *require_env(const char *key)
{
    const char *v = getenv(key);
    if (!v || v[0] == '\0') {
        LOG_ERROR("required env var %s is not set", key);
        return NULL;
    }
    LOG_DEBUG("env var %s is set", key);
    return v;
}

static const char *env_or_default(const char *key, const char *def)
{
    const char *v = getenv(key);
    if (v && v[0] != '\0') {
        LOG_DEBUG("env var %s is set to: %s", key, v);
        return v;
    }
    LOG_DEBUG("env var %s not set, using default: %s", key, def);
    return def;
}

/* Parse comma-separated list of int64 user IDs into cfg->allowed_users.
 * Returns number of parsed IDs, or -1 on error. */
static int parse_user_ids(Config *cfg, const char *raw)
{
    LOG_DEBUG("parsing %d user IDs", (int)strlen(raw));
    
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
            LOG_ERROR("invalid user ID: %s", token);
            return -1;
        }
        cfg->allowed_users[count++] = id;
        LOG_DEBUG("parsed user ID #%d: %lld", count, (long long)id);
        token = strtok(NULL, ",");
    }
    LOG_INFO("parsed %d allowed user IDs", count);
    return count;
}

/* ── public API ───────────────────────────────────────────────────────────── */

int config_load(Config *cfg)
{
    LOG_INFO("loading configuration...");
    memset(cfg, 0, sizeof(*cfg));

    const char *token = require_env("TELEGRAM_TOKEN");
    if (!token) return -1;
    snprintf(cfg->telegram_token, MAX_TOKEN_LEN, "%s", token);
    LOG_DEBUG("telegram token loaded (length: %zu)", strlen(token));

    const char *gemini_key = require_env("GEMINI_API_KEY");
    if (!gemini_key) return -1;
    snprintf(cfg->gemini_api_key, MAX_TOKEN_LEN, "%s", gemini_key);
    LOG_DEBUG("gemini API key loaded (length: %zu)", strlen(gemini_key));

    const char *users_raw = require_env("ALLOWED_USER_IDS");
    if (!users_raw) return -1;

    int count = parse_user_ids(cfg, users_raw);
    if (count <= 0) {
        LOG_ERROR("ALLOWED_USER_IDS must contain at least one valid ID");
        return -1;
    }
    cfg->allowed_users_count = count;

    snprintf(cfg->gemini_model, MAX_MODEL_LEN, "%s",
            env_or_default("GEMINI_MODEL", "gemini-2.5-flash"));
    LOG_DEBUG("gemini model: %s", cfg->gemini_model);

    /* ── Storage backend configuration ───────────────────────────────────── */
    const char *storage_backend = env_or_default("STORAGE_BACKEND", "local");
    if (strcmp(storage_backend, "supabase") == 0) {
#ifdef HAVE_POSTGRES
        cfg->storage_backend = STORAGE_BACKEND_SUPABASE;
        LOG_INFO("using Supabase storage backend");
        
        const char *sb_url = require_env("SUPABASE_URL");
        if (!sb_url) return -1;
        snprintf(cfg->supabase_url, MAX_URL_LEN, "%s", sb_url);
        
        const char *sb_key = require_env("SUPABASE_ANON_KEY");
        if (!sb_key) return -1;
        snprintf(cfg->supabase_anon_key, MAX_TOKEN_LEN, "%s", sb_key);
        
        snprintf(cfg->supabase_bucket, MAX_PATH_LEN, "%s",
                env_or_default("SUPABASE_BUCKET", "receipts"));
#else
        LOG_ERROR("Supabase storage requested but not compiled in");
        return -1;
#endif
    } else {
        cfg->storage_backend = STORAGE_BACKEND_LOCAL;
        LOG_INFO("using local storage backend");
        snprintf(cfg->storage_path, MAX_PATH_LEN, "%s",
                env_or_default("STORAGE_PATH", "/data/files"));
    }
    LOG_DEBUG("storage path/bucket: %s", 
              cfg->storage_backend == STORAGE_BACKEND_LOCAL ? cfg->storage_path : cfg->supabase_bucket);

    /* ── Database backend configuration ───────────────────────────────────── */
    const char *db_backend = env_or_default("DB_BACKEND", "sqlite");
    if (strcmp(db_backend, "postgres") == 0) {
#ifdef HAVE_POSTGRES
        cfg->db_backend = DB_BACKEND_POSTGRES;
        LOG_INFO("using PostgreSQL database backend");
        
        const char *pg_host = require_env("POSTGRES_HOST");
        if (!pg_host) return -1;
        snprintf(cfg->postgres_host, MAX_PATH_LEN, "%s", pg_host);
        
        const char *pg_port = env_or_default("POSTGRES_PORT", "5432");
        snprintf(cfg->postgres_port, sizeof(cfg->postgres_port), "%s", pg_port);
        
        const char *pg_db = require_env("POSTGRES_DB");
        if (!pg_db) return -1;
        snprintf(cfg->postgres_db, MAX_PATH_LEN, "%s", pg_db);
        
        const char *pg_user = require_env("POSTGRES_USER");
        if (!pg_user) return -1;
        snprintf(cfg->postgres_user, MAX_PATH_LEN, "%s", pg_user);
        
        const char *pg_pass = require_env("POSTGRES_PASSWORD");
        if (!pg_pass) return -1;
        snprintf(cfg->postgres_password, MAX_TOKEN_LEN, "%s", pg_pass);
        
        snprintf(cfg->postgres_ssl_mode, sizeof(cfg->postgres_ssl_mode), "%s",
                env_or_default("POSTGRES_SSL_MODE", "require"));
#else
        LOG_ERROR("PostgreSQL backend requested but not compiled in");
        return -1;
#endif
    } else {
        cfg->db_backend = DB_BACKEND_SQLITE;
        LOG_INFO("using SQLite database backend");
        snprintf(cfg->db_path, MAX_PATH_LEN, "%s",
                env_or_default("DB_PATH", "/data/bot.db"));
    }
    LOG_DEBUG("database path/host: %s",
              cfg->db_backend == DB_BACKEND_SQLITE ? cfg->db_path : cfg->postgres_host);

    LOG_INFO("configuration loaded successfully");
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
