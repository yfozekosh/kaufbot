#include "utils.h"
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

/* ── Grow buffer ─────────────────────────────────────────────────────────── */

#define GROWBUF_INITIAL_CAP 8192

size_t growbuf_write_cb(void *ptr, size_t size, size_t nmemb, void *userdata)
{
    GrowBuf *buf = (GrowBuf *)userdata;
    size_t incoming = size * nmemb;
    size_t needed   = buf->len + incoming + 1;

    if (needed > buf->cap) {
        size_t new_cap = buf->cap ? buf->cap * 2 : GROWBUF_INITIAL_CAP;
        while (new_cap < needed) new_cap *= 2;
        char *tmp = realloc(buf->data, new_cap);
        if (!tmp) return 0;
        buf->data = tmp;
        buf->cap  = new_cap;
    }
    memcpy(buf->data + buf->len, ptr, incoming);
    buf->len += incoming;
    buf->data[buf->len] = '\0';
    return incoming;
}

void growbuf_free(GrowBuf *buf)
{
    if (buf) {
        free(buf->data);
        buf->data = NULL;
        buf->len  = 0;
        buf->cap  = 0;
    }
}

/* ── Base64 encoder ──────────────────────────────────────────────────────── */

static const char B64_CHARS[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

char *base64_encode(const uint8_t *src, size_t len)
{
    size_t out_len = ((len + 2) / 3) * 4 + 1;
    char *out = malloc(out_len);
    if (!out) return NULL;

    size_t i, j;
    for (i = 0, j = 0; i < len;) {
        uint32_t a = i < len ? src[i++] : 0;
        uint32_t b = i < len ? src[i++] : 0;
        uint32_t c = i < len ? src[i++] : 0;
        uint32_t triple = (a << 16) | (b << 8) | c;
        out[j++] = B64_CHARS[(triple >> 18) & 0x3F];
        out[j++] = B64_CHARS[(triple >> 12) & 0x3F];
        out[j++] = B64_CHARS[(triple >>  6) & 0x3F];
        out[j++] = B64_CHARS[ triple        & 0x3F];
    }
    if (len % 3 == 1) { out[j-1] = '='; out[j-2] = '='; }
    if (len % 3 == 2) { out[j-1] = '='; }
    out[j] = '\0';
    return out;
}

/* ── URL percent-encoding ────────────────────────────────────────────────── */

/* RFC 3986 unreserved characters */
static int is_unreserved(char c)
{
    return (c >= 'A' && c <= 'Z') ||
           (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') ||
           c == '-' || c == '_' || c == '.' || c == '~';
}

char *url_percent_encode(const char *src)
{
    if (!src) return NULL;

    size_t len = strlen(src);
    /* Worst case: every char becomes %XX (3 bytes) */
    char *out = malloc(len * 3 + 1);
    if (!out) return NULL;

    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)src[i];
        if (is_unreserved(c)) {
            out[j++] = c;
        } else {
            out[j++] = '%';
            out[j++] = "0123456789ABCDEF"[c >> 4];
            out[j++] = "0123456789ABCDEF"[c & 0x0F];
        }
    }
    out[j] = '\0';
    return out;
}

/* ── Safe shell-free path removal ────────────────────────────────────────── */

int safe_remove_path(const char *path)
{
    if (!path || path[0] == '\0') return -1;

    /* Use remove() for files, recursive rmdir for directories */
    struct stat st;
    if (stat(path, &st) != 0) return -1;

    if (S_ISDIR(st.st_mode)) {
        /* Use nftw or manual recursion - keep it simple for now */
        return remove(path); /* Only works for empty dirs */
    }
    return remove(path);
}
