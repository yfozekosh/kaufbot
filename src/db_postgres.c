#include "db_backend.h"
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <libpq-fe.h>

typedef struct {
    PGconn *conn;
} PostgresDB;

/* ── helpers ──────────────────────────────────────────────────────────────── */

static void now_iso8601(char *out, size_t len)
{
    time_t t = time(NULL);
    struct tm *tm = gmtime(&t);
    strftime(out, len, "%Y-%m-%dT%H:%M:%SZ", tm);
}

static int db_exec(PGconn *conn, const char *sql)
{
    PGresult *res = PQexec(conn, sql);
    ExecStatusType status = PQresultStatus(res);
    if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK) {
        LOG_ERROR("exec error: %s", PQerrorMessage(conn));
        PQclear(res);
        return -1;
    }
    LOG_DEBUG("SQL executed: %s", sql);
    PQclear(res);
    return 0;
}

__attribute__((unused)) static char *escape_literal(PGconn *conn, const char *str)
{
    char *escaped = PQescapeLiteral(conn, str, strlen(str));
    if (!escaped) {
        LOG_ERROR("escape error: %s", PQerrorMessage(conn));
        return NULL;
    }
    return escaped;
}

/* ── open / migrate ───────────────────────────────────────────────────────── */

static DBBackend *postgres_open(const Config *cfg)
{
    LOG_INFO("opening PostgreSQL database: %s@%s:%s/%s", 
             cfg->postgres_user, cfg->postgres_host, cfg->postgres_port, cfg->postgres_db);
    
    /* Build connection string */
    char conninfo[2048];
    snprintf(conninfo, sizeof(conninfo),
             "host=%s port=%s dbname=%s user=%s password=%s sslmode=%s",
             cfg->postgres_host, cfg->postgres_port, cfg->postgres_db,
             cfg->postgres_user, cfg->postgres_password, cfg->postgres_ssl_mode);

    PGconn *conn = PQconnectdb(conninfo);
    if (PQstatus(conn) != CONNECTION_OK) {
        LOG_ERROR("connection failed: %s", PQerrorMessage(conn));
        PQfinish(conn);
        return NULL;
    }

    LOG_INFO("PostgreSQL connection established");

    /* Create tables */
    const char *schema =
        "CREATE TABLE IF NOT EXISTS files ("
        "  id                 BIGSERIAL PRIMARY KEY,"
        "  original_file_name TEXT    NOT NULL,"
        "  file_size_bytes    BIGINT  NOT NULL,"
        "  saved_file_name    TEXT    NOT NULL UNIQUE,"
        "  file_hash          TEXT    NOT NULL UNIQUE,"
        "  is_ocr_processed   BOOLEAN NOT NULL DEFAULT FALSE,"
        "  ocr_file_name      TEXT    NOT NULL DEFAULT '',"
        "  created_at         TIMESTAMPTZ NOT NULL DEFAULT NOW(),"
        "  updated_at         TIMESTAMPTZ NOT NULL DEFAULT NOW()"
        ");"
        "CREATE TABLE IF NOT EXISTS parsed_receipts ("
        "  id           BIGSERIAL PRIMARY KEY,"
        "  file_id      BIGINT NOT NULL UNIQUE REFERENCES files(id) ON DELETE CASCADE,"
        "  parsed_json  JSONB  NOT NULL,"
        "  created_at   TIMESTAMPTZ NOT NULL DEFAULT NOW(),"
        "  updated_at   TIMESTAMPTZ NOT NULL DEFAULT NOW()"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_files_hash ON files(file_hash);"
        "CREATE INDEX IF NOT EXISTS idx_parsed_file_id ON parsed_receipts(file_id);";

    if (db_exec(conn, schema) != 0) {
        LOG_ERROR("failed to create database schema");
        PQfinish(conn);
        return NULL;
    }

    PostgresDB *db = calloc(1, sizeof(PostgresDB));
    if (!db) {
        PQfinish(conn);
        return NULL;
    }
    db->conn = conn;

    DBBackend *backend = calloc(1, sizeof(DBBackend));
    if (!backend) {
        PQfinish(conn);
        free(db);
        return NULL;
    }
    backend->internal = db;
    LOG_INFO("PostgreSQL database opened successfully");
    return backend;
}

static void postgres_close(DBBackend *backend)
{
    if (!backend) return;
    PostgresDB *db = (PostgresDB *)backend->internal;
    if (db && db->conn) {
        PQfinish(db->conn);
    }
    free(db);
    free(backend);
}

/* ── find by hash ─────────────────────────────────────────────────────────── */

static int postgres_find_by_hash(DBBackend *backend, const char *hash, FileRecord *out)
{
    PostgresDB *db = (PostgresDB *)backend->internal;
    LOG_DEBUG("looking up file by hash: %.16s...", hash);
    
    const char *sql =
        "SELECT id, original_file_name, file_size_bytes, saved_file_name,"
        "       file_hash, is_ocr_processed, ocr_file_name,"
        "       to_char(created_at, 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"'),"
        "       to_char(updated_at, 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"')"
        " FROM files WHERE file_hash = $1 LIMIT 1";

    const char *params[1] = { hash };
    PGresult *res = PQexecParams(db->conn, sql, 1, NULL, params, NULL, NULL, 0);
    ExecStatusType status = PQresultStatus(res);
    if (status != PGRES_TUPLES_OK) {
        LOG_ERROR("find_by_hash query error (status=%d): %s", (int)status, PQerrorMessage(db->conn));
        LOG_ERROR("find_by_hash sqlstate: %s", PQresultErrorField(res, PG_DIAG_SQLSTATE));
        PQclear(res);
        return -1;
    }

    if (PQntuples(res) == 0) {
        PQclear(res);
        LOG_DEBUG("file not found by hash");
        return 1;
    }

    memset(out, 0, sizeof(*out));
    out->id = strtoll(PQgetvalue(res, 0, 0), NULL, 10);
    snprintf(out->original_file_name, DB_ORIG_NAME_LEN, "%s", PQgetvalue(res, 0, 1));
    out->file_size_bytes = strtoll(PQgetvalue(res, 0, 2), NULL, 10);
    snprintf(out->saved_file_name, DB_FILENAME_LEN, "%s", PQgetvalue(res, 0, 3));
    snprintf(out->file_hash, DB_HASH_LEN, "%s", PQgetvalue(res, 0, 4));
    out->is_ocr_processed = strcmp(PQgetvalue(res, 0, 5), "t") == 0;
    snprintf(out->ocr_file_name, DB_OCR_FILENAME_LEN, "%s", PQgetvalue(res, 0, 6));
    snprintf(out->created_at, DB_DATE_LEN, "%s", PQgetvalue(res, 0, 7));
    snprintf(out->updated_at, DB_DATE_LEN, "%s", PQgetvalue(res, 0, 8));

    PQclear(res);
    LOG_DEBUG("file found: id=%lld, ocr=%s", (long long)out->id, out->is_ocr_processed ? "done" : "pending");
    return 0;
}

/* ── insert ───────────────────────────────────────────────────────────────── */

static int postgres_insert(DBBackend *backend, FileRecord *rec)
{
    PostgresDB *db = (PostgresDB *)backend->internal;
    char now[DB_DATE_LEN];
    now_iso8601(now, sizeof(now));

    LOG_DEBUG("inserting file record: %s (%lld bytes)", rec->original_file_name, (long long)rec->file_size_bytes);
    
    const char *sql =
        "INSERT INTO files"
        " (original_file_name, file_size_bytes, saved_file_name, file_hash,"
        "  is_ocr_processed, ocr_file_name, created_at, updated_at)"
        " VALUES ($1, $2, $3, $4, $5, $6, $7, $8)"
        " RETURNING id";

    const char *params[8];
    char size_str[32];
    char is_ocr_str[2];
    snprintf(size_str, sizeof(size_str), "%lld", (long long)rec->file_size_bytes);
    snprintf(is_ocr_str, sizeof(is_ocr_str), "%d", rec->is_ocr_processed);
    
    params[0] = rec->original_file_name;
    params[1] = size_str;
    params[2] = rec->saved_file_name;
    params[3] = rec->file_hash;
    params[4] = is_ocr_str;
    params[5] = rec->ocr_file_name;
    params[6] = now;
    params[7] = now;

    PGresult *res = PQexecParams(db->conn, sql, 8, NULL, params, NULL, NULL, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        LOG_ERROR("insert error: %s", PQerrorMessage(db->conn));
        PQclear(res);
        return -1;
    }

    rec->id = strtoll(PQgetvalue(res, 0, 0), NULL, 10);
    snprintf(rec->created_at, DB_DATE_LEN, "%s", now);
    snprintf(rec->updated_at, DB_DATE_LEN, "%s", now);
    PQclear(res);
    
    LOG_DEBUG("file record inserted with id=%lld", (long long)rec->id);
    return 0;
}

/* ── mark OCR done ────────────────────────────────────────────────────────── */

static int postgres_mark_ocr_done(DBBackend *backend, int64_t id, const char *ocr_file_name)
{
    PostgresDB *db = (PostgresDB *)backend->internal;
    LOG_DEBUG("marking OCR done for file id=%lld: %s", (long long)id, ocr_file_name);
    
    const char *sql =
        "UPDATE files SET is_ocr_processed=TRUE, ocr_file_name=$1, updated_at=NOW()"
        " WHERE id=$2";

    const char *params[2];
    char id_str[32];
    snprintf(id_str, sizeof(id_str), "%lld", (long long)id);
    params[0] = ocr_file_name;
    params[1] = id_str;

    PGresult *res = PQexecParams(db->conn, sql, 2, NULL, params, NULL, NULL, 0);
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        LOG_ERROR("mark_ocr error: %s", PQerrorMessage(db->conn));
        PQclear(res);
        return -1;
    }
    PQclear(res);
    
    LOG_DEBUG("OCR marked done for file id=%lld", (long long)id);
    return 0;
}

/* ── mark parsing done ────────────────────────────────────────────────────── */

static int postgres_mark_parsing_done(DBBackend *backend, int64_t file_id, const char *parsed_json)
{
    PostgresDB *db = (PostgresDB *)backend->internal;
    LOG_DEBUG("marking parsing done for file id=%lld", (long long)file_id);
    
    const char *sql =
        "INSERT INTO parsed_receipts (file_id, parsed_json, created_at, updated_at)"
        " VALUES ($1, $2::jsonb, NOW(), NOW())"
        " ON CONFLICT (file_id) DO UPDATE"
        " SET parsed_json = $2::jsonb, updated_at = NOW()";

    const char *params[2];
    char id_str[32];
    snprintf(id_str, sizeof(id_str), "%lld", (long long)file_id);
    params[0] = id_str;
    params[1] = parsed_json;

    PGresult *res = PQexecParams(db->conn, sql, 2, NULL, params, NULL, NULL, 0);
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        LOG_ERROR("parsing error: %s", PQerrorMessage(db->conn));
        PQclear(res);
        return -1;
    }
    PQclear(res);
    
    LOG_DEBUG("parsing saved for file id=%lld", (long long)file_id);
    return 0;
}

/* ── get parsed receipt ───────────────────────────────────────────────────── */

static int postgres_get_parsed_receipt(DBBackend *backend, int64_t file_id, ParsedReceipt *out)
{
    PostgresDB *db = (PostgresDB *)backend->internal;
    LOG_DEBUG("getting parsed receipt for file id=%lld", (long long)file_id);
    
    const char *sql =
        "SELECT id, file_id, parsed_json::text,"
        "       to_char(created_at, 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"'),"
        "       to_char(updated_at, 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"')"
        " FROM parsed_receipts WHERE file_id = $1 LIMIT 1";

    char id_str[32];
    snprintf(id_str, sizeof(id_str), "%lld", (long long)file_id);
    const char *params[1] = { id_str };

    PGresult *res = PQexecParams(db->conn, sql, 1, NULL, params, NULL, NULL, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        LOG_ERROR("query error: %s", PQerrorMessage(db->conn));
        PQclear(res);
        return -1;
    }

    if (PQntuples(res) == 0) {
        PQclear(res);
        LOG_DEBUG("parsed receipt not found for file id=%lld", (long long)file_id);
        return 1;
    }

    memset(out, 0, sizeof(*out));
    out->id = strtoll(PQgetvalue(res, 0, 0), NULL, 10);
    out->file_id = strtoll(PQgetvalue(res, 0, 1), NULL, 10);
    snprintf(out->parsed_json, DB_JSON_LEN, "%s", PQgetvalue(res, 0, 2));
    snprintf(out->created_at, DB_DATE_LEN, "%s", PQgetvalue(res, 0, 3));
    snprintf(out->updated_at, DB_DATE_LEN, "%s", PQgetvalue(res, 0, 4));

    PQclear(res);
    LOG_DEBUG("parsed receipt found for file id=%lld", (long long)file_id);
    return 0;
}

/* ── list ─────────────────────────────────────────────────────────────────── */

static int postgres_list(DBBackend *backend, db_list_cb cb, void *userdata)
{
    PostgresDB *db = (PostgresDB *)backend->internal;
    LOG_DEBUG("listing all files");
    
    const char *sql =
        "SELECT id, original_file_name, file_size_bytes, saved_file_name,"
        "       file_hash, is_ocr_processed, ocr_file_name,"
        "       to_char(created_at, 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"'),"
        "       to_char(updated_at, 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"')"
        " FROM files ORDER BY created_at DESC";

    PGresult *res = PQexec(db->conn, sql);
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        LOG_ERROR("list error: %s", PQerrorMessage(db->conn));
        PQclear(res);
        return -1;
    }

    int count = 0;
    int nrows = PQntuples(res);
    for (int i = 0; i < nrows; i++) {
        FileRecord rec;
        memset(&rec, 0, sizeof(rec));
        rec.id = strtoll(PQgetvalue(res, i, 0), NULL, 10);
        snprintf(rec.original_file_name, DB_ORIG_NAME_LEN, "%s", PQgetvalue(res, i, 1));
        rec.file_size_bytes = strtoll(PQgetvalue(res, i, 2), NULL, 10);
        snprintf(rec.saved_file_name, DB_FILENAME_LEN, "%s", PQgetvalue(res, i, 3));
        snprintf(rec.file_hash, DB_HASH_LEN, "%s", PQgetvalue(res, i, 4));
        rec.is_ocr_processed = strcmp(PQgetvalue(res, i, 5), "t") == 0;
        snprintf(rec.ocr_file_name, DB_OCR_FILENAME_LEN, "%s", PQgetvalue(res, i, 6));
        snprintf(rec.created_at, DB_DATE_LEN, "%s", PQgetvalue(res, i, 7));
        snprintf(rec.updated_at, DB_DATE_LEN, "%s", PQgetvalue(res, i, 8));
        cb(&rec, userdata);
        count++;
    }

    PQclear(res);
    LOG_DEBUG("listed %d files", count);
    return 0;
}

/* ── backend ops ──────────────────────────────────────────────────────────── */

static const DBBackendOps postgres_ops = {
    .open              = postgres_open,
    .close             = postgres_close,
    .find_by_hash      = postgres_find_by_hash,
    .insert            = postgres_insert,
    .mark_ocr_done     = postgres_mark_ocr_done,
    .mark_parsing_done = postgres_mark_parsing_done,
    .get_parsed_receipt = postgres_get_parsed_receipt,
    .list              = postgres_list
};

DBBackend *db_backend_postgres_open(const Config *cfg)
{
    DBBackend *backend = postgres_ops.open(cfg);
    if (backend) {
        backend->ops = &postgres_ops;
    }
    return backend;
}
