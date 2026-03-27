#include "file_repository.h"
#include "config.h"
#include "db_backend.h"
#include "storage_backend.h"

#include <stdlib.h>
#include <string.h>

/* ── Internal Structure ──────────────────────────────────────────────────── */

typedef struct {
    DBBackend *db;
    StorageBackend *storage;
} DBFileRepository;

/* ── DB-Backed Implementation ────────────────────────────────────────────── */

static int db_repo_find_by_hash(void *ctx, const char *hash, FileRecord *out) {
    if (!ctx || !hash || !out)
        return FILE_REPO_ERR_INVALID_ARG;

    DBFileRepository *repo = (DBFileRepository *)ctx;
    int rc = db_backend_find_by_hash(repo->db, hash, out);
    return (rc == 0) ? FILE_REPO_OK : FILE_REPO_ERR_NOT_FOUND;
}

static int db_repo_find_by_id(void *ctx, int64_t id, FileRecord *out) {
    if (!ctx || !out)
        return FILE_REPO_ERR_INVALID_ARG;

    DBFileRepository *repo = (DBFileRepository *)ctx;
    int rc = db_backend_find_by_id(repo->db, id, out);
    return (rc == 0) ? FILE_REPO_OK : FILE_REPO_ERR_NOT_FOUND;
}

static int db_repo_insert(void *ctx, const char *original_name, const char *hash,
                          const char *saved_filename, int64_t *out_id) {
    if (!ctx || !original_name || !hash || !saved_filename || !out_id) {
        return FILE_REPO_ERR_INVALID_ARG;
    }

    DBFileRepository *repo = (DBFileRepository *)ctx;

    FileRecord rec;
    memset(&rec, 0, sizeof(rec));
    snprintf(rec.original_file_name, DB_ORIG_NAME_LEN, "%s", original_name);
    snprintf(rec.file_hash, DB_HASH_LEN, "%s", hash);
    snprintf(rec.saved_file_name, DB_FILENAME_LEN, "%s", saved_filename);
    rec.is_ocr_processed = 0;

    int rc = db_backend_insert(repo->db, &rec);
    if (rc != 0) {
        return FILE_REPO_ERR_DB;
    }
    *out_id = rec.id;
    return FILE_REPO_OK;
}

static int db_repo_mark_ocr_complete(void *ctx, int64_t id, const char *ocr_filename) {
    if (!ctx || !ocr_filename)
        return FILE_REPO_ERR_INVALID_ARG;

    DBFileRepository *repo = (DBFileRepository *)ctx;
    int rc = db_backend_mark_ocr_done(repo->db, id, ocr_filename);
    return (rc == 0) ? FILE_REPO_OK : FILE_REPO_ERR_DB;
}

static int db_repo_mark_parsing_complete(void *ctx, int64_t id, const char *parsed_json) {
    if (!ctx || !parsed_json)
        return FILE_REPO_ERR_INVALID_ARG;

    DBFileRepository *repo = (DBFileRepository *)ctx;
    int rc = db_backend_mark_parsing_done(repo->db, id, parsed_json);
    return (rc == 0) ? FILE_REPO_OK : FILE_REPO_ERR_DB;
}

static int db_repo_delete_by_id(void *ctx, int64_t id) {
    if (!ctx)
        return FILE_REPO_ERR_INVALID_ARG;

    DBFileRepository *repo = (DBFileRepository *)ctx;
    int rc = db_backend_delete_file(repo->db, id);
    return (rc == 0) ? FILE_REPO_OK : FILE_REPO_ERR_DB;
}

/* Callback adapter for listing */
typedef struct {
    file_record_cb user_cb;
    void *user_data;
    int count;
    int limit;
} ListCallbackCtx;

static void list_adapter_cb(const FileRecord *rec, void *userdata) {
    ListCallbackCtx *ctx = (ListCallbackCtx *)userdata;
    if (!ctx || !ctx->user_cb)
        return;
    /* Stop early if limit is reached */
    if (ctx->limit > 0 && ctx->count >= ctx->limit)
        return;
    ctx->user_cb(rec, ctx->user_data);
    ctx->count++;
}

static int db_repo_list(void *ctx, file_record_cb cb, void *userdata, int limit) {
    if (!ctx || !cb)
        return FILE_REPO_ERR_INVALID_ARG;

    DBFileRepository *repo = (DBFileRepository *)ctx;

    ListCallbackCtx adapter_ctx = {cb, userdata, 0, limit};
    db_backend_list(repo->db, list_adapter_cb, &adapter_ctx);
    return FILE_REPO_OK;
}

static StorageBackend *db_repo_get_storage(void *ctx) {
    if (!ctx)
        return NULL;
    DBFileRepository *repo = (DBFileRepository *)ctx;
    return repo->storage;
}

/* ── VTable ──────────────────────────────────────────────────────────────── */

static const FileRepositoryOps db_repo_ops = {
    .find_by_hash = db_repo_find_by_hash,
    .find_by_id = db_repo_find_by_id,
    .insert = db_repo_insert,
    .mark_ocr_complete = db_repo_mark_ocr_complete,
    .mark_parsing_complete = db_repo_mark_parsing_complete,
    .delete_by_id = db_repo_delete_by_id,
    .list = db_repo_list,
    .get_storage = db_repo_get_storage,
};

/* ── Public API ──────────────────────────────────────────────────────────── */

FileRepository *file_repository_db_backend(DBBackend *db, StorageBackend *storage) {
    if (!db || !storage)
        return NULL;

    DBFileRepository *repo = calloc(1, sizeof(DBFileRepository));
    if (!repo)
        return NULL;

    repo->db = db;
    repo->storage = storage;

    FileRepository *public = calloc(1, sizeof(FileRepository));
    if (!public) {
        free(repo);
        return NULL;
    }

    public->ops = &db_repo_ops;
    public->internal = repo;

    return public;
}

void file_repository_free(FileRepository *repo) {
    if (!repo)
        return;
    free(repo->internal);
    free(repo);
}
