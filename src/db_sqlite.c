#include "db_backend.h"
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sqlite3.h>

/* Forward declaration */
static const DBBackendOps sqlite_ops;

typedef struct {
    sqlite3 *conn;
} SQLiteDB;

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

static DBBackend *sqlite_open(const Config *cfg)
{
    const char *path = cfg->db_path;
    LOG_INFO("opening SQLite database: %s", path);
    
    SQLiteDB *db = calloc(1, sizeof(SQLiteDB));
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

    /* Performance tuning */
    sqlite3_exec(db->conn, "PRAGMA journal_mode=WAL;",  NULL, NULL, NULL);
    sqlite3_exec(db->conn, "PRAGMA synchronous=NORMAL;", NULL, NULL, NULL);
    sqlite3_exec(db->conn, "PRAGMA busy_timeout=5000;",  NULL, NULL, NULL);
    sqlite3_exec(db->conn, "PRAGMA cache_size=-2000;",   NULL, NULL, NULL);

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

    DBBackend *backend = calloc(1, sizeof(DBBackend));
    if (!backend) {
        sqlite3_close(db->conn);
        free(db);
        return NULL;
    }
    backend->ops = &sqlite_ops;
    backend->internal = db;
    LOG_INFO("SQLite database opened successfully");
    return backend;
}

static void sqlite_close(DBBackend *backend)
{
    if (!backend) return;
    SQLiteDB *db = (SQLiteDB *)backend->internal;
    if (db) {
        sqlite3_close(db->conn);
        free(db);
    }
    free(backend);
}

/* ── find by hash ─────────────────────────────────────────────────────────── */

static int sqlite_find_by_hash(DBBackend *backend, const char *hash, FileRecord *out)
{
    SQLiteDB *db = (SQLiteDB *)backend->internal;
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
        return 1;
    }
    if (rc != SQLITE_ROW) {
        LOG_ERROR("step find_by_hash: %s", sqlite3_errmsg(db->conn));
        sqlite3_finalize(stmt);
        return -1;
    }

    memset(out, 0, sizeof(*out));
    out->id             = sqlite3_column_int64(stmt, 0);
    snprintf(out->original_file_name, DB_ORIG_NAME_LEN, "%s", (const char *)sqlite3_column_text(stmt, 1));
    out->file_size_bytes = sqlite3_column_int64(stmt, 2);
    snprintf(out->saved_file_name, DB_FILENAME_LEN, "%s", (const char *)sqlite3_column_text(stmt, 3));
    snprintf(out->file_hash, DB_HASH_LEN, "%s", (const char *)sqlite3_column_text(stmt, 4));
    out->is_ocr_processed = sqlite3_column_int(stmt, 5);
    snprintf(out->ocr_file_name, DB_OCR_FILENAME_LEN, "%s", (const char *)sqlite3_column_text(stmt, 6));
    snprintf(out->created_at, DB_DATE_LEN, "%s", (const char *)sqlite3_column_text(stmt, 7));
    snprintf(out->updated_at, DB_DATE_LEN, "%s", (const char *)sqlite3_column_text(stmt, 8));

    sqlite3_finalize(stmt);
    LOG_DEBUG("file found: id=%lld, ocr=%s", (long long)out->id, out->is_ocr_processed ? "done" : "pending");
    return 0;
}

/* ── insert ───────────────────────────────────────────────────────────────── */

static int sqlite_insert(DBBackend *backend, FileRecord *rec)
{
    SQLiteDB *db = (SQLiteDB *)backend->internal;
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

static int sqlite_mark_ocr_done(DBBackend *backend, int64_t id, const char *ocr_file_name)
{
    SQLiteDB *db = (SQLiteDB *)backend->internal;
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

/* ── mark parsing done ────────────────────────────────────────────────────── */

static int sqlite_mark_parsing_done(DBBackend *backend, int64_t file_id, const char *parsed_json)
{
    SQLiteDB *db = (SQLiteDB *)backend->internal;
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

static int sqlite_get_parsed_receipt(DBBackend *backend, int64_t file_id, ParsedReceipt *out)
{
    SQLiteDB *db = (SQLiteDB *)backend->internal;
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
        return 1;
    }
    if (rc != SQLITE_ROW) {
        LOG_ERROR("step get_parsed: %s", sqlite3_errmsg(db->conn));
        sqlite3_finalize(stmt);
        return -1;
    }

    memset(out, 0, sizeof(*out));
    out->id        = sqlite3_column_int64(stmt, 0);
    out->file_id   = sqlite3_column_int64(stmt, 1);
    snprintf(out->parsed_json, DB_JSON_LEN, "%s", (const char *)sqlite3_column_text(stmt, 2));
    snprintf(out->created_at, DB_DATE_LEN, "%s", (const char *)sqlite3_column_text(stmt, 3));
    snprintf(out->updated_at, DB_DATE_LEN, "%s", (const char *)sqlite3_column_text(stmt, 4));

    sqlite3_finalize(stmt);
    LOG_DEBUG("parsed receipt found for file id=%lld", (long long)file_id);
    return 0;
}

/* ── list ─────────────────────────────────────────────────────────────────── */

static int sqlite_list(DBBackend *backend, db_list_cb cb, void *userdata)
{
    SQLiteDB *db = (SQLiteDB *)backend->internal;
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
        snprintf(rec.original_file_name, DB_ORIG_NAME_LEN, "%s", (const char *)sqlite3_column_text(stmt, 1));
        rec.file_size_bytes = sqlite3_column_int64(stmt, 2);
        snprintf(rec.saved_file_name, DB_FILENAME_LEN, "%s", (const char *)sqlite3_column_text(stmt, 3));
        snprintf(rec.file_hash, DB_HASH_LEN, "%s", (const char *)sqlite3_column_text(stmt, 4));
        rec.is_ocr_processed = sqlite3_column_int(stmt, 5);
        snprintf(rec.ocr_file_name, DB_OCR_FILENAME_LEN, "%s", (const char *)sqlite3_column_text(stmt, 6));
        snprintf(rec.created_at, DB_DATE_LEN, "%s", (const char *)sqlite3_column_text(stmt, 7));
        snprintf(rec.updated_at, DB_DATE_LEN, "%s", (const char *)sqlite3_column_text(stmt, 8));
        cb(&rec, userdata);
        count++;
    }

    sqlite3_finalize(stmt);
    LOG_DEBUG("listed %d files", count);
    return (rc == SQLITE_DONE) ? 0 : -1;
}

/* ── backend ops ──────────────────────────────────────────────────────────── */

static const DBBackendOps sqlite_ops = {
    .open              = sqlite_open,
    .close             = sqlite_close,
    .find_by_hash      = sqlite_find_by_hash,
    .insert            = sqlite_insert,
    .mark_ocr_done     = sqlite_mark_ocr_done,
    .mark_parsing_done = sqlite_mark_parsing_done,
    .get_parsed_receipt = sqlite_get_parsed_receipt,
    .list              = sqlite_list
};

DBBackend *db_backend_sqlite_open(const Config *cfg)
{
    return sqlite_ops.open(cfg);
}

/* ── Backward compatibility wrappers for tests ───────────────────────────── */

typedef struct DB {
    sqlite3 *conn;
} DB;

static DB *g_test_db = NULL;

DB *db_open(const char *path)
{
    if (g_test_db) return g_test_db;
    
    DB *db = calloc(1, sizeof(DB));
    if (!db) return NULL;

    int rc = sqlite3_open(path, &db->conn);
    if (rc != SQLITE_OK) {
        free(db);
        return NULL;
    }

    sqlite3_exec(db->conn, "PRAGMA journal_mode=WAL;", NULL, NULL, NULL);
    
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
        "  updated_at   TEXT    NOT NULL"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_files_hash ON files(file_hash);"
        "CREATE INDEX IF NOT EXISTS idx_parsed_file_id ON parsed_receipts(file_id);";

    char *err = NULL;
    rc = sqlite3_exec(db->conn, schema, NULL, NULL, &err);
    if (rc != SQLITE_OK) {
        sqlite3_free(err);
        sqlite3_close(db->conn);
        free(db);
        return NULL;
    }

    g_test_db = db;
    return db;
}

void db_close(DB *db)
{
    if (db && db->conn) {
        sqlite3_close(db->conn);
        free(db);
    }
    g_test_db = NULL;
}

int db_find_by_hash(DB *db, const char *hash, FileRecord *out)
{
    const char *sql =
        "SELECT id, original_file_name, file_size_bytes, saved_file_name,"
        "       file_hash, is_ocr_processed, ocr_file_name, created_at, updated_at"
        " FROM files WHERE file_hash = ? LIMIT 1";

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db->conn, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) return -1;
    sqlite3_bind_text(stmt, 1, hash, -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_DONE) {
        sqlite3_finalize(stmt);
        return 1;
    }
    if (rc != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return -1;
    }

    memset(out, 0, sizeof(*out));
    out->id = sqlite3_column_int64(stmt, 0);
    snprintf(out->original_file_name, DB_ORIG_NAME_LEN, "%s", (const char *)sqlite3_column_text(stmt, 1));
    out->file_size_bytes = sqlite3_column_int64(stmt, 2);
    snprintf(out->saved_file_name, DB_FILENAME_LEN, "%s", (const char *)sqlite3_column_text(stmt, 3));
    snprintf(out->file_hash, DB_HASH_LEN, "%s", (const char *)sqlite3_column_text(stmt, 4));
    out->is_ocr_processed = sqlite3_column_int(stmt, 5);
    snprintf(out->ocr_file_name, DB_OCR_FILENAME_LEN, "%s", (const char *)sqlite3_column_text(stmt, 6));
    snprintf(out->created_at, DB_DATE_LEN, "%s", (const char *)sqlite3_column_text(stmt, 7));
    snprintf(out->updated_at, DB_DATE_LEN, "%s", (const char *)sqlite3_column_text(stmt, 8));

    sqlite3_finalize(stmt);
    return 0;
}

int db_insert(DB *db, FileRecord *rec)
{
    char now[DB_DATE_LEN];
    time_t t = time(NULL);
    struct tm *tm = gmtime(&t);
    strftime(now, sizeof(now), "%Y-%m-%dT%H:%M:%SZ", tm);

    const char *sql =
        "INSERT INTO files"
        " (original_file_name, file_size_bytes, saved_file_name, file_hash,"
        "  is_ocr_processed, ocr_file_name, created_at, updated_at)"
        " VALUES (?,?,?,?,?,?,?,?)";

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db->conn, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) return -1;

    sqlite3_bind_text(stmt, 1, rec->original_file_name, -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 2, rec->file_size_bytes);
    sqlite3_bind_text(stmt, 3, rec->saved_file_name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, rec->file_hash, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 5, rec->is_ocr_processed);
    sqlite3_bind_text(stmt, 6, rec->ocr_file_name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 7, now, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 8, now, -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) return -1;

    rec->id = sqlite3_last_insert_rowid(db->conn);
    snprintf(rec->created_at, DB_DATE_LEN, "%s", now);
    snprintf(rec->updated_at, DB_DATE_LEN, "%s", now);
    return 0;
}

int db_mark_ocr_done(DB *db, int64_t id, const char *ocr_file_name)
{
    const char *sql = "UPDATE files SET is_ocr_processed=1, ocr_file_name=? WHERE id=?";
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db->conn, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) return -1;

    sqlite3_bind_text(stmt, 1, ocr_file_name, -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 2, id);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? 0 : -1;
}

int db_mark_parsing_done(DB *db, int64_t file_id, const char *parsed_json)
{
    char now[DB_DATE_LEN];
    time_t t = time(NULL);
    struct tm *tm = gmtime(&t);
    strftime(now, sizeof(now), "%Y-%m-%dT%H:%M:%SZ", tm);

    const char *sql =
        "INSERT INTO parsed_receipts (file_id, parsed_json, created_at, updated_at)"
        " VALUES (?, ?, ?, ?)"
        " ON CONFLICT(file_id) DO UPDATE SET parsed_json=?, updated_at=?";

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db->conn, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) return -1;

    sqlite3_bind_int64(stmt, 1, file_id);
    sqlite3_bind_text(stmt, 2, parsed_json, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, now, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, now, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 5, parsed_json, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 6, now, -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? 0 : -1;
}

int db_get_parsed_receipt(DB *db, int64_t file_id, ParsedReceipt *out)
{
    const char *sql =
        "SELECT id, file_id, parsed_json, created_at, updated_at"
        " FROM parsed_receipts WHERE file_id = ? LIMIT 1";

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db->conn, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) return -1;
    sqlite3_bind_int64(stmt, 1, file_id);

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_DONE) {
        sqlite3_finalize(stmt);
        return 1;
    }
    if (rc != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return -1;
    }

    memset(out, 0, sizeof(*out));
    out->id = sqlite3_column_int64(stmt, 0);
    out->file_id = sqlite3_column_int64(stmt, 1);
    snprintf(out->parsed_json, DB_JSON_LEN, "%s", (const char *)sqlite3_column_text(stmt, 2));
    snprintf(out->created_at, DB_DATE_LEN, "%s", (const char *)sqlite3_column_text(stmt, 3));
    snprintf(out->updated_at, DB_DATE_LEN, "%s", (const char *)sqlite3_column_text(stmt, 4));

    sqlite3_finalize(stmt);
    return 0;
}

int db_list(DB *db, db_list_cb cb, void *userdata)
{
    const char *sql =
        "SELECT id, original_file_name, file_size_bytes, saved_file_name,"
        "       file_hash, is_ocr_processed, ocr_file_name, created_at, updated_at"
        " FROM files ORDER BY created_at DESC";

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db->conn, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) return -1;

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        FileRecord rec;
        memset(&rec, 0, sizeof(rec));
        rec.id = sqlite3_column_int64(stmt, 0);
        snprintf(rec.original_file_name, DB_ORIG_NAME_LEN, "%s", (const char *)sqlite3_column_text(stmt, 1));
        rec.file_size_bytes = sqlite3_column_int64(stmt, 2);
        snprintf(rec.saved_file_name, DB_FILENAME_LEN, "%s", (const char *)sqlite3_column_text(stmt, 3));
        snprintf(rec.file_hash, DB_HASH_LEN, "%s", (const char *)sqlite3_column_text(stmt, 4));
        rec.is_ocr_processed = sqlite3_column_int(stmt, 5);
        snprintf(rec.ocr_file_name, DB_OCR_FILENAME_LEN, "%s", (const char *)sqlite3_column_text(stmt, 6));
        snprintf(rec.created_at, DB_DATE_LEN, "%s", (const char *)sqlite3_column_text(stmt, 7));
        snprintf(rec.updated_at, DB_DATE_LEN, "%s", (const char *)sqlite3_column_text(stmt, 8));
        cb(&rec, userdata);
    }

    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? 0 : -1;
}
