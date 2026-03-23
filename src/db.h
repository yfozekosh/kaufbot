#ifndef DB_H
#define DB_H

#include <stdint.h>

/* ── Database constants ───────────────────────────────────────────────────── */

#define DB_OCR_FILENAME_LEN 256
#define DB_HASH_LEN         65
#define DB_FILENAME_LEN     256
#define DB_ORIG_NAME_LEN    512
#define DB_DATE_LEN         32
#define DB_JSON_LEN         65536

/* ── Data structures ──────────────────────────────────────────────────────── */

typedef struct {
    int64_t id;
    char    original_file_name[DB_ORIG_NAME_LEN];
    int64_t file_size_bytes;
    char    saved_file_name[DB_FILENAME_LEN];
    char    file_hash[DB_HASH_LEN];
    int     is_ocr_processed;           /* 0 or 1 */
    char    ocr_file_name[DB_OCR_FILENAME_LEN];
    char    created_at[DB_DATE_LEN];
    char    updated_at[DB_DATE_LEN];
} FileRecord;

typedef struct {
    int64_t id;
    int64_t file_id;
    char    parsed_json[DB_JSON_LEN];
    char    created_at[DB_DATE_LEN];
    char    updated_at[DB_DATE_LEN];
} ParsedReceipt;

/* ── Legacy SQLite API (deprecated - use db_backend.h) ────────────────────── */

typedef struct DB DB;

DB *db_open(const char *path);
void db_close(DB *db);
int db_find_by_hash(DB *db, const char *hash, FileRecord *out);
int db_insert(DB *db, FileRecord *out);
int db_mark_ocr_done(DB *db, int64_t id, const char *ocr_file_name);
int db_mark_parsing_done(DB *db, int64_t file_id, const char *parsed_json);
int db_get_parsed_receipt(DB *db, int64_t file_id, ParsedReceipt *out);

typedef void (*db_list_cb)(const FileRecord *rec, void *userdata);
int db_list(DB *db, db_list_cb cb, void *userdata);

#endif /* DB_H */
