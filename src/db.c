#include "db.h"
#include "config.h"

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
        LOG_ERROR("exec error: %s", err ? err : "unknown");
        sqlite3_free(err);
        return -1;
    }
    LOG_DEBUG("SQL executed: %s", sql);
    return 0;
}

/* ── open / migrate ───────────────────────────────────────────────────────── */

DB *db_open(const char *path)
{
    LOG_INFO("opening database: %s", path);
    DB *db = calloc(1, sizeof(DB));
    if (!db) {
        LOG_ERROR("failed to allocate memory for DB");
        return NULL;
    }

    int rc = sqlite3_open(path, &db->conn);
    if (rc != SQLITE_OK) {
        LOG_ERROR("open %s: %s", path, sqlite3_errmsg(db->conn));
        free(db);
        return NULL;
    }

    /* Performance tuning for low-memory device */
    sqlite3_exec(db->conn, "PRAGMA journal_mode=WAL;",  NULL, NULL, NULL);
    sqlite3_exec(db->conn, "PRAGMA synchronous=NORMAL;", NULL, NULL, NULL);
    sqlite3_exec(db->conn, "PRAGMA busy_timeout=5000;",  NULL, NULL, NULL);
    sqlite3_exec(db->conn, "PRAGMA cache_size=-2000;",   NULL, NULL, NULL); /* 2MB */
    LOG_DEBUG("database performance tuning applied");

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
        "CREATE TABLE IF NOT EXISTS parsed_receipts ("
        "  id           INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  file_id      INTEGER NOT NULL UNIQUE,"
        "  parsed_json  TEXT    NOT NULL,"
        "  created_at   TEXT    NOT NULL,"
        "  updated_at   TEXT    NOT NULL,"
        "  FOREIGN KEY (file_id) REFERENCES files(id)"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_files_hash ON files(file_hash);"
        "CREATE INDEX IF NOT EXISTS idx_parsed_file_id ON parsed_receipts(file_id);";

    if (db_exec(db->conn, schema) != 0) {
        LOG_ERROR("failed to create database schema");
        sqlite3_close(db->conn);
        free(db);
        return NULL;
    }

    LOG_INFO("database opened and schema verified");
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
    LOG_DEBUG("looking up file by hash: %.16s...", hash);
    const char *sql =
        "SELECT id, original_file_name, file_size_bytes, saved_file_name,"
        "       file_hash, is_ocr_processed, ocr_file_name, created_at, updated_at"
        " FROM files WHERE file_hash = ? LIMIT 1";

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db->conn, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        LOG_ERROR("prepare find_by_hash: %s", sqlite3_errmsg(db->conn));
        return -1;
    }
    sqlite3_bind_text(stmt, 1, hash, -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_DONE) {
        sqlite3_finalize(stmt);
        LOG_DEBUG("file not found by hash");
        return 1; /* not found */
    }
    if (rc != SQLITE_ROW) {
        LOG_ERROR("step find_by_hash: %s", sqlite3_errmsg(db->conn));
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
    LOG_DEBUG("file found: id=%lld, ocr=%s", (long long)out->id, out->is_ocr_processed ? "done" : "pending");
    return 0;
}

/* ── insert ───────────────────────────────────────────────────────────────── */

int db_insert(DB *db, FileRecord *rec)
{
    char now[DB_DATE_LEN];
    now_iso8601(now, sizeof(now));

    LOG_DEBUG("inserting file record: %s (%lld bytes)", rec->original_file_name, (long long)rec->file_size_bytes);
    const char *sql =
        "INSERT INTO files"
        " (original_file_name, file_size_bytes, saved_file_name, file_hash,"
        "  is_ocr_processed, ocr_file_name, created_at, updated_at)"
        " VALUES (?,?,?,?,?,?,?,?)";

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db->conn, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        LOG_ERROR("prepare insert: %s", sqlite3_errmsg(db->conn));
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
        LOG_ERROR("insert error: %s", sqlite3_errmsg(db->conn));
        return -1;
    }

    rec->id = sqlite3_last_insert_rowid(db->conn);
    snprintf(rec->created_at, DB_DATE_LEN, "%s", now);
    snprintf(rec->updated_at, DB_DATE_LEN, "%s", now);
    LOG_DEBUG("file record inserted with id=%lld", (long long)rec->id);
    return 0;
}

/* ── mark OCR done ────────────────────────────────────────────────────────── */

int db_mark_ocr_done(DB *db, int64_t id, const char *ocr_file_name)
{
    char now[DB_DATE_LEN];
    now_iso8601(now, sizeof(now));

    LOG_DEBUG("marking OCR done for file id=%lld: %s", (long long)id, ocr_file_name);
    const char *sql =
        "UPDATE files SET is_ocr_processed=1, ocr_file_name=?, updated_at=?"
        " WHERE id=?";

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db->conn, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        LOG_ERROR("prepare mark_ocr: %s", sqlite3_errmsg(db->conn));
        return -1;
    }

    sqlite3_bind_text(stmt,  1, ocr_file_name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt,  2, now,            -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 3, id);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        LOG_ERROR("mark_ocr error: %s", sqlite3_errmsg(db->conn));
        return -1;
    }
    LOG_DEBUG("OCR marked done for file id=%lld", (long long)id);
    return 0;
}

/* ── list ─────────────────────────────────────────────────────────────────── */

int db_list(DB *db, db_list_cb cb, void *userdata)
{
    LOG_DEBUG("listing all files");
    const char *sql =
        "SELECT id, original_file_name, file_size_bytes, saved_file_name,"
        "       file_hash, is_ocr_processed, ocr_file_name, created_at, updated_at"
        " FROM files ORDER BY created_at DESC";

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db->conn, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        LOG_ERROR("prepare list: %s", sqlite3_errmsg(db->conn));
        return -1;
    }

    int count = 0;
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
        count++;
    }

    sqlite3_finalize(stmt);
    LOG_DEBUG("listed %d files", count);
    return (rc == SQLITE_DONE) ? 0 : -1;
}

/* ── mark parsing done ────────────────────────────────────────────────────── */

int db_mark_parsing_done(DB *db, int64_t file_id, const char *parsed_json)
{
    char now[DB_DATE_LEN];
    now_iso8601(now, sizeof(now));

    LOG_DEBUG("marking parsing done for file id=%lld", (long long)file_id);
    /* Check if already exists */
    const char *check_sql = "SELECT id FROM parsed_receipts WHERE file_id = ?";
    sqlite3_stmt *check_stmt;
    int rc = sqlite3_prepare_v2(db->conn, check_sql, -1, &check_stmt, NULL);
    if (rc != SQLITE_OK) {
        LOG_ERROR("prepare check_parsing: %s", sqlite3_errmsg(db->conn));
        return -1;
    }
    sqlite3_bind_int64(check_stmt, 1, file_id);
    rc = sqlite3_step(check_stmt);
    int exists = (rc == SQLITE_ROW);
    sqlite3_finalize(check_stmt);
    LOG_DEBUG("parsed receipt %s for file id=%lld", exists ? "exists" : "new", (long long)file_id);

    const char *sql;
    sqlite3_stmt *stmt;
    if (exists) {
        sql = "UPDATE parsed_receipts SET parsed_json=?, updated_at=? WHERE file_id=?";
        rc = sqlite3_prepare_v2(db->conn, sql, -1, &stmt, NULL);
        if (rc != SQLITE_OK) {
            LOG_ERROR("prepare update_parsing: %s", sqlite3_errmsg(db->conn));
            return -1;
        }
        sqlite3_bind_text(stmt,  1, parsed_json, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt,  2, now,         -1, SQLITE_STATIC);
        sqlite3_bind_int64(stmt, 3, file_id);
    } else {
        sql = "INSERT INTO parsed_receipts (file_id, parsed_json, created_at, updated_at)"
              " VALUES (?, ?, ?, ?)";
        rc = sqlite3_prepare_v2(db->conn, sql, -1, &stmt, NULL);
        if (rc != SQLITE_OK) {
            LOG_ERROR("prepare insert_parsing: %s", sqlite3_errmsg(db->conn));
            return -1;
        }
        sqlite3_bind_int64(stmt, 1, file_id);
        sqlite3_bind_text(stmt,  2, parsed_json, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt,  3, now,         -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt,  4, now,         -1, SQLITE_STATIC);
    }

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        LOG_ERROR("parsing error: %s", sqlite3_errmsg(db->conn));
        return -1;
    }
    LOG_DEBUG("parsing saved for file id=%lld", (long long)file_id);
    return 0;
}

/* ── get parsed receipt ───────────────────────────────────────────────────── */

int db_get_parsed_receipt(DB *db, int64_t file_id, ParsedReceipt *out)
{
    LOG_DEBUG("getting parsed receipt for file id=%lld", (long long)file_id);
    const char *sql =
        "SELECT id, file_id, parsed_json, created_at, updated_at"
        " FROM parsed_receipts WHERE file_id = ? LIMIT 1";

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db->conn, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        LOG_ERROR("prepare get_parsed: %s", sqlite3_errmsg(db->conn));
        return -1;
    }
    sqlite3_bind_int64(stmt, 1, file_id);

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_DONE) {
        sqlite3_finalize(stmt);
        LOG_DEBUG("parsed receipt not found for file id=%lld", (long long)file_id);
        return 1; /* not found */
    }
    if (rc != SQLITE_ROW) {
        LOG_ERROR("step get_parsed: %s", sqlite3_errmsg(db->conn));
        sqlite3_finalize(stmt);
        return -1;
    }

    memset(out, 0, sizeof(*out));
    out->id        = sqlite3_column_int64(stmt, 0);
    out->file_id   = sqlite3_column_int64(stmt, 1);
    strncpy(out->parsed_json, (const char *)sqlite3_column_text(stmt, 2), DB_JSON_LEN - 1);
    strncpy(out->created_at,  (const char *)sqlite3_column_text(stmt, 3), DB_DATE_LEN - 1);
    strncpy(out->updated_at,  (const char *)sqlite3_column_text(stmt, 4), DB_DATE_LEN - 1);

    sqlite3_finalize(stmt);
    LOG_DEBUG("parsed receipt found for file id=%lld", (long long)file_id);
    return 0;
}
