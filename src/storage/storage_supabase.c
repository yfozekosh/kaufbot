#include "cJSON.h"
#include "config.h"
#include "http_client.h"
#include "storage_backend.h"
#include "utils.h"

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

static HttpHeaders *build_auth_headers(const SupabaseStorage *storage) {
    HttpHeaders *headers = http_headers_new(3);
    if (!headers)
        return NULL;

    char hdr[MAX_TOKEN_LEN + 64];

    if (storage->is_v2_key) {
        snprintf(hdr, sizeof(hdr), "%s", storage->anon_key);
        http_headers_add(headers, "apikey", hdr);
    } else {
        snprintf(hdr, sizeof(hdr), "Bearer %s", storage->anon_key);
        http_headers_add(headers, "Authorization", hdr);
        snprintf(hdr, sizeof(hdr), "%s", storage->anon_key);
        http_headers_add(headers, "apikey", hdr);
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

    HttpClient *http = http_client_new();
    if (!http) {
        LOG_ERROR("failed to create HTTP client");
        return -1;
    }

    HttpHeaders *auth_headers = build_auth_headers(storage);
    if (!auth_headers) {
        http_client_free(http);
        return -1;
    }

    /* Add Content-Type and x-upsert headers */
    http_headers_add(auth_headers, "Content-Type", "application/octet-stream");
    http_headers_add(auth_headers, "x-upsert", "true");

    HttpResponse resp;
    int rc = http_client_put(http, url, data, len, &resp);

    if (rc != 0 || !resp.success) {
        LOG_ERROR("upload failed: %s (HTTP %ld)", resp.error, (long)resp.status_code);
        http_response_free(&resp);
        http_headers_free(auth_headers);
        http_client_free(http);
        return -1;
    }

    http_response_free(&resp);
    http_headers_free(auth_headers);
    http_client_free(http);

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

    HttpClient *http = http_client_new();
    if (!http)
        return -1;

    HttpHeaders *auth_headers = build_auth_headers(storage);
    if (!auth_headers) {
        http_client_free(http);
        return -1;
    }

    HttpResponse resp;
    int rc = http_client_head(http, url, SUPABASE_HTTP_HEAD_TIMEOUT_SECS, &resp);

    if (rc != 0) {
        LOG_ERROR("existence check failed: %s", resp.error);
        http_response_free(&resp);
        http_headers_free(auth_headers);
        http_client_free(http);
        return -1;
    }

    int exists = resp.success ? 1 : 0;
    http_response_free(&resp);
    http_headers_free(auth_headers);
    http_client_free(http);

    return exists;
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

    HttpClient *http = http_client_new();
    if (!http)
        return NULL;

    cJSON *body = cJSON_CreateObject();
    cJSON_AddNumberToObject(body, "expiresIn", SIGNED_URL_EXPIRY_SECS);
    char *body_str = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    if (!body_str) {
        http_client_free(http);
        return NULL;
    }

    HttpHeaders *auth_headers = build_auth_headers(storage);
    if (!auth_headers) {
        free(body_str);
        http_client_free(http);
        return NULL;
    }
    http_headers_add(auth_headers, "Content-Type", "application/json");

    HttpResponse resp;
    int rc = http_client_post_json(http, sign_url, body_str, &resp);
    free(body_str);

    if (rc != 0 || !resp.success) {
        LOG_ERROR("failed to generate signed URL (HTTP %ld)", (long)resp.status_code);
        http_response_free(&resp);
        http_headers_free(auth_headers);
        http_client_free(http);
        return NULL;
    }

    cJSON *json = cJSON_Parse(resp.body);
    http_response_free(&resp);
    http_headers_free(auth_headers);
    http_client_free(http);

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

    HttpClient *http = http_client_new();
    if (!http)
        return 0;

    HttpHeaders *auth_headers = build_auth_headers(storage);
    if (!auth_headers) {
        http_client_free(http);
        return 0;
    }

    HttpResponse resp;
    int rc = http_client_head(http, url, SUPABASE_HTTP_HEAD_TIMEOUT_SECS, &resp);

    int accessible = 0;
    if (rc == 0 && resp.success) {
        accessible = 1;
    }

    http_response_free(&resp);
    http_headers_free(auth_headers);
    http_client_free(http);

    return accessible;
}

static int supabase_delete_file(StorageBackend *backend, const char *filename) {
    SupabaseStorage *storage = (SupabaseStorage *)backend->internal;
    char url[MAX_COMPOSED_URL];
    snprintf(url, sizeof(url), "%s/storage/v1/object/%s/%s", storage->base_url, storage->bucket,
             filename);

    LOG_DEBUG("deleting file from Supabase: %s", filename);

    HttpClient *http = http_client_new();
    if (!http)
        return -1;

    HttpHeaders *auth_headers = build_auth_headers(storage);
    if (!auth_headers) {
        http_client_free(http);
        return -1;
    }

    HttpResponse resp;
    int rc = http_client_delete(http, url, &resp);

    if (rc != 0) {
        LOG_ERROR("supabase delete failed: %s", resp.error);
        http_response_free(&resp);
        http_headers_free(auth_headers);
        http_client_free(http);
        return -1;
    }

    if (resp.status_code == HTTP_STATUS_NOT_FOUND) {
        LOG_DEBUG("file not found in Supabase: %s", filename);
        http_response_free(&resp);
        http_headers_free(auth_headers);
        http_client_free(http);
        return 1;
    }

    if (!resp.success) {
        LOG_ERROR("supabase delete returned HTTP %ld", (long)resp.status_code);
        http_response_free(&resp);
        http_headers_free(auth_headers);
        http_client_free(http);
        return -1;
    }

    LOG_DEBUG("file deleted from Supabase: %s", filename);
    http_response_free(&resp);
    http_headers_free(auth_headers);
    http_client_free(http);
    return 0;
}

static char *supabase_get_public_url(StorageBackend *backend, const char *filename) {
    SupabaseStorage *storage = (SupabaseStorage *)backend->internal;

    if (supabase_check_public_access(storage, filename))
        return supabase_public_url(storage, filename);

    return supabase_signed_url(storage, filename);
}

static char *supabase_read_text(StorageBackend *backend, const char *filename) {
    SupabaseStorage *storage = (SupabaseStorage *)backend->internal;
    LOG_DEBUG("reading text from Supabase: %s/%s", storage->bucket, filename);

    char url[MAX_COMPOSED_URL];
    snprintf(url, sizeof(url), "%s/storage/v1/object/%s/%s", storage->base_url, storage->bucket,
             filename);

    HttpClient *http = http_client_new();
    if (!http)
        return NULL;

    HttpHeaders *auth_headers = build_auth_headers(storage);
    if (!auth_headers) {
        http_client_free(http);
        return NULL;
    }

    HttpResponse resp;
    int rc = http_client_get(http, url, SUPABASE_HTTP_TIMEOUT_SECS, &resp);

    if (rc != 0 || !resp.success) {
        LOG_ERROR("supabase read failed: %s (HTTP %ld)", resp.error, (long)resp.status_code);
        http_response_free(&resp);
        http_headers_free(auth_headers);
        http_client_free(http);
        return NULL;
    }

    http_headers_free(auth_headers);
    http_client_free(http);

    /* Transfer ownership of body to caller */
    char *text = resp.body;
    resp.body = NULL;
    http_response_free(&resp);

    return text;
}

static uint8_t *supabase_read_binary(StorageBackend *backend, const char *filename,
                                     size_t *out_len) {
    SupabaseStorage *storage = (SupabaseStorage *)backend->internal;
    LOG_DEBUG("reading binary from Supabase: %s/%s", storage->bucket, filename);

    char url[MAX_COMPOSED_URL];
    snprintf(url, sizeof(url), "%s/storage/v1/object/%s/%s", storage->base_url, storage->bucket,
             filename);

    HttpClient *http = http_client_new();
    if (!http) {
        *out_len = 0;
        return NULL;
    }

    HttpHeaders *auth_headers = build_auth_headers(storage);
    if (!auth_headers) {
        http_client_free(http);
        *out_len = 0;
        return NULL;
    }

    HttpResponse resp;
    int rc = http_client_get(http, url, SUPABASE_HTTP_TIMEOUT_SECS, &resp);

    if (rc != 0 || !resp.success) {
        LOG_ERROR("supabase read failed: %s (HTTP %ld)", resp.error, (long)resp.status_code);
        http_response_free(&resp);
        http_headers_free(auth_headers);
        http_client_free(http);
        *out_len = 0;
        return NULL;
    }

    http_headers_free(auth_headers);
    http_client_free(http);

    *out_len = resp.body_len;
    uint8_t *data = (uint8_t *)resp.body;
    resp.body = NULL;
    http_response_free(&resp);

    return data;
}

/* ── backend ops ──────────────────────────────────────────────────────────── */

static const StorageBackendOps supabase_ops = {.open = supabase_open,
                                               .close = supabase_close,
                                               .ensure_dirs = supabase_ensure_dirs,
                                               .save_file = supabase_save_file,
                                               .save_text = supabase_save_text,
                                               .read_text = supabase_read_text,
                                               .read_binary = supabase_read_binary,
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
