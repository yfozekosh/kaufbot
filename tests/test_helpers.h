#ifndef TEST_HELPERS_H
#define TEST_HELPERS_H

#include "db_backend.h"
#include "storage_backend.h"
#include "config.h"

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
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", path);
    return system(cmd);
}

#endif /* TEST_HELPERS_H */
