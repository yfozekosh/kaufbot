#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ── Forward declarations ───────────────────────────────────────────────── */

typedef struct HttpClient HttpClient;
typedef struct HttpHeaders HttpHeaders;

/* ── HTTP Status Codes ──────────────────────────────────────────────────── */

typedef enum {
    HTTP_STATUS_OK = 200,
    HTTP_STATUS_CREATED = 201,
    HTTP_STATUS_NO_CONTENT = 204,
    HTTP_STATUS_BAD_REQUEST = 400,
    HTTP_STATUS_UNAUTHORIZED = 401,
    HTTP_STATUS_FORBIDDEN = 403,
    HTTP_STATUS_NOT_FOUND = 404,
    HTTP_STATUS_TOO_MANY_REQUESTS = 429,
    HTTP_STATUS_INTERNAL_SERVER_ERROR = 500,
    HTTP_STATUS_BAD_GATEWAY = 502,
    HTTP_STATUS_SERVICE_UNAVAILABLE = 503,
} HttpStatusCode;

/* ── Request/Response Structures ────────────────────────────────────────── */

typedef enum {
    HTTP_METHOD_GET,
    HTTP_METHOD_POST,
    HTTP_METHOD_PUT,
    HTTP_METHOD_DELETE,
    HTTP_METHOD_HEAD,
    HTTP_METHOD_PATCH,
} HttpMethod;

typedef struct {
    const char *key;
    const char *value;
} HttpHeader;

struct HttpHeaders {
    HttpHeader *headers;
    size_t count;
    size_t capacity;
};

typedef struct {
    HttpMethod method;
    const char *url;
    const char *body;          /* For POST/PUT/PATCH */
    size_t body_len;           /* For binary bodies */
    HttpHeaders *headers;      /* Optional custom headers */
    long timeout_secs;         /* Request-specific timeout (0 = default) */
    long connect_timeout_secs; /* Connection timeout (0 = default) */
    bool follow_redirects;     /* Follow 3xx redirects */
    void *user_data;           /* For hooks/callbacks */
} HttpRequest;

typedef struct {
    HttpStatusCode status_code;
    char *body;            /* NUL-terminated response body */
    size_t body_len;       /* Length excluding NUL terminator */
    HttpHeaders *headers;  /* Response headers (not yet implemented) */
    long total_time_ms;    /* Total request time */
    long connect_time_ms;  /* Connection time */
    long download_time_ms; /* Download time */
    char error[256];       /* Error message if failed */
    bool success;          /* true if request succeeded (2xx status) */
} HttpResponse;

/* ── Client Configuration ───────────────────────────────────────────────── */

typedef struct {
    long default_timeout_secs;
    long default_connect_timeout_secs;
    int max_retries;            /* 0 = no retry */
    double retry_base_delay_ms; /* Exponential backoff base */
    bool enable_logging;        /* Log request/response */
    bool follow_redirects;      /* Follow 3xx redirects */
    void *user_data;            /* Passed to hooks */

    /* Optional hooks for observability */
    void (*on_request)(const HttpRequest *req, void *user_data);
    void (*on_response)(const HttpResponse *resp, void *user_data);
    void (*on_error)(const HttpRequest *req, const HttpResponse *resp, void *user_data);
} HttpClientConfig;

/* ── Lifecycle ──────────────────────────────────────────────────────────── */

/* Create HTTP client with default configuration.
 * Returns NULL on failure (e.g., curl init failed). */
HttpClient *http_client_new(void);

/* Create HTTP client with custom configuration. */
HttpClient *http_client_new_custom(const HttpClientConfig *config);

/* Free HTTP client and all associated resources. */
void http_client_free(HttpClient *client);

/* ── Request Execution ──────────────────────────────────────────────────── */

/* Execute a generic HTTP request.
 * Returns 0 on success (even if HTTP status is error), -1 on network failure. */
int http_client_execute(HttpClient *client, const HttpRequest *request, HttpResponse *response);

/* Convenience methods for common HTTP verbs.
 * Response must be freed with http_response_free(). */
int http_client_get(HttpClient *client, const char *url, long timeout_secs, HttpResponse *response);
int http_client_post(HttpClient *client, const char *url, const char *body,
                     const char *content_type, HttpResponse *response);
int http_client_post_json(HttpClient *client, const char *url, const char *json_body,
                          HttpResponse *response);
int http_client_put(HttpClient *client, const char *url, const uint8_t *data, size_t len,
                    HttpResponse *response);
int http_client_delete(HttpClient *client, const char *url, HttpResponse *response);
int http_client_head(HttpClient *client, const char *url, long timeout_secs,
                     HttpResponse *response);

/* ── Response Management ────────────────────────────────────────────────── */

/* Free response resources. Safe to call on partially initialized response. */
void http_response_free(HttpResponse *response);

/* Check if response has successful status code (2xx). */
bool http_response_is_success(const HttpResponse *response);

/* ── Utility Functions ──────────────────────────────────────────────────── */

/* Get human-readable status code description. */
const char *http_status_description(HttpStatusCode code);

/* Check if status code indicates retryable error (5xx, 429). */
bool http_is_retryable(HttpStatusCode code);

/* URL percent-encode a string (wrapper around utils.c). */
char *http_url_encode(const char *src);

/* Header management helpers */
HttpHeaders *http_headers_new(size_t capacity);
void http_headers_free(HttpHeaders *headers);
int http_headers_add(HttpHeaders *headers, const char *key, const char *value);

#endif /* HTTP_CLIENT_H */
