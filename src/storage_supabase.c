#include "storage_backend.h"
#include "config.h"
#include "../third_party/cjson/cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>

typedef struct {
    char base_url[MAX_URL_LEN];
    char anon_key[MAX_TOKEN_LEN];
    char bucket[MAX_PATH_LEN];
} SupabaseStorage;

/* ── grow buffer for curl ────────────────────────────────────────────────── */

typedef struct {
    char  *data;
    size_t len;
    size_t cap;
} GrowBuf;

static size_t write_cb(void *ptr, size_t size, size_t nmemb, void *userdata)
{
    GrowBuf *buf = (GrowBuf *)userdata;
    size_t incoming = size * nmemb;
    size_t needed   = buf->len + incoming + 1;

    if (needed > buf->cap) {
        size_t new_cap = buf->cap ? buf->cap * 2 : 4096;
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

static void growbuf_free(GrowBuf *buf)
{
    free(buf->data);
    buf->data = NULL;
    buf->len  = 0;
    buf->cap  = 0;
}

/* ── base64 encoder ───────────────────────────────────────────────────────── */

static const char B64_CHARS[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static char *base64_encode(const uint8_t *src, size_t len)
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

/* ── Supabase storage implementation ──────────────────────────────────────── */

static StorageBackend *supabase_open(const Config *cfg)
{
    LOG_INFO("opening Supabase storage: %s/%s", cfg->supabase_url, cfg->supabase_bucket);
    
    SupabaseStorage *storage = calloc(1, sizeof(SupabaseStorage));
    if (!storage) {
        LOG_ERROR("failed to allocate storage");
        return NULL;
    }
    snprintf(storage->base_url, sizeof(storage->base_url), "%s/storage/v1", cfg->supabase_url);
    snprintf(storage->anon_key, sizeof(storage->anon_key), "%s", cfg->supabase_anon_key);
    snprintf(storage->bucket, sizeof(storage->bucket), "%s", cfg->supabase_bucket);

    StorageBackend *backend = calloc(1, sizeof(StorageBackend));
    if (!backend) {
        free(storage);
        return NULL;
    }
    backend->internal = storage;
    return backend;
}

static void supabase_close(StorageBackend *backend)
{
    if (!backend) return;
    SupabaseStorage *storage = (SupabaseStorage *)backend->internal;
    free(storage);
    free(backend);
}

static int supabase_ensure_dirs(StorageBackend *backend)
{
    (void)backend;
    /* Supabase buckets are managed externally */
    LOG_DEBUG("Supabase storage doesn't require directory creation");
    return 0;
}

static int supabase_save_file(StorageBackend *backend, const char *filename, const uint8_t *data, size_t len)
{
    SupabaseStorage *storage = (SupabaseStorage *)backend->internal;
    LOG_INFO("uploading file to Supabase: %s/%s (%zu bytes)", storage->bucket, filename, len);

    char url[MAX_URL_LEN];
    snprintf(url, sizeof(url), "%s/object/%s/%s", storage->base_url, storage->bucket, filename);

    CURL *curl = curl_easy_init();
    if (!curl) {
        LOG_ERROR("curl_easy_init failed");
        return -1;
    }

    /* Build headers */
    struct curl_slist *headers = NULL;
    char auth_header[MAX_TOKEN_LEN + 64];
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", storage->anon_key);
    headers = curl_slist_append(headers, auth_header);
    headers = curl_slist_append(headers, "Content-Type: application/octet-stream");
    headers = curl_slist_append(headers, "x-upsert: true");

    /* Use multipart upload for large files, simple PUT for small */
    GrowBuf resp = {0};

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (curl_off_t)len);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        LOG_ERROR("upload failed: %s", curl_easy_strerror(res));
        growbuf_free(&resp);
        return -1;
    }

    if (http_code != 200 && http_code != 201) {
        LOG_ERROR("upload HTTP error: %ld - %.400s", http_code, resp.data ? resp.data : "(empty)");
        growbuf_free(&resp);
        return -1;
    }

    growbuf_free(&resp);
    LOG_INFO("file uploaded successfully to Supabase: %s", filename);
    return 0;
}

static int supabase_save_text(StorageBackend *backend, const char *filename, const char *text)
{
    return supabase_save_file(backend, filename, (const uint8_t *)text, strlen(text));
}

static int supabase_file_exists(StorageBackend *backend, const char *filename)
{
    SupabaseStorage *storage = (SupabaseStorage *)backend->internal;
    LOG_DEBUG("checking file existence in Supabase: %s", filename);

    char url[MAX_URL_LEN];
    snprintf(url, sizeof(url), "%s/object/%s/%s", storage->base_url, storage->bucket, filename);

    CURL *curl = curl_easy_init();
    if (!curl) return -1;

    struct curl_slist *headers = NULL;
    char auth_header[MAX_TOKEN_LEN + 64];
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", storage->anon_key);
    headers = curl_slist_append(headers, auth_header);

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        LOG_ERROR("existence check failed: %s", curl_easy_strerror(res));
        return -1;
    }

    return (http_code == 200) ? 1 : 0;
}

static char *supabase_get_public_url(StorageBackend *backend, const char *filename)
{
    SupabaseStorage *storage = (SupabaseStorage *)backend->internal;
    
    /* Try to get public URL first */
    char url[MAX_URL_LEN];
    snprintf(url, sizeof(url), "%s/object/public/%s/%s", 
             storage->base_url, storage->bucket, filename);
    
    /* Check if bucket is public by making a HEAD request */
    CURL *curl = curl_easy_init();
    if (!curl) return NULL;

    struct curl_slist *headers = NULL;
    char auth_header[MAX_TOKEN_LEN + 64];
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", storage->anon_key);
    headers = curl_slist_append(headers, auth_header);

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res == CURLE_OK && http_code == 200) {
        /* Public bucket - return public URL */
        char *public_url = malloc(MAX_URL_LEN);
        if (public_url) {
            snprintf(public_url, MAX_URL_LEN, "%s/storage/v1/object/public/%s/%s",
                     storage->base_url, storage->bucket, filename);
        }
        return public_url;
    }

    /* Private bucket - return signed URL */
    LOG_DEBUG("bucket is private, generating signed URL for: %s", filename);
    
    /* Create signed URL via Supabase API */
    char sign_url[MAX_URL_LEN];
    snprintf(sign_url, sizeof(sign_url), "%s/object/sign/%s/%s",
             storage->base_url, storage->bucket, filename);

    curl = curl_easy_init();
    if (!curl) return NULL;

    /* Request body with expiresIn */
    cJSON *body = cJSON_CreateObject();
    cJSON_AddNumberToObject(body, "expiresIn", 3600); /* 1 hour */
    char *body_str = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);

    GrowBuf resp = {0};
    headers = NULL;
    char auth_header[MAX_TOKEN_LEN + 64];
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", storage->anon_key);
    headers = curl_slist_append(headers, auth_header);
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, sign_url);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_str);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

    res = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    free(body_str);

    if (res != CURLE_OK || http_code != 200) {
        LOG_ERROR("failed to generate signed URL");
        growbuf_free(&resp);
        return NULL;
    }

    /* Parse response: {"signedURL":"/object/sign/..."} */
    cJSON *json = cJSON_Parse(resp.data);
    growbuf_free(&resp);
    
    if (!json) {
        LOG_ERROR("failed to parse signed URL response");
        return NULL;
    }

    cJSON *signed_url = cJSON_GetObjectItem(json, "signedURL");
    char *result = NULL;
    if (cJSON_IsString(signed_url)) {
        /* Build full URL */
        result = malloc(MAX_URL_LEN);
        if (result) {
            snprintf(result, MAX_URL_LEN, "%s%s", storage->base_url, signed_url->valuestring);
        }
    }

    cJSON_Delete(json);
    return result;
}

/* ── backend ops ──────────────────────────────────────────────────────────── */

static const StorageBackendOps supabase_ops = {
    .open           = supabase_open,
    .close          = supabase_close,
    .ensure_dirs    = supabase_ensure_dirs,
    .save_file      = supabase_save_file,
    .save_text      = supabase_save_text,
    .file_exists    = supabase_file_exists,
    .get_public_url = supabase_get_public_url
};

StorageBackend *storage_backend_supabase_open(const Config *cfg)
{
    return supabase_ops.open(cfg);
}
