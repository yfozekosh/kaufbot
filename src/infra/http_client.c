#include "http_client.h"
#include "config.h"
#include "utils.h"

#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ── Internal Structures ────────────────────────────────────────────────── */

struct HttpClient {
    HttpClientConfig config;
    CURL *curl;
    bool owns_curl;
};

/* ── Helper Functions ───────────────────────────────────────────────────── */

static const char *http_method_to_string(HttpMethod method) {
    switch (method) {
    case HTTP_METHOD_GET:
        return "GET";
    case HTTP_METHOD_POST:
        return "POST";
    case HTTP_METHOD_PUT:
        return "PUT";
    case HTTP_METHOD_DELETE:
        return "DELETE";
    case HTTP_METHOD_HEAD:
        return "HEAD";
    case HTTP_METHOD_PATCH:
        return "PATCH";
    default:
        return "UNKNOWN";
    }
}

static void log_request(const HttpRequest *req) {
    LOG_INFO("HTTP %s %s", http_method_to_string(req->method), req->url);

    if (req->body && req->method == HTTP_METHOD_POST) {
        LOG_DEBUG("Request body: %.500s", req->body);
    }
}

static void log_response(const HttpResponse *resp) {
    if (resp->success) {
        LOG_INFO("HTTP %ld - %ldms (body: %zu bytes)", (long)resp->status_code, resp->total_time_ms,
                 resp->body_len);
    } else {
        LOG_ERROR("HTTP request failed: %s (status: %ld) response: %.500s", resp->error,
                  (long)resp->status_code, resp->body ? resp->body : "(empty)");
    }
}

static bool should_retry(const HttpResponse *resp, int attempt, int max_retries) {
    if (max_retries <= 0)
        return false;
    if (attempt >= max_retries)
        return false;
    return http_is_retryable(resp->status_code);
}

static void exponential_backoff(int attempt, double base_delay_ms) {
    double delay_ms = base_delay_ms * (1 << attempt); /* 2^attempt */
    if (delay_ms > 10000)
        delay_ms = 10000; /* Cap at 10s */

    LOG_DEBUG("Retry attempt %d, waiting %.0fms", attempt + 1, delay_ms);
    usleep((useconds_t)(delay_ms * 1000));
}

/* ── Header Management ──────────────────────────────────────────────────── */

HttpHeaders *http_headers_new(size_t capacity) {
    HttpHeaders *headers = calloc(1, sizeof(HttpHeaders));
    if (!headers)
        return NULL;

    headers->headers = calloc(capacity, sizeof(HttpHeader));
    if (!headers->headers) {
        free(headers);
        return NULL;
    }
    headers->count = 0;
    headers->capacity = capacity;
    return headers;
}

void http_headers_free(HttpHeaders *headers) {
    if (!headers)
        return;
    for (size_t i = 0; i < headers->count; i++) {
        free((void *)headers->headers[i].key);
        free((void *)headers->headers[i].value);
    }
    free(headers->headers);
    free(headers);
}

int http_headers_add(HttpHeaders *headers, const char *key, const char *value) {
    if (!headers || !key || !value)
        return -1;
    if (headers->count >= headers->capacity)
        return -1;

    headers->headers[headers->count].key = strdup(key);
    headers->headers[headers->count].value = strdup(value);
    if (!headers->headers[headers->count].key || !headers->headers[headers->count].value) {
        free((void *)headers->headers[headers->count].key);
        free((void *)headers->headers[headers->count].value);
        return -1;
    }
    headers->count++;
    return 0;
}

/* ── Lifecycle ──────────────────────────────────────────────────────────── */

HttpClient *http_client_new(void) {
    HttpClientConfig default_config = {
        .default_timeout_secs = 60,
        .default_connect_timeout_secs = 10,
        .max_retries = 0,
        .retry_base_delay_ms = 100,
        .enable_logging = true,
        .follow_redirects = false,
        .user_data = NULL,
        .on_request = NULL,
        .on_response = NULL,
        .on_error = NULL,
    };
    return http_client_new_custom(&default_config);
}

HttpClient *http_client_new_custom(const HttpClientConfig *config) {
    static bool curl_initialized = false;

    if (!curl_initialized) {
        CURLcode rc = curl_global_init(CURL_GLOBAL_DEFAULT);
        if (rc != CURLE_OK) {
            LOG_ERROR("curl_global_init failed: %s", curl_easy_strerror(rc));
            return NULL;
        }
        curl_initialized = true;
    }

    HttpClient *client = calloc(1, sizeof(HttpClient));
    if (!client)
        return NULL;

    client->config = *config;
    client->curl = curl_easy_init();
    if (!client->curl) {
        free(client);
        return NULL;
    }
    client->owns_curl = true;

    return client;
}

void http_client_free(HttpClient *client) {
    if (!client)
        return;
    if (client->owns_curl && client->curl) {
        curl_easy_cleanup(client->curl);
    }
    free(client);
}

/* ── Request Execution ──────────────────────────────────────────────────── */

static struct curl_slist *build_curl_headers(HttpHeaders *headers) {
    if (!headers || headers->count == 0)
        return NULL;

    struct curl_slist *headers_list = NULL;
    for (size_t i = 0; i < headers->count; i++) {
        char hdr[512];
        snprintf(hdr, sizeof(hdr), "%s: %s", headers->headers[i].key, headers->headers[i].value);
        struct curl_slist *new_list = curl_slist_append(headers_list, hdr);
        if (!new_list) {
            curl_slist_free_all(headers_list);
            return NULL;
        }
        headers_list = new_list;
    }
    return headers_list;
}

static void configure_curl(HttpClient *client, CURL *curl, const HttpRequest *req) {
    curl_easy_reset(curl); /* Reset to clean state */

    curl_easy_setopt(curl, CURLOPT_URL, req->url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, growbuf_write_cb);

    /* Timeouts */
    long timeout = req->timeout_secs > 0 ? req->timeout_secs : client->config.default_timeout_secs;
    long connect_timeout = req->connect_timeout_secs > 0
                               ? req->connect_timeout_secs
                               : client->config.default_connect_timeout_secs;
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, connect_timeout);

    /* Redirects */
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, req->follow_redirects ? 1L : 0L);

    /* Reuse connections */
    curl_easy_setopt(curl, CURLOPT_FORBID_REUSE, 0L);

    /* Method-specific options */
    switch (req->method) {
    case HTTP_METHOD_POST:
    case HTTP_METHOD_PUT:
    case HTTP_METHOD_PATCH:
        if (req->body) {
            /* POSTFIELDS must be set before POSTFIELDSIZE to avoid libcurl
             * using strlen on binary data before the size is known */
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, req->body);
            if (req->body_len > 0) {
                curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)req->body_len);
            }
        }
        break;
    case HTTP_METHOD_DELETE:
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
        break;
    case HTTP_METHOD_HEAD:
        curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
        break;
    default:
        break;
    }

    /* Custom headers */
    if (req->headers) {
        struct curl_slist *headers_list = build_curl_headers(req->headers);
        if (headers_list) {
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers_list);
            /* Note: headers_list will be freed after curl_easy_perform */
        }
    }
}

int http_client_execute(HttpClient *client, const HttpRequest *request, HttpResponse *response) {
    if (!client || !request || !response)
        return -1;

    /* Initialize response */
    memset(response, 0, sizeof(HttpResponse));
    response->success = false;

    /* Invoke request hook */
    if (client->config.on_request) {
        client->config.on_request(request, client->config.user_data);
    }

    /* Logging */
    if (client->config.enable_logging) {
        log_request(request);
    }

    /* Retry loop */
    int attempt = 0;
    struct curl_slist *headers_list = NULL;

    do {
        if (attempt > 0) {
            exponential_backoff(attempt, client->config.retry_base_delay_ms);
        }

        /* Configure curl */
        GrowBuf resp_buf = {0};
        CURL *curl = client->curl;
        configure_curl(client, curl, request);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp_buf);

        /* Build headers for this attempt */
        if (request->headers) {
            headers_list = build_curl_headers(request->headers);
            if (headers_list) {
                curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers_list);
            }
        }

        /* Execute */
        CURLcode res = curl_easy_perform(curl);

        /* Get metadata */
        long http_code = 0;
        double total_time = 0;
        double connect_time = 0;
        double download_time = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &total_time);
        curl_easy_getinfo(curl, CURLINFO_CONNECT_TIME, &connect_time);
#ifdef CURLINFO_DOWNLOAD_TIME
        curl_easy_getinfo(curl, CURLINFO_DOWNLOAD_TIME, &download_time);
#else
        download_time = total_time - connect_time; /* Fallback estimation */
#endif

        /* Free headers */
        if (headers_list) {
            curl_slist_free_all(headers_list);
            headers_list = NULL;
        }

        /* Populate response */
        response->status_code = (HttpStatusCode)http_code;
        response->body = resp_buf.data;
        response->body_len = resp_buf.len;
        response->total_time_ms = (long)(total_time * 1000);
        response->connect_time_ms = (long)(connect_time * 1000);
        response->download_time_ms = (long)(download_time * 1000);
        response->success = (res == CURLE_OK && http_code >= 200 && http_code < 300);

        if (res != CURLE_OK) {
            snprintf(response->error, sizeof(response->error), "curl error: %s",
                     curl_easy_strerror(res));
        }

        /* Check for retry */
        if (should_retry(response, attempt, client->config.max_retries)) {
            LOG_WARN("Retryable error (HTTP %ld), attempt %d/%d", (long)response->status_code,
                     attempt + 1, client->config.max_retries);
            free(resp_buf.data);
            memset(response, 0, sizeof(HttpResponse));
            attempt++;
            continue;
        }

        break;
    } while (true);

    /* Logging */
    if (client->config.enable_logging) {
        log_response(response);
    }

    /* Invoke response hook */
    if (client->config.on_response) {
        client->config.on_response(response, client->config.user_data);
    }

    /* Invoke error hook on failure */
    if (!response->success && client->config.on_error) {
        client->config.on_error(request, response, client->config.user_data);
    }

    return response->success ? 0 : -1;
}

/* ── Convenience Methods ────────────────────────────────────────────────── */

int http_client_get(HttpClient *client, const char *url, long timeout_secs,
                    HttpResponse *response) {
    if (!client || !url || !response)
        return -1;

    HttpRequest req = {
        .method = HTTP_METHOD_GET,
        .url = url,
        .timeout_secs = timeout_secs,
        .follow_redirects = client->config.follow_redirects,
    };
    return http_client_execute(client, &req, response);
}

int http_client_post(HttpClient *client, const char *url, const char *body,
                     const char *content_type, HttpResponse *response) {
    HttpRequest req = {
        .method = HTTP_METHOD_POST,
        .url = url,
        .body = body,
        .body_len = body ? strlen(body) : 0,
    };

    /* Add Content-Type header if provided */
    HttpHeaders *headers = NULL;
    if (content_type) {
        headers = http_headers_new(1);
        if (headers) {
            http_headers_add(headers, "Content-Type", content_type);
            req.headers = headers;
        }
    }

    int result = http_client_execute(client, &req, response);
    http_headers_free(headers);
    return result;
}

int http_client_post_json(HttpClient *client, const char *url, const char *json_body,
                          HttpResponse *response) {
    return http_client_post(client, url, json_body, "application/json", response);
}

int http_client_put(HttpClient *client, const char *url, HttpHeaders *headers, const uint8_t *data,
                    size_t len, HttpResponse *response) {
    HttpRequest req = {
        .method = HTTP_METHOD_PUT,
        .url = url,
        .body = (const char *)data,
        .body_len = len,
        .headers = headers,
    };
    return http_client_execute(client, &req, response);
}

int http_client_upload(HttpClient *client, const char *url, HttpHeaders *headers,
                       const uint8_t *data, size_t len, HttpResponse *response) {
    HttpRequest req = {
        .method = HTTP_METHOD_POST,
        .url = url,
        .body = (const char *)data,
        .body_len = len,
        .headers = headers,
    };
    return http_client_execute(client, &req, response);
}

int http_client_delete(HttpClient *client, const char *url, HttpResponse *response) {
    HttpRequest req = {
        .method = HTTP_METHOD_DELETE,
        .url = url,
    };
    return http_client_execute(client, &req, response);
}

int http_client_head(HttpClient *client, const char *url, long timeout_secs,
                     HttpResponse *response) {
    HttpRequest req = {
        .method = HTTP_METHOD_HEAD,
        .url = url,
        .timeout_secs = timeout_secs,
    };
    return http_client_execute(client, &req, response);
}

/* ── Response Management ────────────────────────────────────────────────── */

void http_response_free(HttpResponse *response) {
    if (!response)
        return;
    free(response->body);
    response->body = NULL;
    response->body_len = 0;
    if (response->headers) {
        http_headers_free(response->headers);
        response->headers = NULL;
    }
}

bool http_response_is_success(const HttpResponse *response) {
    return response && response->success;
}

/* ── Utility Functions ──────────────────────────────────────────────────── */

const char *http_status_description(HttpStatusCode code) {
    switch (code) {
    case HTTP_STATUS_OK:
        return "OK";
    case HTTP_STATUS_CREATED:
        return "Created";
    case HTTP_STATUS_NO_CONTENT:
        return "No Content";
    case HTTP_STATUS_BAD_REQUEST:
        return "Bad Request";
    case HTTP_STATUS_UNAUTHORIZED:
        return "Unauthorized";
    case HTTP_STATUS_FORBIDDEN:
        return "Forbidden";
    case HTTP_STATUS_NOT_FOUND:
        return "Not Found";
    case HTTP_STATUS_TOO_MANY_REQUESTS:
        return "Too Many Requests";
    case HTTP_STATUS_INTERNAL_SERVER_ERROR:
        return "Internal Server Error";
    case HTTP_STATUS_BAD_GATEWAY:
        return "Bad Gateway";
    case HTTP_STATUS_SERVICE_UNAVAILABLE:
        return "Service Unavailable";
    default:
        return "Unknown";
    }
}

bool http_is_retryable(HttpStatusCode code) {
    return code >= 500 || code == HTTP_STATUS_TOO_MANY_REQUESTS;
}

char *http_url_encode(const char *src) {
    return url_percent_encode(src);
}
