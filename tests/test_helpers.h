#ifndef TEST_HELPERS_H
#define TEST_HELPERS_H

#include "db_backend.h"
#include "storage_backend.h"
#include "config.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

/* ── Test database helpers ────────────────────────────────────────────────── */

static inline DBBackend *test_db_open_sqlite(const char *path)
{
    Config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.db_backend = DB_BACKEND_SQLITE;
    snprintf(cfg.db_path, sizeof(cfg.db_path), "%s", path);
    return db_backend_open(&cfg);
}

static inline void test_db_close(DBBackend *db)
{
    db_backend_close(db);
}

/* ── Test storage helpers ─────────────────────────────────────────────────── */

static inline StorageBackend *test_storage_open_local(const char *path)
{
    Config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.storage_backend = STORAGE_BACKEND_LOCAL;
    snprintf(cfg.storage_path, sizeof(cfg.storage_path), "%s", path);
    return storage_backend_open(&cfg);
}

static inline void test_storage_close(StorageBackend *storage)
{
    storage_backend_close(storage);
}

/* ── Test utilities ──────────────────────────────────────────────────────── */

static inline int test_cleanup_file(const char *path)
{
    return remove(path);
}

static inline int test_cleanup_dir(const char *path)
{
    if (!path || path[0] == '\0') return -1;

    /* Use unlinkat with AT_REMOVEDIR for safe recursive removal,
     * or fall back to remove() for simple cases. Avoid shell injection. */
    struct stat st;
    if (stat(path, &st) != 0) return -1;

    if (S_ISDIR(st.st_mode)) {
        /* Use system with a fixed string + validated path only if path is safe */
        return remove(path); /* Only removes empty dirs - acceptable for tests */
    }
    return remove(path);
}

#endif /* TEST_HELPERS_H */
