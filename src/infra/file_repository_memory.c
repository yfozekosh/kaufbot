#include "config.h"
#include "file_repository.h"
#include "storage_backend.h"

#include <stdlib.h>
#include <string.h>

/* ── Constants ───────────────────────────────────────────────────────────── */

#define MAX_MEMORY_RECORDS  100
#define MAX_MEMORY_RECEIPTS 100

/* ── Internal Structure ──────────────────────────────────────────────────── */

typedef struct {
    FileRecord records[MAX_MEMORY_RECORDS];
    ParsedReceipt receipts[MAX_MEMORY_RECEIPTS];
    int count;
    int receipt_count;
    int64_t next_id;
    int64_t next_receipt_id;
    StorageBackend *storage;
} MemoryFileRepository;

/* ── Memory Implementation ──────────────────────────────────────────────── */

static int mem_repo_find_by_hash(void *ctx, const char *hash, FileRecord *out) {
    if (!ctx || !hash || !out)
        return FILE_REPO_ERR_INVALID_ARG;

    MemoryFileRepository *repo = (MemoryFileRepository *)ctx;
    for (int i = 0; i < repo->count; i++) {
        if (strcmp(repo->records[i].file_hash, hash) == 0) {
            *out = repo->records[i];
            return FILE_REPO_OK;
        }
    }
    return FILE_REPO_ERR_NOT_FOUND;
}

static int mem_repo_find_by_id(void *ctx, int64_t id, FileRecord *out) {
    if (!ctx || !out)
        return FILE_REPO_ERR_INVALID_ARG;

    MemoryFileRepository *repo = (MemoryFileRepository *)ctx;
    for (int i = 0; i < repo->count; i++) {
        if (repo->records[i].id == id) {
            *out = repo->records[i];
            return FILE_REPO_OK;
        }
    }
    return FILE_REPO_ERR_NOT_FOUND;
}

static int mem_repo_insert(void *ctx, const char *original_name, const char *hash,
                           const char *saved_filename, int64_t *out_id) {
    if (!ctx || !original_name || !hash || !saved_filename || !out_id) {
        return FILE_REPO_ERR_INVALID_ARG;
    }

    MemoryFileRepository *repo = (MemoryFileRepository *)ctx;
    if (repo->count >= MAX_MEMORY_RECORDS) {
        return FILE_REPO_ERR_MEMORY;
    }

    /* Check for duplicate hash */
    for (int i = 0; i < repo->count; i++) {
        if (strcmp(repo->records[i].file_hash, hash) == 0) {
            return FILE_REPO_ERR_DUPLICATE;
        }
    }

    FileRecord *rec = &repo->records[repo->count];
    memset(rec, 0, sizeof(FileRecord));
    rec->id = repo->next_id++;
    strncpy(rec->original_file_name, original_name, sizeof(rec->original_file_name) - 1);
    strncpy(rec->file_hash, hash, sizeof(rec->file_hash) - 1);
    strncpy(rec->saved_file_name, saved_filename, sizeof(rec->saved_file_name) - 1);
    rec->is_ocr_processed = 0;
    rec->ocr_file_name[0] = '\0';

    repo->count++;
    *out_id = rec->id;
    return FILE_REPO_OK;
}

static int mem_repo_mark_ocr_complete(void *ctx, int64_t id, const char *ocr_filename) {
    if (!ctx || !ocr_filename)
        return FILE_REPO_ERR_INVALID_ARG;

    MemoryFileRepository *repo = (MemoryFileRepository *)ctx;
    for (int i = 0; i < repo->count; i++) {
        if (repo->records[i].id == id) {
            repo->records[i].is_ocr_processed = 1;
            strncpy(repo->records[i].ocr_file_name, ocr_filename,
                    sizeof(repo->records[i].ocr_file_name) - 1);
            return FILE_REPO_OK;
        }
    }
    return FILE_REPO_ERR_NOT_FOUND;
}

static int mem_repo_mark_parsing_complete(void *ctx, int64_t id, const char *parsed_json) {
    if (!ctx || !parsed_json)
        return FILE_REPO_ERR_INVALID_ARG;

    MemoryFileRepository *repo = (MemoryFileRepository *)ctx;

    /* Verify the file exists */
    int found = 0;
    for (int i = 0; i < repo->count; i++) {
        if (repo->records[i].id == id) {
            found = 1;
            break;
        }
    }
    if (!found)
        return FILE_REPO_ERR_NOT_FOUND;

    /* Store parsed receipt */
    if (repo->receipt_count >= MAX_MEMORY_RECEIPTS) {
        return FILE_REPO_ERR_MEMORY;
    }

    ParsedReceipt *pr = &repo->receipts[repo->receipt_count];
    memset(pr, 0, sizeof(ParsedReceipt));
    pr->id = repo->next_receipt_id++;
    pr->file_id = id;
    strncpy(pr->parsed_json, parsed_json, sizeof(pr->parsed_json) - 1);
    repo->receipt_count++;
    return FILE_REPO_OK;
}

static int mem_repo_delete_by_id(void *ctx, int64_t id) {
    if (!ctx)
        return FILE_REPO_ERR_INVALID_ARG;

    MemoryFileRepository *repo = (MemoryFileRepository *)ctx;
    for (int i = 0; i < repo->count; i++) {
        if (repo->records[i].id == id) {
            /* Shift remaining records down */
            for (int j = i; j < repo->count - 1; j++) {
                repo->records[j] = repo->records[j + 1];
            }
            repo->count--;
            return FILE_REPO_OK;
        }
    }
    return FILE_REPO_ERR_NOT_FOUND;
}

static int mem_repo_list(void *ctx, file_record_cb cb, void *userdata, int limit) {
    if (!ctx || !cb)
        return FILE_REPO_ERR_INVALID_ARG;

    MemoryFileRepository *repo = (MemoryFileRepository *)ctx;
    int count = 0;
    int max = (limit > 0 && limit < repo->count) ? limit : repo->count;

    /* Return most recent first (reverse order) */
    for (int i = repo->count - 1; i >= 0 && count < max; i--) {
        cb(&repo->records[i], userdata);
        count++;
    }
    return FILE_REPO_OK;
}

static StorageBackend *mem_repo_get_storage(void *ctx) {
    if (!ctx)
        return NULL;
    MemoryFileRepository *repo = (MemoryFileRepository *)ctx;
    return repo->storage;
}

/* ── VTable ──────────────────────────────────────────────────────────────── */

static const FileRepositoryOps mem_repo_ops = {
    .find_by_hash = mem_repo_find_by_hash,
    .find_by_id = mem_repo_find_by_id,
    .insert = mem_repo_insert,
    .mark_ocr_complete = mem_repo_mark_ocr_complete,
    .mark_parsing_complete = mem_repo_mark_parsing_complete,
    .delete_by_id = mem_repo_delete_by_id,
    .list = mem_repo_list,
    .get_storage = mem_repo_get_storage,
};

/* ── Public API ──────────────────────────────────────────────────────────── */

FileRepository *file_repository_memory_new(StorageBackend *storage) {
    MemoryFileRepository *repo = calloc(1, sizeof(MemoryFileRepository));
    if (!repo)
        return NULL;

    repo->next_id = 1;
    repo->next_receipt_id = 1;
    repo->count = 0;
    repo->receipt_count = 0;
    repo->storage = storage;

    FileRepository *public = calloc(1, sizeof(FileRepository));
    if (!public) {
        free(repo);
        return NULL;
    }

    public->ops = &mem_repo_ops;
    public->internal = repo;

    return public;
}

void file_repository_memory_free(FileRepository *repo) {
    if (!repo)
        return;
    free(repo->internal);
    free(repo);
}

/* ── Test Helpers ───────────────────────────────────────────────────────── */

int file_repository_memory_count(FileRepository *repo) {
    if (!repo || !repo->internal)
        return -1;
    MemoryFileRepository *mem = (MemoryFileRepository *)repo->internal;
    return mem->count;
}

FileRecord *file_repository_memory_get(FileRepository *repo, int index) {
    if (!repo || !repo->internal)
        return NULL;
    MemoryFileRepository *mem = (MemoryFileRepository *)repo->internal;
    if (index < 0 || index >= mem->count)
        return NULL;
    return &mem->records[index];
}

void file_repository_memory_clear(FileRepository *repo) {
    if (!repo || !repo->internal)
        return;
    MemoryFileRepository *mem = (MemoryFileRepository *)repo->internal;
    mem->count = 0;
    mem->receipt_count = 0;
    mem->next_id = 1;
    mem->next_receipt_id = 1;
}
