#include "document_store.h"
#include "storage.h"
#include "storage_backend.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Internal Structure ───────────────────────────────────────────────────── */

typedef struct {
    StorageBackend *storage;
} StorageDocumentStore;

/* ── Implementation ───────────────────────────────────────────────────────── */

static int storage_doc_store_document(void *ctx, const char *original_name, const uint8_t *data,
                                      size_t len, char *out_doc_id, size_t doc_id_len) {
    if (!ctx || !original_name || !data || !out_doc_id) {
        return DOC_STORE_ERR_INVALID_ARG;
    }

    StorageDocumentStore *store = (StorageDocumentStore *)ctx;

    /* Generate filename from original name */
    const char *ext = strrchr(original_name, '.');
    char saved_name[MAX_FILENAME];
    storage_gen_filename(ext ? ext : "", saved_name, sizeof(saved_name));

    if (storage_backend_save_file(store->storage, saved_name, data, len) != 0) {
        return DOC_STORE_ERR_STORAGE;
    }

    /* Use saved filename as document ID */
    snprintf(out_doc_id, doc_id_len, "%s", saved_name);
    return DOC_STORE_OK;
}

static int storage_doc_store_text(void *ctx, const char *doc_id, const char *text_type,
                                  const char *text) {
    if (!ctx || !doc_id || !text_type || !text) {
        return DOC_STORE_ERR_INVALID_ARG;
    }

    StorageDocumentStore *store = (StorageDocumentStore *)ctx;

    /* Build text filename: {doc_id}_{text_type}.txt */
    char text_filename[MAX_FILENAME];
    snprintf(text_filename, sizeof(text_filename), "%s_%s.txt", doc_id, text_type);

    if (storage_backend_save_text(store->storage, text_filename, text) != 0) {
        return DOC_STORE_ERR_STORAGE;
    }
    return DOC_STORE_OK;
}

static char *storage_doc_read_text(void *ctx, const char *doc_id, const char *text_type) {
    if (!ctx || !doc_id || !text_type)
        return NULL;

    StorageDocumentStore *store = (StorageDocumentStore *)ctx;

    char text_filename[MAX_FILENAME];
    snprintf(text_filename, sizeof(text_filename), "%s_%s.txt", doc_id, text_type);

    return storage_backend_read_text(store->storage, text_filename);
}

static int storage_doc_delete(void *ctx, const char *doc_id) {
    if (!ctx || !doc_id)
        return DOC_STORE_ERR_INVALID_ARG;

    StorageDocumentStore *store = (StorageDocumentStore *)ctx;

    /* Delete the main file */
    storage_backend_delete_file(store->storage, doc_id);

    /* Try to delete associated text files (best-effort) */
    char text_filename[MAX_FILENAME];
    snprintf(text_filename, sizeof(text_filename), "%s_ocr.txt", doc_id);
    storage_backend_delete_file(store->storage, text_filename);

    snprintf(text_filename, sizeof(text_filename), "%s_parsed.txt", doc_id);
    storage_backend_delete_file(store->storage, text_filename);

    return DOC_STORE_OK;
}

static int storage_doc_exists(void *ctx, const char *doc_id) {
    if (!ctx || !doc_id)
        return 0;

    StorageDocumentStore *store = (StorageDocumentStore *)ctx;
    return storage_backend_file_exists(store->storage, doc_id);
}

/* ── VTable ───────────────────────────────────────────────────────────────── */

static const DocumentStoreOps storage_doc_ops = {
    .store_document = storage_doc_store_document,
    .store_text = storage_doc_store_text,
    .read_text = storage_doc_read_text,
    .delete_document = storage_doc_delete,
    .exists = storage_doc_exists,
};

/* ── Public API ───────────────────────────────────────────────────────────── */

DocumentStore *document_store_new(StorageBackend *storage) {
    if (!storage)
        return NULL;

    StorageDocumentStore *impl = calloc(1, sizeof(StorageDocumentStore));
    if (!impl)
        return NULL;

    impl->storage = storage;

    DocumentStore *store = calloc(1, sizeof(DocumentStore));
    if (!store) {
        free(impl);
        return NULL;
    }

    store->ops = &storage_doc_ops;
    store->internal = impl;

    return store;
}

void document_store_free(DocumentStore *store) {
    if (!store)
        return;
    free(store->internal);
    free(store);
}
