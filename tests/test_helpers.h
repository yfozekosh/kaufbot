#ifndef TEST_HELPERS_H
#define TEST_HELPERS_H

#include "config.h"
#include "db_backend.h"
#include "storage_backend.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* ── Test database helpers ────────────────────────────────────────────────── */

static inline DBBackend *test_db_open_sqlite(const char *path) {
    Config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.db_backend = DB_BACKEND_SQLITE;
    snprintf(cfg.db_path, sizeof(cfg.db_path), "%s", path);
    return db_backend_open(&cfg);
}

static inline void test_db_close(DBBackend *db) {
    db_backend_close(db);
}

/* ── Test storage helpers ─────────────────────────────────────────────────── */

static inline StorageBackend *test_storage_open_local(const char *path) {
    Config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.storage_backend = STORAGE_BACKEND_LOCAL;
    snprintf(cfg.storage_path, sizeof(cfg.storage_path), "%s", path);
    return storage_backend_open(&cfg);
}

static inline void test_storage_close(StorageBackend *storage) {
    storage_backend_close(storage);
}

/* ── File/dir cleanup helpers (no shell calls) ───────────────────────────── */

static inline void test_rm(const char *path) {
    /* Best-effort remove file; ignore if it doesn't exist */
    (void)remove(path);
}

static inline void test_rmrf(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0)
        return;

    if (S_ISDIR(st.st_mode)) {
        DIR *d = opendir(path);
        if (!d)
            return;
        struct dirent *ent;
        while ((ent = readdir(d)) != NULL) {
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
                continue;
            char child[4096];
            int n = snprintf(child, sizeof(child), "%.2048s/%.2048s", path, ent->d_name);
            if (n > 0 && (size_t)n < sizeof(child))
                test_rmrf(child);
        }
        closedir(d);
        rmdir(path);
    } else {
        remove(path);
    }
}

static inline void test_mkdirp(const char *path) {
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
}

static inline int test_cleanup_file(const char *path) {
    return remove(path);
}

static inline int test_cleanup_dir(const char *path) {
    if (!path || path[0] == '\0')
        return -1;
    test_rmrf(path);
    return 0;
}

#endif /* TEST_HELPERS_H */
