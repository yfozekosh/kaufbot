#include "config.h"
#include "storage_backend.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

/* Forward declaration */
static const StorageBackendOps local_ops;

typedef struct {
    char base_path[MAX_PATH_LEN];
} LocalStorage;

/* ── SHA-256 ──────────────────────────────────────────────────────────────── */

typedef struct {
    uint32_t state[8];
    uint64_t count;
    uint8_t buf[64];
} SHA256_CTX;

static const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

#define ROR32(x, n)  (((x) >> (n)) | ((x) << (32 - (n))))
#define CH(x, y, z)  (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define S0(x)        (ROR32(x, 2) ^ ROR32(x, 13) ^ ROR32(x, 22))
#define S1(x)        (ROR32(x, 6) ^ ROR32(x, 11) ^ ROR32(x, 25))
#define s0(x)        (ROR32(x, 7) ^ ROR32(x, 18) ^ ((x) >> 3))
#define s1(x)        (ROR32(x, 17) ^ ROR32(x, 19) ^ ((x) >> 10))

static void sha256_transform(SHA256_CTX *ctx, const uint8_t *data) {
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    uint32_t e;
    uint32_t f;
    uint32_t g;
    uint32_t h;
    uint32_t t1;
    uint32_t t2;
    uint32_t m[64];
    int i;
    for (i = 0; i < 16; i++) {
        size_t base = (size_t)i * 4;
        m[i] = ((uint32_t)data[base] << 24) | ((uint32_t)data[base + 1] << 16) |
               ((uint32_t)data[base + 2] << 8) | (uint32_t)data[base + 3];
    }
    for (; i < 64; i++)
        m[i] = s1(m[i - 2]) + m[i - 7] + s0(m[i - 15]) + m[i - 16];

    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];
    f = ctx->state[5];
    g = ctx->state[6];
    h = ctx->state[7];

    for (i = 0; i < 64; i++) {
        t1 = h + S1(e) + CH(e, f, g) + K[i] + m[i];
        t2 = S0(a) + MAJ(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

static void sha256_init(SHA256_CTX *ctx) {
    ctx->count = 0;
    ctx->state[0] = 0x6a09e667;
    ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372;
    ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f;
    ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab;
    ctx->state[7] = 0x5be0cd19;
}

static void sha256_update(SHA256_CTX *ctx, const uint8_t *data, size_t len) {
    size_t i;
    for (i = 0; i < len; i++) {
        ctx->buf[ctx->count % 64] = data[i];
        ctx->count++;
        if (ctx->count % 64 == 0)
            sha256_transform(ctx, ctx->buf);
    }
}

static void sha256_final(SHA256_CTX *ctx, uint8_t *digest) {
    uint64_t bit_count = ctx->count * 8;
    uint8_t pad = 0x80;
    sha256_update(ctx, &pad, 1);
    pad = 0;
    while (ctx->count % 64 != 56)
        sha256_update(ctx, &pad, 1);
    uint8_t bc[8];
    for (int i = 7; i >= 0; i--) {
        bc[i] = (uint8_t)(bit_count & 0xff);
        bit_count >>= 8;
    }
    sha256_update(ctx, bc, 8);
    for (int j = 0; j < 8; j++)
        for (int i = 0; i < 4; i++)
            digest[((size_t)j * 4) + i] = (ctx->state[j] >> (24 - i * 8)) & 0xff;
}

void storage_sha256_hex(const uint8_t *data, size_t len, char *out) {
    LOG_DEBUG("computing SHA256 for %zu bytes", len);
    SHA256_CTX ctx;
    uint8_t digest[32];
    sha256_init(&ctx);
    sha256_update(&ctx, data, len);
    sha256_final(&ctx, digest);
    for (int i = 0; i < 32; i++)
        snprintf(out + ((size_t)i * 2), 3, "%02x", digest[i]);
    out[64] = '\0';
    LOG_DEBUG("SHA256: %s", out);
}

/* ── local storage implementation ────────────────────────────────────────── */

static StorageBackend *local_open(const Config *cfg) {
    LOG_INFO("opening local storage: %s", cfg->storage_path);

    LocalStorage *storage = calloc(1, sizeof(LocalStorage));
    if (!storage) {
        LOG_ERROR("failed to allocate storage");
        return NULL;
    }
    snprintf(storage->base_path, sizeof(storage->base_path), "%s", cfg->storage_path);

    StorageBackend *backend = calloc(1, sizeof(StorageBackend));
    if (!backend) {
        free(storage);
        return NULL;
    }
    backend->ops = &local_ops;
    backend->internal = storage;
    return backend;
}

static void local_close(StorageBackend *backend) {
    if (!backend)
        return;
    LocalStorage *storage = (LocalStorage *)backend->internal;
    free(storage);
    free(backend);
}

static int mkdir_p(const char *path) {
    char *buf = strdup(path);
    if (!buf)
        return -1;

    char *p = buf;
    if (*p == '/')
        p++;

    while (*p) {
        while (*p && *p != '/')
            p++;
        if (*p == '/') {
            *p = '\0';
            if (mkdir(buf, 0755) != 0 && errno != EEXIST) {
                LOG_ERROR("mkdir failed for %s: %s", buf, strerror(errno));
                free(buf);
                return -1;
            }
            LOG_DEBUG("created directory: %s", buf);
            *p = '/';
            p++;
        }
    }
    if (mkdir(buf, 0755) != 0 && errno != EEXIST) {
        LOG_ERROR("mkdir failed for %s: %s", buf, strerror(errno));
        free(buf);
        return -1;
    }
    LOG_DEBUG("created directory: %s", buf);
    free(buf);
    return 0;
}

static int local_ensure_dirs(StorageBackend *backend) {
    LocalStorage *storage = (LocalStorage *)backend->internal;
    const char *base_path = storage->base_path;

    struct stat st;
    if (stat(base_path, &st) == 0) {
        LOG_DEBUG("storage directory already exists: %s", base_path);
        return 0;
    }

    LOG_INFO("creating storage directory: %s", base_path);
    if (mkdir_p(base_path) != 0)
        return -1;

    LOG_INFO("storage directory created successfully");
    return 0;
}

static int local_save_file(StorageBackend *backend, const char *filename, const uint8_t *data,
                           size_t len) {
    LocalStorage *storage = (LocalStorage *)backend->internal;
    char full_path[MAX_PATH_LEN * 2];
    snprintf(full_path, sizeof(full_path), "%s/%s", storage->base_path, filename);

    LOG_DEBUG("saving file: %s (%zu bytes)", filename, len);
    FILE *f = fopen(full_path, "wb");
    if (!f) {
        LOG_ERROR("fopen failed for %s: %s", full_path, strerror(errno));
        return -1;
    }
    size_t written = fwrite(data, 1, len, f);
    fclose(f);

    if (written != len) {
        LOG_ERROR("short write for %s: %zu / %zu bytes", filename, written, len);
        return -1;
    }
    LOG_DEBUG("file saved successfully: %s", filename);
    return 0;
}

static int local_save_text(StorageBackend *backend, const char *filename, const char *text) {
    return local_save_file(backend, filename, (const uint8_t *)text, strlen(text));
}

static int local_file_exists(StorageBackend *backend, const char *filename) {
    LocalStorage *storage = (LocalStorage *)backend->internal;
    char full_path[MAX_PATH_LEN * 2];
    snprintf(full_path, sizeof(full_path), "%s/%s", storage->base_path, filename);

    struct stat st;
    return (stat(full_path, &st) == 0) ? 1 : 0;
}

static int local_delete_file(StorageBackend *backend, const char *filename) {
    LocalStorage *storage = (LocalStorage *)backend->internal;
    char full_path[MAX_PATH_LEN * 2];
    snprintf(full_path, sizeof(full_path), "%s/%s", storage->base_path, filename);

    LOG_DEBUG("deleting file: %s", full_path);
    if (remove(full_path) != 0) {
        if (errno == ENOENT) {
            LOG_DEBUG("file not found: %s", full_path);
            return 1; /* not found */
        }
        LOG_ERROR("remove failed for %s: %s", full_path, strerror(errno));
        return -1;
    }
    LOG_DEBUG("file deleted: %s", full_path);
    return 0;
}

static char *local_read_text(StorageBackend *backend, const char *filename) {
    LocalStorage *storage = (LocalStorage *)backend->internal;
    char full_path[MAX_PATH_LEN * 2];
    snprintf(full_path, sizeof(full_path), "%s/%s", storage->base_path, filename);

    FILE *f = fopen(full_path, "r");
    if (!f) {
        LOG_ERROR("cannot open %s: %s", full_path, strerror(errno));
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size <= 0) {
        fclose(f);
        return NULL;
    }

    char *buf = malloc((size_t)size + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }

    size_t read = fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[read] = '\0';
    return buf;
}

static char *local_get_public_url(StorageBackend *backend, const char *filename) {
    (void)backend;
    (void)filename;
    /* Local storage doesn't have public URLs */
    return NULL;
}

/* ── filename generation (backend-agnostic) ───────────────────────────────── */

void storage_gen_filename(const char *ext, char *out, size_t out_len) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    snprintf(out, out_len, "upload_%04d-%02d-%02d_%02d_%02d_%02d%s", t->tm_year + 1900,
             t->tm_mon + 1, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec, ext ? ext : "");
}

void storage_ocr_filename(const char *saved_name, char *out, size_t out_len) {
    const char *dot = strrchr(saved_name, '.');
    size_t base_len = dot ? (size_t)(dot - saved_name) : strlen(saved_name);
    snprintf(out, out_len, "%.*s_ocr_result.txt", (int)base_len, saved_name);
}

const char *storage_mime_type(const char *filename) {
    const char *ext = strrchr(filename, '.');
    if (!ext)
        return "application/octet-stream";

    if (strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".jpeg") == 0)
        return "image/jpeg";
    if (strcasecmp(ext, ".png") == 0)
        return "image/png";
    if (strcasecmp(ext, ".gif") == 0)
        return "image/gif";
    if (strcasecmp(ext, ".webp") == 0)
        return "image/webp";
    if (strcasecmp(ext, ".bmp") == 0)
        return "image/bmp";
    if (strcasecmp(ext, ".tiff") == 0 || strcasecmp(ext, ".tif") == 0)
        return "image/tiff";
    if (strcasecmp(ext, ".pdf") == 0)
        return "application/pdf";

    return "application/octet-stream";
}

/* ── backend ops ──────────────────────────────────────────────────────────── */

static const StorageBackendOps local_ops = {.open = local_open,
                                            .close = local_close,
                                            .ensure_dirs = local_ensure_dirs,
                                            .save_file = local_save_file,
                                            .save_text = local_save_text,
                                            .file_exists = local_file_exists,
                                            .delete_file = local_delete_file,
                                            .get_public_url = local_get_public_url,
                                            .read_text = local_read_text};

StorageBackend *storage_backend_local_open(const Config *cfg) {
    return local_ops.open(cfg);
}

/* ── Backward compatibility wrappers for tests ───────────────────────────── */

int storage_ensure_dirs(const char *base_path) {
    struct stat st;
    if (stat(base_path, &st) == 0)
        return 0;

    char *path = strdup(base_path);
    if (!path)
        return -1;

    char *p = path;
    if (*p == '/')
        p++;

    while (*p) {
        while (*p && *p != '/')
            p++;
        if (*p == '/') {
            *p = '\0';
            if (mkdir(path, 0755) != 0 && errno != EEXIST) {
                free(path);
                return -1;
            }
            *p = '/';
            p++;
        }
    }
    if (mkdir(path, 0755) != 0 && errno != EEXIST) {
        free(path);
        return -1;
    }
    free(path);
    return 0;
}

int storage_save_file(const char *base_path, const char *filename, const uint8_t *data,
                      size_t len) {
    char full_path[MAX_PATH_LEN * 2];
    snprintf(full_path, sizeof(full_path), "%s/%s", base_path, filename);

    FILE *f = fopen(full_path, "wb");
    if (!f)
        return -1;
    size_t written = fwrite(data, 1, len, f);
    fclose(f);
    return (written == len) ? 0 : -1;
}

int storage_save_text(const char *base_path, const char *filename, const char *text) {
    return storage_save_file(base_path, filename, (const uint8_t *)text, strlen(text));
}
