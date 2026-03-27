#ifndef UTILS_H
#define UTILS_H

#include <stddef.h>
#include <stdint.h>

/* ── Grow buffer for dynamic string building (curl callbacks, etc.) ──────── */

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} GrowBuf;

/* curl write callback that appends to a GrowBuf.
 * Returns number of bytes consumed, or 0 on allocation error. */
size_t growbuf_write_cb(void *ptr, size_t size, size_t nmemb, void *userdata);

/* Free the buffer's internal allocation and reset fields. */
void growbuf_free(GrowBuf *buf);

/* ── Base64 encoder ──────────────────────────────────────────────────────── */

/* Encode raw bytes into a heap-allocated base64 string.
 * Returns NULL on allocation failure. Caller must free(). */
char *base64_encode(const uint8_t *src, size_t len);

/* ── URL percent-encoding ────────────────────────────────────────────────── */

/* Percent-encode a string for use in a URL query parameter.
 * Returns heap-allocated string or NULL on error. Caller must free(). */
char *url_percent_encode(const char *src);

/* ── Safe shell cleanup ──────────────────────────────────────────────────── */

/* Remove a file or directory tree safely (no shell injection).
 * Returns 0 on success, -1 on error. */
int safe_remove_path(const char *path);

#endif /* UTILS_H */
