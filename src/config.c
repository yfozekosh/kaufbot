#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *require_env(const char *key) {
    const char *v = getenv(key);
    if (!v || v[0] == '\0') {
        LOG_ERROR("required env var %s is not set", key);
        return NULL;
    }
    LOG_DEBUG("env var %s is set", key);
    return v;
}

static const char *env_or_default(const char *key, const char *def) {
    const char *v = getenv(key);
    if (v && v[0] != '\0') {
        LOG_DEBUG("env var %s is set", key);
        return v;
    }
    LOG_DEBUG("env var %s not set, using default", key);
    return def;
}

static int parse_user_ids(Config *cfg, const char *raw) {
    char buf[1024];
    snprintf(buf, sizeof(buf), "%s", raw);

    int count = 0;
    char *saveptr = NULL;
    char *token = strtok_r(buf, ",", &saveptr);
    while (token && count < MAX_ALLOWED_USERS) {
        while (*token == ' ')
            token++;

        char *end;
        int64_t id = (int64_t)strtoll(token, &end, 10);
        if (end == token) {
            LOG_ERROR("invalid user ID: %s", token);
            return -1;
        }
        cfg->allowed_users[count++] = id;
        token = strtok_r(NULL, ",", &saveptr);
    }
    LOG_INFO("parsed %d allowed user IDs", count);
    return count;
}

static int config_load_storage(Config *cfg) {
    const char *storage_backend = env_or_default("STORAGE_BACKEND", "local");
    if (strcmp(storage_backend, "supabase") == 0) {
#ifdef HAVE_POSTGRES
        cfg->storage_backend = STORAGE_BACKEND_SUPABASE;
        LOG_INFO("using Supabase storage backend");

        const char *sb_url = require_env("SUPABASE_URL");
        if (!sb_url)
            return -1;
        if (strncmp(sb_url, "http://", 7) != 0 && strncmp(sb_url, "https://", 8) != 0) {
            snprintf(cfg->supabase_url, MAX_URL_LEN, "https://%s", sb_url);
        } else {
            snprintf(cfg->supabase_url, MAX_URL_LEN, "%s", sb_url);
        }

        const char *sb_key = require_env("SUPABASE_ANON_KEY");
        if (!sb_key)
            return -1;
        snprintf(cfg->supabase_anon_key, MAX_TOKEN_LEN, "%s", sb_key);

        const char *sb_svc = getenv("SUPABASE_SERVICE_KEY");
        if (sb_svc && sb_svc[0] != '\0') {
            snprintf(cfg->supabase_service_key, MAX_TOKEN_LEN, "%s", sb_svc);
        } else {
            LOG_WARN("SUPABASE_SERVICE_KEY not set, using anon key (RLS applies)");
            snprintf(cfg->supabase_service_key, MAX_TOKEN_LEN, "%s", sb_key);
        }

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
    return 0;
}

static int config_load_database(Config *cfg) {
    const char *db_backend = env_or_default("DB_BACKEND", "sqlite");
    if (strcmp(db_backend, "postgres") == 0) {
#ifdef HAVE_POSTGRES
        cfg->db_backend = DB_BACKEND_POSTGRES;
        LOG_INFO("using PostgreSQL database backend");

        const char *pg_host = require_env("POSTGRES_HOST");
        if (!pg_host)
            return -1;
        snprintf(cfg->postgres_host, MAX_PATH_LEN, "%s", pg_host);

        snprintf(cfg->postgres_port, sizeof(cfg->postgres_port), "%s",
                 env_or_default("POSTGRES_PORT", "5432"));

        const char *pg_db = require_env("POSTGRES_DB");
        if (!pg_db)
            return -1;
        snprintf(cfg->postgres_db, MAX_PATH_LEN, "%s", pg_db);

        const char *pg_user = require_env("POSTGRES_USER");
        if (!pg_user)
            return -1;
        snprintf(cfg->postgres_user, MAX_PATH_LEN, "%s", pg_user);

        const char *pg_pass = require_env("POSTGRES_PASSWORD");
        if (!pg_pass)
            return -1;
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
        snprintf(cfg->db_path, MAX_PATH_LEN, "%s", env_or_default("DB_PATH", "/data/bot.db"));
    }
    return 0;
}

int config_load(Config *cfg) {
    LOG_INFO("loading configuration...");
    memset(cfg, 0, sizeof(*cfg));

    const char *token = require_env("TELEGRAM_TOKEN");
    if (!token)
        return -1;
    snprintf(cfg->telegram_token, MAX_TOKEN_LEN, "%s", token);

    const char *gemini_key = require_env("GEMINI_API_KEY");
    if (!gemini_key)
        return -1;
    snprintf(cfg->gemini_api_key, MAX_TOKEN_LEN, "%s", gemini_key);

    const char *users_raw = require_env("ALLOWED_USER_IDS");
    if (!users_raw)
        return -1;

    int count = parse_user_ids(cfg, users_raw);
    if (count <= 0) {
        LOG_ERROR("ALLOWED_USER_IDS must contain at least one valid ID");
        return -1;
    }
    cfg->allowed_users_count = count;

    snprintf(cfg->gemini_model, MAX_MODEL_LEN, "%s",
             env_or_default("GEMINI_MODEL", "gemini-2.5-flash"));

    snprintf(cfg->gemini_fallback_model, MAX_MODEL_LEN, "%s",
             env_or_default("GEMINI_FALLBACK_MODEL", "gemma-3-27b-it"));

    const char *fb_enabled = env_or_default("GEMINI_FALLBACK_ENABLED", "0");
    cfg->gemini_fallback_enabled =
        (strcmp(fb_enabled, "1") == 0 || strcmp(fb_enabled, "true") == 0);

    if (config_load_storage(cfg) != 0)
        return -1;
    if (config_load_database(cfg) != 0)
        return -1;

    /* Telegram API configuration */
    snprintf(cfg->telegram_api_base, MAX_URL_LEN, "%s",
             env_or_default("TELEGRAM_API_BASE", "https://api.telegram.org/bot"));
    snprintf(cfg->telegram_file_base, MAX_URL_LEN, "%s",
             env_or_default("TELEGRAM_FILE_BASE", "https://api.telegram.org/file/bot"));
    cfg->telegram_poll_timeout_secs =
        strtol(env_or_default("TELEGRAM_POLL_TIMEOUT", "30"), NULL, 10);
    cfg->telegram_http_timeout_secs =
        strtol(env_or_default("TELEGRAM_HTTP_TIMEOUT", "60"), NULL, 10);
    cfg->telegram_download_timeout_secs =
        strtol(env_or_default("TELEGRAM_DOWNLOAD_TIMEOUT", "120"), NULL, 10);
    cfg->telegram_reconnect_delay_secs =
        strtol(env_or_default("TELEGRAM_RECONNECT_DELAY", "5"), NULL, 10);
    cfg->telegram_retry_delay_secs = strtol(env_or_default("TELEGRAM_RETRY_DELAY", "2"), NULL, 10);

    const char *max_file_mb = env_or_default("MAX_FILE_MB", "20");
    cfg->max_file_size_bytes = (size_t)(strtol(max_file_mb, NULL, 10) * 1024 * 1024);

    /* Gemini API configuration */
    snprintf(cfg->gemini_api_base, GEMINI_URL_BUF_LEN, "%s",
             env_or_default("GEMINI_API_BASE",
                            "https://generativelanguage.googleapis.com/v1beta/models"));
    cfg->gemini_http_timeout_secs = strtol(env_or_default("GEMINI_HTTP_TIMEOUT", "600"), NULL, 10);
    cfg->gemini_connect_timeout_secs =
        strtol(env_or_default("GEMINI_CONNECT_TIMEOUT", "15"), NULL, 10);

    /* Parse available Gemini models from CSV env var */
    const char *models_csv = env_or_default(
        "GEMINI_MODELS", "gemma-3-12b-it,gemma-3-27b-it,gemini-2.0-flash,gemini-2.5-flash");
    cfg->gemini_model_count = 0;
    {
        char buf[GEMINI_MODELS_BUF];
        snprintf(buf, sizeof(buf), "%s", models_csv);
        char *saveptr = NULL;
        char *tok = strtok_r(buf, ",", &saveptr);
        while (tok && cfg->gemini_model_count < MAX_GEMINI_MODELS) {
            /* Trim leading whitespace */
            while (*tok == ' ' || *tok == '\t')
                tok++;
            /* Trim trailing whitespace */
            size_t len = strlen(tok);
            while (len > 0 && (tok[len - 1] == ' ' || tok[len - 1] == '\t'))
                tok[--len] = '\0';
            if (len > 0) {
                snprintf(cfg->gemini_models[cfg->gemini_model_count], GEMINI_MAX_MODEL_LEN, "%s",
                         tok);
                cfg->gemini_model_count++;
            }
            tok = strtok_r(NULL, ",", &saveptr);
        }
    }
    if (cfg->gemini_model_count == 0) {
        LOG_WARN("no Gemini models configured, using default");
        snprintf(cfg->gemini_models[0], GEMINI_MAX_MODEL_LEN, "%s", cfg->gemini_model);
        cfg->gemini_model_count = 1;
    }

    LOG_INFO("configuration loaded successfully");
    return 0;
}

int config_is_allowed(const Config *cfg, int64_t user_id) {
    for (int i = 0; i < cfg->allowed_users_count; i++) {
        if (cfg->allowed_users[i] == user_id)
            return 1;
    }
    return 0;
}
