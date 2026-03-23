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
    int     is_ocr_processed;
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

typedef void (*db_list_cb)(const FileRecord *rec, void *userdata);

#endif /* DB_H */
