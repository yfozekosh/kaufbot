#ifndef STORAGE_BACKEND_H
#define STORAGE_BACKEND_H

#include "config.h"
#include "storage.h"

/* ── Storage backend interface ────────────────────────────────────────────── */

typedef struct StorageBackendImpl StorageBackend;

typedef struct {
    /* Lifecycle */
    StorageBackend *(*open)(const Config *cfg);
    void (*close)(StorageBackend *storage);

    /* Operations */
    int (*ensure_dirs)(StorageBackend *storage);
    int (*save_file)(StorageBackend *storage, const char *filename, const uint8_t *data,
                     size_t len);
    int (*save_text)(StorageBackend *storage, const char *filename, const char *text);
    int (*file_exists)(StorageBackend *storage, const char *filename);
    int (*delete_file)(StorageBackend *storage, const char *filename);
    char *(*get_public_url)(StorageBackend *storage, const char *filename);
    char *(*read_text)(StorageBackend *storage, const char *filename);
    uint8_t *(*read_binary)(StorageBackend *storage, const char *filename, size_t *out_len);
} StorageBackendOps;

struct StorageBackendImpl {
    const StorageBackendOps *ops;
    void *internal; /* Backend-specific data */
};

/* ── Backend implementations ──────────────────────────────────────────────── */

StorageBackend *storage_backend_local_open(const Config *cfg);

#ifdef HAVE_POSTGRES
StorageBackend *storage_backend_supabase_open(const Config *cfg);
#endif

/* ── Factory ──────────────────────────────────────────────────────────────── */

static inline StorageBackend *storage_backend_open(const Config *cfg) {
#ifdef HAVE_POSTGRES
    if (cfg->storage_backend == STORAGE_BACKEND_SUPABASE) {
        return storage_backend_supabase_open(cfg);
    } else
#else
    (void)cfg;
#endif
    {
        return storage_backend_local_open(cfg);
    }
}

static inline void storage_backend_close(StorageBackend *storage) {
    if (storage && storage->ops && storage->ops->close) {
        storage->ops->close(storage);
    }
}

static inline int storage_backend_ensure_dirs(StorageBackend *storage) {
    if (storage && storage->ops && storage->ops->ensure_dirs) {
        return storage->ops->ensure_dirs(storage);
    }
    return -1;
}

static inline int storage_backend_save_file(StorageBackend *storage, const char *filename,
                                            const uint8_t *data, size_t len) {
    if (storage && storage->ops && storage->ops->save_file) {
        return storage->ops->save_file(storage, filename, data, len);
    }
    return -1;
}

static inline int storage_backend_save_text(StorageBackend *storage, const char *filename,
                                            const char *text) {
    if (storage && storage->ops && storage->ops->save_text) {
        return storage->ops->save_text(storage, filename, text);
    }
    return -1;
}

static inline int storage_backend_file_exists(StorageBackend *storage, const char *filename) {
    if (storage && storage->ops && storage->ops->file_exists) {
        return storage->ops->file_exists(storage, filename);
    }
    return -1;
}

static inline int storage_backend_delete_file(StorageBackend *storage, const char *filename) {
    if (storage && storage->ops && storage->ops->delete_file) {
        return storage->ops->delete_file(storage, filename);
    }
    return -1;
}

static inline char *storage_backend_read_text(StorageBackend *storage, const char *filename) {
    if (storage && storage->ops && storage->ops->read_text) {
        return storage->ops->read_text(storage, filename);
    }
    return NULL;
}

static inline uint8_t *storage_backend_read_binary(StorageBackend *storage, const char *filename,
                                                   size_t *out_len) {
    if (storage && storage->ops && storage->ops->read_binary) {
        return storage->ops->read_binary(storage, filename, out_len);
    }
    if (out_len)
        *out_len = 0;
    return NULL;
}

static inline char *storage_backend_get_public_url(StorageBackend *storage, const char *filename) {
    if (storage && storage->ops && storage->ops->get_public_url) {
        return storage->ops->get_public_url(storage, filename);
    }
    return NULL;
}

#endif /* STORAGE_BACKEND_H */
