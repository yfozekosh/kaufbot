#ifndef DOCUMENT_STORE_H
#define DOCUMENT_STORE_H

#include <stddef.h>
#include <stdint.h>

/* ── Error Codes ──────────────────────────────────────────────────────────── */

typedef enum {
    DOC_STORE_OK = 0,
    DOC_STORE_ERR_MEMORY = -1,
    DOC_STORE_ERR_INVALID_ARG = -2,
    DOC_STORE_ERR_STORAGE = -3,
    DOC_STORE_ERR_NOT_FOUND = -4,
} DocStoreError;

/* ── Forward Declarations ─────────────────────────────────────────────────── */

typedef struct DocumentStore DocumentStore;

/* ── Document Store Interface ─────────────────────────────────────────────── */

typedef struct {
    /* Store original document. Generates a unique doc_id.
     * Returns DOC_STORE_OK on success, doc_id is filled with the generated ID.
     * doc_id must be at least 64 bytes. */
    int (*store_document)(void *ctx, const char *original_name, const uint8_t *data, size_t len,
                          char *out_doc_id, size_t doc_id_len);

    /* Store associated text (OCR result, parsed JSON) for a document.
     * text_type identifies the type (e.g., "ocr", "parsed"). */
    int (*store_text)(void *ctx, const char *doc_id, const char *text_type, const char *text);

    /* Read stored text for a document. Caller must free the returned string.
     * Returns NULL if not found. */
    char *(*read_text)(void *ctx, const char *doc_id, const char *text_type);

    /* Delete document and all associated files. */
    int (*delete_document)(void *ctx, const char *doc_id);

    /* Check if a document exists. Returns 1 if exists, 0 if not. */
    int (*exists)(void *ctx, const char *doc_id);
} DocumentStoreOps;

struct DocumentStore {
    const DocumentStoreOps *ops;
    void *internal;
};

/* ── Convenience Wrappers ─────────────────────────────────────────────────── */

static inline int doc_store_store(DocumentStore *store, const char *original_name,
                                  const uint8_t *data, size_t len, char *out_doc_id,
                                  size_t doc_id_len) {
    if (!store || !store->ops || !original_name || !data || !out_doc_id) {
        return DOC_STORE_ERR_INVALID_ARG;
    }
    return store->ops->store_document(store->internal, original_name, data, len, out_doc_id,
                                      doc_id_len);
}

static inline int doc_store_store_text(DocumentStore *store, const char *doc_id,
                                       const char *text_type, const char *text) {
    if (!store || !store->ops || !doc_id || !text_type || !text) {
        return DOC_STORE_ERR_INVALID_ARG;
    }
    return store->ops->store_text(store->internal, doc_id, text_type, text);
}

static inline char *doc_store_read_text(DocumentStore *store, const char *doc_id,
                                        const char *text_type) {
    if (!store || !store->ops || !doc_id || !text_type)
        return NULL;
    return store->ops->read_text(store->internal, doc_id, text_type);
}

static inline int doc_store_delete(DocumentStore *store, const char *doc_id) {
    if (!store || !store->ops || !doc_id)
        return DOC_STORE_ERR_INVALID_ARG;
    return store->ops->delete_document(store->internal, doc_id);
}

static inline int doc_store_exists(DocumentStore *store, const char *doc_id) {
    if (!store || !store->ops || !doc_id)
        return 0;
    return store->ops->exists(store->internal, doc_id);
}

/* ── StorageBackend Implementation ────────────────────────────────────────── */

#include "storage_backend.h"

/* Create a document store backed by a StorageBackend */
DocumentStore *document_store_new(StorageBackend *storage);

/* Free the document store (does not close the underlying storage) */
void document_store_free(DocumentStore *store);

#endif /* DOCUMENT_STORE_H */
