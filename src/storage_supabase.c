#include "cJSON.h"
#include "config.h"
#include "storage_backend.h"
#include "utils.h"

#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_COMPOSED_URL                (MAX_URL_LEN + MAX_PATH_LEN + MAX_PATH_LEN + 64)
#define SUPABASE_HTTP_TIMEOUT_SECS      120L
#define SUPABASE_HTTP_HEAD_TIMEOUT_SECS 30L
#define SUPABASE_HTTP_SIGN_TIMEOUT_SECS 30L
#define SIGNED_URL_EXPIRY_SECS          3600

typedef struct {
    char base_url[MAX_URL_LEN];
    char anon_key[MAX_TOKEN_LEN];
    char bucket[MAX_PATH_LEN];
    int is_v2_key;
} SupabaseStorage;

static struct curl_slist *build_auth_headers(const SupabaseStorage *storage) {
    struct curl_slist *headers = NULL;
    char hdr[MAX_TOKEN_LEN + 64];

    if (storage->is_v2_key) {
        snprintf(hdr, sizeof(hdr), "apikey: %s", storage->anon_key);
        headers = curl_slist_append(headers, hdr);
    } else {
        snprintf(hdr, sizeof(hdr), "Authorization: Bearer %s", storage->anon_key);
        headers = curl_slist_append(headers, hdr);
        snprintf(hdr, sizeof(hdr), "apikey: %s", storage->anon_key);
        headers = curl_slist_append(headers, hdr);
    }
    return headers;
}

/* ── Supabase storage implementation ──────────────────────────────────────── */

static StorageBackend *supabase_open(const Config *cfg) {
    LOG_INFO("opening Supabase storage: %s/%s", cfg->supabase_url, cfg->supabase_bucket);

    SupabaseStorage *storage = calloc(1, sizeof(SupabaseStorage));
    if (!storage) {
        LOG_ERROR("failed to allocate storage");
        return NULL;
    }
    snprintf(storage->base_url, sizeof(storage->base_url), "%s", cfg->supabase_url);
    snprintf(storage->anon_key, sizeof(storage->anon_key), "%s", cfg->supabase_service_key);
    snprintf(storage->bucket, sizeof(storage->bucket), "%s", cfg->supabase_bucket);
    storage->is_v2_key = (strncmp(cfg->supabase_service_key, "sb_", 3) == 0);

    StorageBackend *backend = calloc(1, sizeof(StorageBackend));
    if (!backend) {
        free(storage);
        return NULL;
    }
    backend->ops = NULL;
    backend->internal = storage;
    return backend;
}

static void supabase_close(StorageBackend *backend) {
    if (!backend)
        return;
    SupabaseStorage *storage = (SupabaseStorage *)backend->internal;
    free(storage);
    free(backend);
}

static int supabase_ensure_dirs(StorageBackend *backend) {
    (void)backend;
    LOG_DEBUG("Supabase storage doesn't require directory creation");
    return 0;
}

static int supabase_save_file(StorageBackend *backend, const char *filename, const uint8_t *data,
                              size_t len) {
    SupabaseStorage *storage = (SupabaseStorage *)backend->internal;
    LOG_INFO("uploading file to Supabase: %s/%s (%zu bytes)", storage->bucket, filename, len);

    char url[MAX_COMPOSED_URL];
    snprintf(url, sizeof(url), "%s/storage/v1/object/%s/%s", storage->base_url, storage->bucket,
             filename);

    CURL *curl = curl_easy_init();
    if (!curl) {
        LOG_ERROR("curl_easy_init failed");
        return -1;
    }

    struct curl_slist *headers = build_auth_headers(storage);
    headers = curl_slist_append(headers, "Content-Type: application/octet-stream");
    headers = curl_slist_append(headers, "x-upsert: true");

    GrowBuf resp = {0};

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (curl_off_t)len);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, growbuf_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, SUPABASE_HTTP_TIMEOUT_SECS);

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
        LOG_ERROR("upload HTTP error: %ld", http_code);
        growbuf_free(&resp);
        return -1;
    }

    growbuf_free(&resp);
    LOG_INFO("file uploaded successfully to Supabase: %s", filename);
    return 0;
}

static int supabase_save_text(StorageBackend *backend, const char *filename, const char *text) {
    return supabase_save_file(backend, filename, (const uint8_t *)text, strlen(text));
}

static int supabase_file_exists(StorageBackend *backend, const char *filename) {
    SupabaseStorage *storage = (SupabaseStorage *)backend->internal;
    LOG_DEBUG("checking file existence in Supabase: %s", filename);

    char url[MAX_COMPOSED_URL];
    snprintf(url, sizeof(url), "%s/storage/v1/object/%s/%s", storage->base_url, storage->bucket,
             filename);

    CURL *curl = curl_easy_init();
    if (!curl)
        return -1;

    struct curl_slist *headers = build_auth_headers(storage);

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, SUPABASE_HTTP_HEAD_TIMEOUT_SECS);

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

static char *supabase_public_url(const SupabaseStorage *storage, const char *filename) {
    char *url = malloc(MAX_COMPOSED_URL);
    if (url) {
        snprintf(url, MAX_COMPOSED_URL, "%s/storage/v1/object/public/%s/%s", storage->base_url,
                 storage->bucket, filename);
    }
    return url;
}

static char *supabase_signed_url(const SupabaseStorage *storage, const char *filename) {
    LOG_DEBUG("bucket is private, generating signed URL for: %s", filename);

    char sign_url[MAX_COMPOSED_URL];
    snprintf(sign_url, sizeof(sign_url), "%s/storage/v1/object/sign/%s/%s", storage->base_url,
             storage->bucket, filename);

    CURL *curl = curl_easy_init();
    if (!curl)
        return NULL;

    cJSON *body = cJSON_CreateObject();
    cJSON_AddNumberToObject(body, "expiresIn", SIGNED_URL_EXPIRY_SECS);
    char *body_str = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    if (!body_str) {
        curl_easy_cleanup(curl);
        return NULL;
    }

    GrowBuf resp = {0};
    struct curl_slist *headers = build_auth_headers(storage);
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, sign_url);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_str);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, growbuf_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, SUPABASE_HTTP_SIGN_TIMEOUT_SECS);

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    free(body_str);

    if (res != CURLE_OK || http_code != 200) {
        LOG_ERROR("failed to generate signed URL");
        growbuf_free(&resp);
        return NULL;
    }

    cJSON *json = cJSON_Parse(resp.data);
    growbuf_free(&resp);

    if (!json) {
        LOG_ERROR("failed to parse signed URL response");
        return NULL;
    }

    cJSON *signed_url = cJSON_GetObjectItem(json, "signedURL");
    char *result = NULL;
    if (cJSON_IsString(signed_url)) {
        result = malloc(MAX_COMPOSED_URL);
        if (result) {
            snprintf(result, MAX_COMPOSED_URL, "%s/storage/v1%s", storage->base_url,
                     signed_url->valuestring);
        }
    }

    cJSON_Delete(json);
    return result;
}

static int supabase_check_public_access(const SupabaseStorage *storage, const char *filename) {
    char url[MAX_COMPOSED_URL];
    snprintf(url, sizeof(url), "%s/storage/v1/object/public/%s/%s", storage->base_url,
             storage->bucket, filename);

    CURL *curl = curl_easy_init();
    if (!curl)
        return 0;

    struct curl_slist *headers = build_auth_headers(storage);

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, SUPABASE_HTTP_HEAD_TIMEOUT_SECS);

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    return (res == CURLE_OK && http_code == 200);
}

static int supabase_delete_file(StorageBackend *backend, const char *filename) {
    SupabaseStorage *storage = (SupabaseStorage *)backend->internal;
    char url[MAX_COMPOSED_URL];
    snprintf(url, sizeof(url), "%s/storage/v1/object/%s/%s", storage->base_url, storage->bucket,
             filename);

    LOG_DEBUG("deleting file from Supabase: %s", filename);

    CURL *curl = curl_easy_init();
    if (!curl)
        return -1;

    struct curl_slist *headers = build_auth_headers(storage);

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, SUPABASE_HTTP_TIMEOUT_SECS);

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        LOG_ERROR("supabase delete failed: %s", curl_easy_strerror(res));
        return -1;
    }

    if (http_code == 404) {
        LOG_DEBUG("file not found in Supabase: %s", filename);
        return 1;
    }

    if (http_code != 200 && http_code != 204) {
        LOG_ERROR("supabase delete returned HTTP %ld", http_code);
        return -1;
    }

    LOG_DEBUG("file deleted from Supabase: %s", filename);
    return 0;
}

static char *supabase_get_public_url(StorageBackend *backend, const char *filename) {
    SupabaseStorage *storage = (SupabaseStorage *)backend->internal;

    if (supabase_check_public_access(storage, filename))
        return supabase_public_url(storage, filename);

    return supabase_signed_url(storage, filename);
}

/* ── backend ops ──────────────────────────────────────────────────────────── */

static const StorageBackendOps supabase_ops = {.open = supabase_open,
                                               .close = supabase_close,
                                               .ensure_dirs = supabase_ensure_dirs,
                                               .save_file = supabase_save_file,
                                               .save_text = supabase_save_text,
                                               .file_exists = supabase_file_exists,
                                               .delete_file = supabase_delete_file,
                                               .get_public_url = supabase_get_public_url};

StorageBackend *storage_backend_supabase_open(const Config *cfg) {
    StorageBackend *backend = supabase_ops.open(cfg);
    if (backend) {
        backend->ops = &supabase_ops;
    }
    return backend;
}
