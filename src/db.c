#include "db.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct DB {
    sqlite3 *conn;
};

/* ── helpers ──────────────────────────────────────────────────────────────── */

static void now_iso8601(char *out, size_t len)
{
    time_t t = time(NULL);
    struct tm *tm = gmtime(&t);
    strftime(out, len, "%Y-%m-%dT%H:%M:%SZ", tm);
}

static int db_exec(sqlite3 *conn, const char *sql)
{
    char *err = NULL;
    int rc = sqlite3_exec(conn, sql, NULL, NULL, &err);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[db] exec error: %s\n", err ? err : "unknown");
        sqlite3_free(err);
        return -1;
    }
    return 0;
}

/* ── open / migrate ───────────────────────────────────────────────────────── */

DB *db_open(const char *path)
{
    DB *db = calloc(1, sizeof(DB));
    if (!db) return NULL;

    int rc = sqlite3_open(path, &db->conn);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[db] open %s: %s\n", path, sqlite3_errmsg(db->conn));
        free(db);
        return NULL;
    }

    /* Performance tuning for low-memory device */
    sqlite3_exec(db->conn, "PRAGMA journal_mode=WAL;",  NULL, NULL, NULL);
    sqlite3_exec(db->conn, "PRAGMA synchronous=NORMAL;", NULL, NULL, NULL);
    sqlite3_exec(db->conn, "PRAGMA busy_timeout=5000;",  NULL, NULL, NULL);
    sqlite3_exec(db->conn, "PRAGMA cache_size=-2000;",   NULL, NULL, NULL); /* 2MB */

    const char *schema =
        "CREATE TABLE IF NOT EXISTS files ("
        "  id                 INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  original_file_name TEXT    NOT NULL,"
        "  file_size_bytes    INTEGER NOT NULL,"
        "  saved_file_name    TEXT    NOT NULL UNIQUE,"
        "  file_hash          TEXT    NOT NULL UNIQUE,"
        "  is_ocr_processed   INTEGER NOT NULL DEFAULT 0,"
        "  ocr_file_name      TEXT    NOT NULL DEFAULT '',"
        "  created_at         TEXT    NOT NULL,"
        "  updated_at         TEXT    NOT NULL"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_files_hash ON files(file_hash);";

    if (db_exec(db->conn, schema) != 0) {
        sqlite3_close(db->conn);
        free(db);
        return NULL;
    }

    return db;
}

void db_close(DB *db)
{
    if (db) {
        sqlite3_close(db->conn);
        free(db);
    }
}

/* ── find by hash ─────────────────────────────────────────────────────────── */

int db_find_by_hash(DB *db, const char *hash, FileRecord *out)
{
    const char *sql =
        "SELECT id, original_file_name, file_size_bytes, saved_file_name,"
        "       file_hash, is_ocr_processed, ocr_file_name, created_at, updated_at"
        " FROM files WHERE file_hash = ? LIMIT 1";

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db->conn, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[db] prepare find_by_hash: %s\n", sqlite3_errmsg(db->conn));
        return -1;
    }
    sqlite3_bind_text(stmt, 1, hash, -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_DONE) {
        sqlite3_finalize(stmt);
        return 1; /* not found */
    }
    if (rc != SQLITE_ROW) {
        fprintf(stderr, "[db] step find_by_hash: %s\n", sqlite3_errmsg(db->conn));
        sqlite3_finalize(stmt);
        return -1;
    }

    memset(out, 0, sizeof(*out));
    out->id             = sqlite3_column_int64(stmt, 0);
    strncpy(out->original_file_name, (const char *)sqlite3_column_text(stmt, 1), DB_ORIG_NAME_LEN - 1);
    out->file_size_bytes = sqlite3_column_int64(stmt, 2);
    strncpy(out->saved_file_name, (const char *)sqlite3_column_text(stmt, 3), DB_FILENAME_LEN - 1);
    strncpy(out->file_hash,       (const char *)sqlite3_column_text(stmt, 4), DB_HASH_LEN - 1);
    out->is_ocr_processed = sqlite3_column_int(stmt, 5);
    strncpy(out->ocr_file_name,   (const char *)sqlite3_column_text(stmt, 6), DB_OCR_FILENAME_LEN - 1);
    strncpy(out->created_at,      (const char *)sqlite3_column_text(stmt, 7), DB_DATE_LEN - 1);
    strncpy(out->updated_at,      (const char *)sqlite3_column_text(stmt, 8), DB_DATE_LEN - 1);

    sqlite3_finalize(stmt);
    return 0;
}

/* ── insert ───────────────────────────────────────────────────────────────── */

int db_insert(DB *db, FileRecord *rec)
{
    char now[DB_DATE_LEN];
    now_iso8601(now, sizeof(now));

    const char *sql =
        "INSERT INTO files"
        " (original_file_name, file_size_bytes, saved_file_name, file_hash,"
        "  is_ocr_processed, ocr_file_name, created_at, updated_at)"
        " VALUES (?,?,?,?,?,?,?,?)";

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db->conn, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[db] prepare insert: %s\n", sqlite3_errmsg(db->conn));
        return -1;
    }

    sqlite3_bind_text(stmt, 1, rec->original_file_name, -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 2, rec->file_size_bytes);
    sqlite3_bind_text(stmt, 3, rec->saved_file_name,    -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, rec->file_hash,          -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt,  5, rec->is_ocr_processed);
    sqlite3_bind_text(stmt, 6, rec->ocr_file_name,      -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 7, now,                     -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 8, now,                     -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        fprintf(stderr, "[db] insert error: %s\n", sqlite3_errmsg(db->conn));
        return -1;
    }

    rec->id = sqlite3_last_insert_rowid(db->conn);
    strncpy(rec->created_at, now, DB_DATE_LEN - 1);
    strncpy(rec->updated_at, now, DB_DATE_LEN - 1);
    return 0;
}

/* ── mark OCR done ────────────────────────────────────────────────────────── */

int db_mark_ocr_done(DB *db, int64_t id, const char *ocr_file_name)
{
    char now[DB_DATE_LEN];
    now_iso8601(now, sizeof(now));

    const char *sql =
        "UPDATE files SET is_ocr_processed=1, ocr_file_name=?, updated_at=?"
        " WHERE id=?";

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db->conn, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[db] prepare mark_ocr: %s\n", sqlite3_errmsg(db->conn));
        return -1;
    }

    sqlite3_bind_text(stmt,  1, ocr_file_name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt,  2, now,            -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 3, id);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        fprintf(stderr, "[db] mark_ocr error: %s\n", sqlite3_errmsg(db->conn));
        return -1;
    }
    return 0;
}

/* ── list ─────────────────────────────────────────────────────────────────── */

int db_list(DB *db, db_list_cb cb, void *userdata)
{
    const char *sql =
        "SELECT id, original_file_name, file_size_bytes, saved_file_name,"
        "       file_hash, is_ocr_processed, ocr_file_name, created_at, updated_at"
        " FROM files ORDER BY created_at DESC";

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db->conn, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[db] prepare list: %s\n", sqlite3_errmsg(db->conn));
        return -1;
    }

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        FileRecord rec;
        memset(&rec, 0, sizeof(rec));
        rec.id = sqlite3_column_int64(stmt, 0);
        strncpy(rec.original_file_name, (const char *)sqlite3_column_text(stmt, 1), DB_ORIG_NAME_LEN - 1);
        rec.file_size_bytes = sqlite3_column_int64(stmt, 2);
        strncpy(rec.saved_file_name, (const char *)sqlite3_column_text(stmt, 3), DB_FILENAME_LEN - 1);
        strncpy(rec.file_hash,       (const char *)sqlite3_column_text(stmt, 4), DB_HASH_LEN - 1);
        rec.is_ocr_processed = sqlite3_column_int(stmt, 5);
        strncpy(rec.ocr_file_name, (const char *)sqlite3_column_text(stmt, 6), DB_OCR_FILENAME_LEN - 1);
        strncpy(rec.created_at,    (const char *)sqlite3_column_text(stmt, 7), DB_DATE_LEN - 1);
        strncpy(rec.updated_at,    (const char *)sqlite3_column_text(stmt, 8), DB_DATE_LEN - 1);
        cb(&rec, userdata);
    }

    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? 0 : -1;
}
