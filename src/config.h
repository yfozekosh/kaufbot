#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>
#include <stdio.h>

/* ── Logging macros ───────────────────────────────────────────────────────── */

#ifndef LOG_LEVEL
#define LOG_LEVEL 0  /* 0=DEBUG, 1=INFO, 2=WARN, 3=ERROR */
#endif

#define LOG_DEBUG(fmt, ...) \
    do { if (LOG_LEVEL <= 0) fprintf(stderr, "[DEBUG] " fmt "\n", ##__VA_ARGS__); } while (0)

#define LOG_INFO(fmt, ...) \
    do { if (LOG_LEVEL <= 1) fprintf(stderr, "[INFO]  " fmt "\n", ##__VA_ARGS__); } while (0)

#define LOG_WARN(fmt, ...) \
    do { if (LOG_LEVEL <= 2) fprintf(stderr, "[WARN]  " fmt "\n", ##__VA_ARGS__); } while (0)

#define LOG_ERROR(fmt, ...) \
    do { if (LOG_LEVEL <= 3) fprintf(stderr, "[ERROR] " fmt "\n", ##__VA_ARGS__); } while (0)

/* ── Constants ────────────────────────────────────────────────────────────── */

#define MAX_ALLOWED_USERS 32
#define MAX_PATH_LEN      512
#define MAX_TOKEN_LEN     256
#define MAX_MODEL_LEN     128

typedef struct {
    char   telegram_token[MAX_TOKEN_LEN];
    char   gemini_api_key[MAX_TOKEN_LEN];
    char   gemini_model[MAX_MODEL_LEN];
    char   storage_path[MAX_PATH_LEN];
    char   db_path[MAX_PATH_LEN];
    int64_t allowed_users[MAX_ALLOWED_USERS];
    int    allowed_users_count;
} Config;

/* Load config from environment variables.
 * Returns 0 on success, -1 on error (missing required vars).
 * Prints error to stderr. */
int  config_load(Config *cfg);

/* Returns 1 if user_id is in the whitelist, 0 otherwise. */
int  config_is_allowed(const Config *cfg, int64_t user_id);

#endif /* CONFIG_H */
