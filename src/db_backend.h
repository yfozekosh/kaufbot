#ifndef DB_BACKEND_H
#define DB_BACKEND_H

#include "db.h"
#include "config.h"

/* ── Database backend interface ───────────────────────────────────────────── */

typedef struct DBBackendImpl DBBackend;

typedef struct {
    /* Lifecycle */
    DBBackend *(*open)(const Config *cfg);
    void (*close)(DBBackend *db);
    
    /* Operations */
    int (*find_by_hash)(DBBackend *db, const char *hash, FileRecord *out);
    int (*insert)(DBBackend *db, FileRecord *rec);
    int (*mark_ocr_done)(DBBackend *db, int64_t id, const char *ocr_file_name);
    int (*mark_parsing_done)(DBBackend *db, int64_t file_id, const char *parsed_json);
    int (*get_parsed_receipt)(DBBackend *db, int64_t file_id, ParsedReceipt *out);
    int (*list)(DBBackend *db, db_list_cb cb, void *userdata);
} DBBackendOps;

struct DBBackendImpl {
    const DBBackendOps *ops;
    void *internal;  /* Backend-specific data */
};

/* ── Backend implementations ──────────────────────────────────────────────── */

DBBackend *db_backend_sqlite_open(const Config *cfg);

#ifdef HAVE_POSTGRES
DBBackend *db_backend_postgres_open(const Config *cfg);
#endif

/* ── Factory ──────────────────────────────────────────────────────────────── */

static inline DBBackend *db_backend_open(const Config *cfg)
{
#ifdef HAVE_POSTGRES
    if (cfg->db_backend == DB_BACKEND_POSTGRES) {
        return db_backend_postgres_open(cfg);
    } else
#else
    (void)cfg;
#endif
    {
        return db_backend_sqlite_open(cfg);
    }
}

static inline void db_backend_close(DBBackend *db)
{
    if (db && db->ops && db->ops->close) {
        db->ops->close(db);
    }
}

static inline int db_backend_find_by_hash(DBBackend *db, const char *hash, FileRecord *out)
{
    if (db && db->ops && db->ops->find_by_hash) {
        return db->ops->find_by_hash(db, hash, out);
    }
    return -1;
}

static inline int db_backend_insert(DBBackend *db, FileRecord *rec)
{
    if (db && db->ops && db->ops->insert) {
        return db->ops->insert(db, rec);
    }
    return -1;
}

static inline int db_backend_mark_ocr_done(DBBackend *db, int64_t id, const char *ocr_file_name)
{
    if (db && db->ops && db->ops->mark_ocr_done) {
        return db->ops->mark_ocr_done(db, id, ocr_file_name);
    }
    return -1;
}

static inline int db_backend_mark_parsing_done(DBBackend *db, int64_t file_id, const char *parsed_json)
{
    if (db && db->ops && db->ops->mark_parsing_done) {
        return db->ops->mark_parsing_done(db, file_id, parsed_json);
    }
    return -1;
}

static inline int db_backend_get_parsed_receipt(DBBackend *db, int64_t file_id, ParsedReceipt *out)
{
    if (db && db->ops && db->ops->get_parsed_receipt) {
        return db->ops->get_parsed_receipt(db, file_id, out);
    }
    return -1;
}

static inline int db_backend_list(DBBackend *db, db_list_cb cb, void *userdata)
{
    if (db && db->ops && db->ops->list) {
        return db->ops->list(db, cb, userdata);
    }
    return -1;
}

#endif /* DB_BACKEND_H */
