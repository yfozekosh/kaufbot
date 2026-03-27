#ifndef FILE_REPOSITORY_H
#define FILE_REPOSITORY_H

#include "db.h"
#include "db_backend.h"
#include "storage_backend.h"
#include <stdbool.h>
#include <stdint.h>

/* ── Error Codes ─────────────────────────────────────────────────────────── */

typedef enum {
    FILE_REPO_OK = 0,
    FILE_REPO_ERR_MEMORY = -1,
    FILE_REPO_ERR_NOT_FOUND = -2,
    FILE_REPO_ERR_DUPLICATE = -3,
    FILE_REPO_ERR_INVALID_ARG = -4,
    FILE_REPO_ERR_DB = -5,
    FILE_REPO_ERR_STORAGE = -6,
} FileRepoError;

/* ── Forward Declarations ────────────────────────────────────────────────── */

typedef struct FileRepository FileRepository;

/* ── Callback Types ──────────────────────────────────────────────────────── */

/* Callback for listing files */
typedef void (*file_record_cb)(const FileRecord *rec, void *userdata);

/* ── Repository Interface ────────────────────────────────────────────────── */

typedef struct {
    /* Find a file by its hash. Returns FILE_REPO_OK on success, FILE_REPO_ERR_NOT_FOUND if not
     * found */
    int (*find_by_hash)(void *ctx, const char *hash, FileRecord *out);

    /* Find a file by ID. Returns FILE_REPO_OK on success, FILE_REPO_ERR_NOT_FOUND if not found */
    int (*find_by_id)(void *ctx, int64_t id, FileRecord *out);

    /* Insert a new file record. Returns FILE_REPO_OK on success, sets *out_id to the new ID */
    int (*insert)(void *ctx, const char *original_name, const char *hash,
                  const char *saved_filename, int64_t *out_id);

    /* Mark OCR as complete for a file. Returns FILE_REPO_OK on success */
    int (*mark_ocr_complete)(void *ctx, int64_t id, const char *ocr_filename);

    /* Mark parsing as complete for a file. Returns FILE_REPO_OK on success */
    int (*mark_parsing_complete)(void *ctx, int64_t id, const char *parsed_json);

    /* Delete a file record by ID. Returns FILE_REPO_OK on success */
    int (*delete_by_id)(void *ctx, int64_t id);

    /* List files (most recent first). Calls cb for each record */
    int (*list)(void *ctx, file_record_cb cb, void *userdata, int limit);

    /* Get storage backend associated with this repository */
    StorageBackend *(*get_storage)(void *ctx);
} FileRepositoryOps;

struct FileRepository {
    const FileRepositoryOps *ops;
    void *internal; /* Implementation-specific data */
};

/* ── DB-Backed Repository Implementation ─────────────────────────────────── */

/* Create a file repository backed by DBBackend and StorageBackend */
FileRepository *file_repository_db_backend(DBBackend *db, StorageBackend *storage);

/* Free the repository (does not close underlying db/storage) */
void file_repository_free(FileRepository *repo);

/* ── Memory-Backed Repository (for testing) ──────────────────────────────── */

/* Create an in-memory file repository for testing */
FileRepository *file_repository_memory_new(StorageBackend *storage);

/* Free memory repository */
void file_repository_memory_free(FileRepository *repo);

/* Test helpers */
int file_repository_memory_count(FileRepository *repo);
FileRecord *file_repository_memory_get(FileRepository *repo, int index);
void file_repository_memory_clear(FileRepository *repo);

/* ── Convenience Wrappers ────────────────────────────────────────────────── */

/* Inline wrappers for cleaner calling code */

static inline int file_repo_find_by_hash(FileRepository *repo, const char *hash, FileRecord *out) {
    if (!repo || !repo->ops || !hash || !out)
        return FILE_REPO_ERR_INVALID_ARG;
    return repo->ops->find_by_hash(repo->internal, hash, out);
}

static inline int file_repo_find_by_id(FileRepository *repo, int64_t id, FileRecord *out) {
    if (!repo || !repo->ops || !out)
        return FILE_REPO_ERR_INVALID_ARG;
    return repo->ops->find_by_id(repo->internal, id, out);
}

static inline int file_repo_insert(FileRepository *repo, const char *original_name,
                                   const char *hash, const char *saved_filename, int64_t *out_id) {
    if (!repo || !repo->ops || !original_name || !hash || !saved_filename || !out_id) {
        return FILE_REPO_ERR_INVALID_ARG;
    }
    return repo->ops->insert(repo->internal, original_name, hash, saved_filename, out_id);
}

static inline int file_repo_mark_ocr_complete(FileRepository *repo, int64_t id,
                                              const char *ocr_filename) {
    if (!repo || !repo->ops || !ocr_filename)
        return FILE_REPO_ERR_INVALID_ARG;
    return repo->ops->mark_ocr_complete(repo->internal, id, ocr_filename);
}

static inline int file_repo_mark_parsing_complete(FileRepository *repo, int64_t id,
                                                  const char *parsed_json) {
    if (!repo || !repo->ops || !parsed_json)
        return FILE_REPO_ERR_INVALID_ARG;
    return repo->ops->mark_parsing_complete(repo->internal, id, parsed_json);
}

static inline int file_repo_delete_by_id(FileRepository *repo, int64_t id) {
    if (!repo || !repo->ops)
        return FILE_REPO_ERR_INVALID_ARG;
    return repo->ops->delete_by_id(repo->internal, id);
}

static inline int file_repo_list(FileRepository *repo, file_record_cb cb, void *userdata,
                                 int limit) {
    if (!repo || !repo->ops || !cb)
        return FILE_REPO_ERR_INVALID_ARG;
    return repo->ops->list(repo->internal, cb, userdata, limit);
}

static inline StorageBackend *file_repo_get_storage(FileRepository *repo) {
    if (!repo || !repo->ops)
        return NULL;
    return repo->ops->get_storage(repo->internal);
}

#endif /* FILE_REPOSITORY_H */
