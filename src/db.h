#ifndef DB_H
#define DB_H

#include <stdint.h>
#include <sqlite3.h>

#define DB_OCR_FILENAME_LEN 256
#define DB_HASH_LEN         65
#define DB_FILENAME_LEN     256
#define DB_ORIG_NAME_LEN    512
#define DB_DATE_LEN         32

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

typedef struct DB DB;

/* Open (or create) the SQLite DB at path and run migrations.
 * Returns pointer on success, NULL on error. */
DB *db_open(const char *path);

/* Close the database. */
void db_close(DB *db);

/* Find a record by SHA-256 hash. Returns 0 and fills *out if found.
 * Returns 1 if not found. Returns -1 on error. */
int db_find_by_hash(DB *db, const char *hash, FileRecord *out);

/* Insert a new record. Fills out->id on success.
 * Returns 0 on success, -1 on error. */
int db_insert(DB *db, FileRecord *out);

/* Mark OCR as done and set ocr_file_name.
 * Returns 0 on success, -1 on error. */
int db_mark_ocr_done(DB *db, int64_t id, const char *ocr_file_name);

/* List all records, newest first.
 * Calls callback(record, userdata) for each row.
 * Returns 0 on success, -1 on error. */
typedef void (*db_list_cb)(const FileRecord *rec, void *userdata);
int db_list(DB *db, db_list_cb cb, void *userdata);

#endif /* DB_H */
