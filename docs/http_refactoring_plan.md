# HTTP Client Refactoring Plan

## Executive Summary

This document outlines a comprehensive refactoring plan to decouple HTTP client logic from business logic across the kaufbot codebase. Currently, HTTP concerns are tightly coupled with business logic in three main modules: **Telegram Bot** (`bot.c`), **Gemini API Client** (`gemini.c`), and **Supabase Storage** (`storage_supabase.c`).

The goal is to introduce a unified **HTTP Client abstraction** that centralizes technical HTTP concerns (curl initialization, request configuration, error handling, response buffering) while leaving business logic modules responsible for:
- URL construction
- Request/response payload serialization
- Business-specific error interpretation

---

## Table of Contents

1. [Current State Analysis](#current-state-analysis)
2. [Identified Code Smells](#identified-code-smells)
3. [Proposed Architecture](#proposed-architecture)
4. [Refactoring Plan](#refactoring-plan)
5. [Implementation Checklist](#implementation-checklist)
6. [Testing Strategy](#testing-strategy)
7. [Migration Guide](#migration-guide)

---

## Current State Analysis

### 1. Telegram Bot Module (`src/bot.c`)

**Current Responsibilities (Mixed Concerns):**
- Long-polling orchestration
- Command handling
- File download coordination
- **HTTP GET/POST implementation** (lines 45-124)
- **Telegram API response parsing** (lines 126-148)
- **File download via curl** (lines 298-344)

**HTTP Functions:**
```c
static char *http_get(const char *url, long timeout_sec);      // Lines 45-77
static char *http_post_json(const char *url, const char *json_body); // Lines 79-124
```

**Telegram API Endpoints:**
| Endpoint | Method | Purpose | Timeout |
|----------|--------|---------|---------|
| `/sendMessage` | POST JSON | Send text messages | 30s |
| `/answerCallbackQuery` | POST JSON | Acknowledge callback | 30s |
| `/deleteMessage` | POST JSON | Delete messages | 30s |
| `/getFile` | GET | Get file metadata | 60s |
| `/file/bot{token}/{path}` | GET | Download file content | 120s |
| `/getUpdates` | GET | Long-polling updates | 30s + connect |

**Timeout Constants:**
```c
#define HTTP_TIMEOUT_SECS            60L
#define HTTP_CONNECT_TIMEOUT_SECS    10L
#define HTTP_POST_TIMEOUT_SECS       30L
#define HTTP_DOWNLOAD_TIMEOUT_SECS   120L
```

---

### 2. Gemini API Module (`src/gemini.c`)

**Current Responsibilities (Mixed Concerns):**
- OCR text extraction
- Receipt parsing
- Rate-limit fallback logic
- **HTTP POST implementation** (lines 203-275)
- **JSON body construction** (lines 277-311)
- **Token usage logging** (lines 243-258)

**HTTP Function:**
```c
static char *gemini_post_and_parse(GeminiClient *client, const char *body); // Lines 203-275
```

**Gemini API Endpoint:**
| Endpoint | Method | Purpose | Timeout |
|----------|--------|---------|---------|
| `/v1beta/models/{model}:generateContent` | POST JSON | OCR & parsing | 600s |

**Timeout Constants:**
```c
#define GEMINI_HTTP_TIMEOUT_SECS         600L
#define GEMINI_HTTP_CONNECT_TIMEOUT_SECS 15L
```

**Special Concerns:**
- Rate-limit handling (429 → fallback model)
- Token usage metadata extraction
- Markdown JSON stripping

---

### 3. Supabase Storage Module (`src/storage_supabase.c`)

**Current Responsibilities (Mixed Concerns):**
- File upload/download
- Signed URL generation
- **HTTP PUT/GET/HEAD/DELETE implementation** (multiple functions)
- **Auth header construction** (lines 24-36)

**HTTP Functions:**
```c
static int supabase_save_file(...);        // PUT upload (lines 85-127)
static int supabase_file_exists(...);      // HEAD request (lines 141-168)
static char *supabase_signed_url(...);     // POST for signed URL (lines 183-236)
static int supabase_check_public_access(); // HEAD request (lines 248-271)
static int supabase_delete_file(...);      // DELETE request (lines 273-310)
static char *supabase_read_text(...);      // GET request (lines 327-361)
```

**Supabase API Endpoints:**
| Endpoint | Method | Purpose | Timeout |
|----------|--------|---------|---------|
| `/storage/v1/object/{bucket}/{file}` | PUT | Upload file | 120s |
| `/storage/v1/object/{bucket}/{file}` | HEAD | Check existence | 30s |
| `/storage/v1/object/sign/{bucket}/{file}` | POST | Generate signed URL | 30s |
| `/storage/v1/object/public/{bucket}/{file}` | HEAD/GET | Public access | 30s/120s |
| `/storage/v1/object/{bucket}/{file}` | DELETE | Delete file | 120s |

**Timeout Constants:**
```c
#define SUPABASE_HTTP_TIMEOUT_SECS      120L
#define SUPABASE_HTTP_HEAD_TIMEOUT_SECS 30L
#define SUPABASE_HTTP_SIGN_TIMEOUT_SECS 30L
```

**Special Concerns:**
- V1 vs V2 API key format detection
- Custom auth headers (`Authorization: Bearer` + `apikey`)
- `x-upsert: true` header for uploads

---

### 4. Shared Utilities (`src/utils.c`, `src/utils.h`)

**Current Shared Component:**
```c
typedef struct {
    char  *data;
    size_t len;
    size_t cap;
} GrowBuf;

size_t growbuf_write_cb(void *ptr, size_t size, size_t nmemb, void *userdata);
void growbuf_free(GrowBuf *buf);
```

**Usage:** All three modules use `GrowBuf` for curl response buffering.

---

## Identified Code Smells

### 1. **Duplicate HTTP Setup Code** 🔴

**Problem:** Each module repeats curl initialization, timeout configuration, and error handling.

**Example - bot.c:**
```c
CURL *curl = curl_easy_init();
if (!curl) return NULL;
GrowBuf buf = {0};
curl_easy_setopt(curl, CURLOPT_URL, url);
curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, growbuf_write_cb);
curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_sec + HTTP_CONNECT_TIMEOUT_SECS);
curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, HTTP_CONNECT_TIMEOUT_SECS);
curl_easy_setopt(curl, CURLOPT_FORBID_REUSE, 1L);
```

**Example - gemini.c:**
```c
CURL *curl = curl_easy_init();
if (!curl) { LOG_ERROR("curl_easy_init failed"); return NULL; }
GrowBuf resp = {0};
struct curl_slist *headers = NULL;
headers = curl_slist_append(headers, "Content-Type: application/json");
curl_easy_setopt(curl, CURLOPT_URL, url);
curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, growbuf_write_cb);
curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
curl_easy_setopt(curl, CURLOPT_TIMEOUT, GEMINI_HTTP_TIMEOUT_SECS);
```

**Impact:**
- 5+ duplicate code blocks across the codebase
- Inconsistent error handling
- Hard to apply global changes (e.g., adding retry logic, metrics)

---

### 2. **Mixed Concerns** 🟡

**Problem:** Business logic functions contain HTTP implementation details.

**Example - `tg_send_message`:**
```c
void tg_send_message(const TgBot *bot, int64_t chat_id, const char *text) {
    // Business logic: construct URL
    char url[MAX_URL_LEN];
    snprintf(url, sizeof(url), "%s%s/sendMessage", TG_API_BASE, bot->cfg->telegram_token);
    
    // Business logic: build JSON payload
    cJSON *payload = cJSON_CreateObject();
    cJSON_AddNumberToObject(payload, "chat_id", (double)chat_id);
    cJSON_AddStringToObject(payload, "text", text);
    char *body = cJSON_PrintUnformatted(payload);
    cJSON_Delete(payload);
    
    // HTTP concern: make request
    char *resp = http_post_json(url, body);
    free(body);
    
    // Business logic: parse response
    tg_check_ok(resp, "sendMessage");
    free(resp);
}
```

**Impact:**
- Hard to test business logic without mocking curl
- HTTP changes require touching business logic files
- Violates Single Responsibility Principle

---

### 3. **Inconsistent Error Handling** 🟡

**Problem:** Each module handles HTTP errors differently.

| Module | Error Logging | Return Value | Cleanup |
|--------|--------------|--------------|---------|
| `bot.c` | `LOG_ERROR` | `NULL` / `-1` | Manual `growbuf_free` |
| `gemini.c` | `LOG_ERROR` + token logging | `NULL` | Manual `free(resp.data)` |
| `storage_supabase.c` | `LOG_ERROR` | `-1` / `0` / `1` | Manual `growbuf_free` |

**Impact:**
- Inconsistent observability
- Hard to implement global error handling (e.g., retry on 5xx)
- Memory leak risk (manual cleanup)

---

### 4. **No Retry Logic** 🟡

**Problem:** Transient network failures cause immediate failures.

**Current Behavior:**
- Single request attempt
- No exponential backoff
- No circuit breaker pattern

**Impact:**
- Poor reliability in production
- Manual retry commands required (`/retry`)

---

### 5. **No Centralized Configuration** 🟡

**Problem:** Timeout constants are scattered across files.

```c
// bot.c
#define HTTP_TIMEOUT_SECS            60L
#define HTTP_CONNECT_TIMEOUT_SECS    10L
#define HTTP_POST_TIMEOUT_SECS       30L
#define HTTP_DOWNLOAD_TIMEOUT_SECS   120L

// gemini.c
#define GEMINI_HTTP_TIMEOUT_SECS         600L
#define GEMINI_HTTP_CONNECT_TIMEOUT_SECS 15L

// storage_supabase.c
#define SUPABASE_HTTP_TIMEOUT_SECS      120L
#define SUPABASE_HTTP_HEAD_TIMEOUT_SECS 30L
#define SUPABASE_HTTP_SIGN_TIMEOUT_SECS 30L
```

**Impact:**
- Hard to tune globally
- Inconsistent timeout strategies

---

### 6. **No Request/Response Logging** 🟡

**Problem:** Limited visibility into HTTP traffic.

**Current State:**
- Only URL and status code logged
- No request body logging
- No response size/timing metrics

**Impact:**
- Hard to debug API issues
- No performance monitoring

---

### 7. **Tight Coupling to libcurl** 🔴

**Problem:** All modules directly depend on libcurl API.

**Impact:**
- Hard to swap HTTP library (e.g., for embedded systems)
- Hard to mock for unit tests
- curl-specific patterns leak into business logic

---

## Proposed Architecture

### Overview

```
┌─────────────────────────────────────────────────────────────┐
│                    Business Logic Layer                      │
├──────────────────┬──────────────────┬───────────────────────┤
│   Telegram Bot   │   Gemini Client  │   Supabase Storage    │
│   (bot.c)        │   (gemini.c)     │   (storage_supabase.c)│
└────────┬─────────┴─────────┬────────┴──────────┬────────────┘
         │                   │                    │
         ▼                   ▼                    ▼
┌─────────────────────────────────────────────────────────────┐
│                  HTTP Client Abstraction                     │
│  ┌──────────────────────────────────────────────────────┐   │
│  │  HttpClient struct (opaque)                          │   │
│  │  - curl handle pool                                  │   │
│  │  - default timeouts                                  │   │
│  │  - retry config                                      │   │
│  │  - request/response hooks (logging, metrics)         │   │
│  └──────────────────────────────────────────────────────┘   │
│                                                              │
│  HttpClient_Request(client, &req, &resp)                    │
│  HttpClient_Get(client, url, timeout, &resp)                │
│  HttpClient_Post(client, url, body, content_type, &resp)    │
│  HttpClient_Put(client, url, data, len, &resp)              │
│  HttpClient_Delete(client, url, &resp)                      │
│  HttpClient_Head(client, url, timeout, &resp)               │
└─────────────────────────────────────────────────────────────┘
         │
         ▼
┌─────────────────────────────────────────────────────────────┐
│                    libcurl (implementation)                  │
└─────────────────────────────────────────────────────────────┘
```

---

### New Module: `http_client.h` / `http_client.c`

#### Header File (`http_client.h`)

```c
#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* ── Forward declarations ───────────────────────────────────────────────── */

typedef struct HttpClient HttpClient;
typedef struct HttpRequest HttpRequest;
typedef struct HttpResponse HttpResponse;
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

typedef struct {
    HttpMethod method;
    const char *url;
    const char *body;           /* For POST/PUT/PATCH */
    size_t body_len;            /* For binary bodies */
    HttpHeaders *headers;       /* Optional custom headers */
    long timeout_secs;          /* Request-specific timeout */
    long connect_timeout_secs;  /* Connection timeout */
    bool follow_redirects;      /* Follow 3xx redirects */
    void *user_data;            /* For hooks/callbacks */
} HttpRequest;

typedef struct {
    HttpStatusCode status_code;
    char *body;                 /* NUL-terminated response body */
    size_t body_len;            /* Length excluding NUL terminator */
    HttpHeaders *headers;       /* Response headers */
    long total_time_ms;         /* Total request time */
    long connect_time_ms;       /* Connection time */
    long download_time_ms;      /* Download time */
    char error[256];            /* Error message if failed */
    bool success;               /* true if request succeeded */
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
int http_client_head(HttpClient *client, const char *url, long timeout_secs, HttpResponse *response);

/* ── Response Management ────────────────────────────────────────────────── */

/* Free response resources. Safe to call on partially initialized response. */
void http_response_free(HttpResponse *response);

/* Check if response has successful status code (2xx). */
bool http_response_is_success(const HttpResponse *response);

/* Parse response body as JSON (returns cJSON*, caller must free). */
void *http_response_parse_json(const HttpResponse *response);

/* ── Utility Functions ──────────────────────────────────────────────────── */

/* Get human-readable status code description. */
const char *http_status_description(HttpStatusCode code);

/* Check if status code indicates retryable error (5xx, 429). */
bool http_is_retryable(HttpStatusCode code);

/* URL percent-encode a string (wrapper around utils.c). */
char *http_url_encode(const char *src);

#endif /* HTTP_CLIENT_H */
```

---

#### Implementation File (`http_client.c`) - Key Sections

```c
#include "http_client.h"
#include "config.h"
#include "utils.h"

#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ── Internal Structures ────────────────────────────────────────────────── */

struct HttpClient {
    HttpClientConfig config;
    CURL *curl;
    bool owns_curl;
};

struct HttpHeaders {
    HttpHeader *headers;
    size_t count;
    size_t capacity;
};

/* ── Helper Functions ───────────────────────────────────────────────────── */

static void log_request(const HttpRequest *req) {
    LOG_INFO("HTTP %s %s", 
             req->method == HTTP_METHOD_GET ? "GET" :
             req->method == HTTP_METHOD_POST ? "POST" :
             req->method == HTTP_METHOD_PUT ? "PUT" :
             req->method == HTTP_METHOD_DELETE ? "DELETE" :
             req->method == HTTP_METHOD_HEAD ? "HEAD" : "UNKNOWN",
             req->url);
    
    if (req->body && req->method == HTTP_METHOD_POST) {
        LOG_DEBUG("Request body: %.500s", req->body);
    }
}

static void log_response(const HttpResponse *resp) {
    if (resp->success) {
        LOG_INFO("HTTP %ld - %ldms (body: %zu bytes)",
                 (long)resp->status_code, resp->total_time_ms, resp->body_len);
    } else {
        LOG_ERROR("HTTP request failed: %s", resp->error);
    }
}

static size_t header_callback(char *buffer, size_t size, size_t nitems, void *userdata) {
    /* Parse and store response headers if needed */
    (void)buffer;
    (void)nitems;
    (void)userdata;
    return size * nitems;
}

static bool should_retry(const HttpResponse *resp, int attempt, int max_retries) {
    if (attempt >= max_retries) return false;
    return http_is_retryable(resp->status_code);
}

static void exponential_backoff(int attempt, double base_delay_ms) {
    double delay_ms = base_delay_ms * (1 << attempt); /* 2^attempt */
    if (delay_ms > 10000) delay_ms = 10000; /* Cap at 10s */
    
    LOG_DEBUG("Retry attempt %d, waiting %.0fms", attempt + 1, delay_ms);
    usleep((useconds_t)(delay_ms * 1000));
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
    if (!client) return NULL;
    
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
    if (!client) return;
    if (client->owns_curl && client->curl) {
        curl_easy_cleanup(client->curl);
    }
    free(client);
}

/* ── Request Execution ──────────────────────────────────────────────────── */

static void configure_curl(HttpClient *client, CURL *curl, const HttpRequest *req) {
    curl_easy_reset(curl); /* Reset to clean state */
    
    curl_easy_setopt(curl, CURLOPT_URL, req->url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, growbuf_write_cb);
    
    /* Timeouts */
    long timeout = req->timeout_secs > 0 ? req->timeout_secs : client->config.default_timeout_secs;
    long connect_timeout = req->connect_timeout_secs > 0 ? req->connect_timeout_secs 
                                                          : client->config.default_connect_timeout_secs;
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, connect_timeout);
    
    /* Redirects */
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, req->follow_redirects ? 1L : 0L);
    
    /* Method-specific options */
    switch (req->method) {
        case HTTP_METHOD_POST:
        case HTTP_METHOD_PUT:
        case HTTP_METHOD_PATCH:
            if (req->body) {
                if (req->body_len > 0) {
                    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)req->body_len);
                }
                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, req->body);
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
        struct curl_slist *headers = NULL;
        for (size_t i = 0; i < req->headers->count; i++) {
            char hdr[512];
            snprintf(hdr, sizeof(hdr), "%s: %s", 
                     req->headers->headers[i].key, 
                     req->headers->headers[i].value);
            headers = curl_slist_append(headers, hdr);
        }
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        /* Note: headers must be freed after curl_easy_perform */
    }
}

int http_client_execute(HttpClient *client, const HttpRequest *request, HttpResponse *response) {
    if (!client || !request || !response) return -1;
    
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
    do {
        if (attempt > 0) {
            exponential_backoff(attempt, client->config.retry_base_delay_ms);
        }
        
        /* Configure curl */
        GrowBuf resp_buf = {0};
        CURL *curl = client->curl;
        configure_curl(client, curl, request);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp_buf);
        
        /* Execute */
        CURLcode res = curl_easy_perform(curl);
        
        /* Get metadata */
        long http_code = 0;
        double total_time = 0, connect_time = 0, download_time = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &total_time);
        curl_easy_getinfo(curl, CURLINFO_CONNECT_TIME, &connect_time);
        curl_easy_getinfo(curl, CURLINFO_DOWNLOAD_TIME, &download_time);
        
        /* Populate response */
        response->status_code = (HttpStatusCode)http_code;
        response->body = resp_buf.data;
        response->body_len = resp_buf.len;
        response->total_time_ms = (long)(total_time * 1000);
        response->connect_time_ms = (long)(connect_time * 1000);
        response->download_time_ms = (long)(download_time * 1000);
        response->success = (res == CURLE_OK && http_code >= 200 && http_code < 300);
        
        if (res != CURLE_OK) {
            snprintf(response->error, sizeof(response->error), 
                     "curl error: %s", curl_easy_strerror(res));
        }
        
        /* Check for retry */
        if (should_retry(response, attempt, client->config.max_retries)) {
            LOG_WARN("Retryable error (HTTP %ld), attempt %d/%d",
                     (long)response->status_code, attempt + 1, client->config.max_retries);
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
    HttpRequest req = {
        .method = HTTP_METHOD_GET,
        .url = url,
        .timeout_secs = timeout_secs,
    };
    return http_client_execute(client, &req, response);
}

int http_client_post_json(HttpClient *client, const char *url, const char *json_body,
                          HttpResponse *response) {
    HttpRequest req = {
        .method = HTTP_METHOD_POST,
        .url = url,
        .body = json_body,
        .body_len = strlen(json_body),
    };
    
    /* Add Content-Type header */
    HttpHeader headers[] = {
        {"Content-Type", "application/json"},
    };
    HttpHeaders hdrs = {
        .headers = headers,
        .count = 1,
        .capacity = 1,
    };
    req.headers = &hdrs;
    
    return http_client_execute(client, &req, response);
}

/* ... other convenience methods ... */

/* ── Response Management ────────────────────────────────────────────────── */

void http_response_free(HttpResponse *response) {
    if (!response) return;
    free(response->body);
    response->body = NULL;
    response->body_len = 0;
    /* Free headers if implemented */
}

bool http_response_is_success(const HttpResponse *response) {
    return response && response->success;
}

/* ── Utility Functions ──────────────────────────────────────────────────── */

const char *http_status_description(HttpStatusCode code) {
    switch (code) {
        case HTTP_STATUS_OK: return "OK";
        case HTTP_STATUS_CREATED: return "Created";
        case HTTP_STATUS_NO_CONTENT: return "No Content";
        case HTTP_STATUS_BAD_REQUEST: return "Bad Request";
        case HTTP_STATUS_UNAUTHORIZED: return "Unauthorized";
        case HTTP_STATUS_FORBIDDEN: return "Forbidden";
        case HTTP_STATUS_NOT_FOUND: return "Not Found";
        case HTTP_STATUS_TOO_MANY_REQUESTS: return "Too Many Requests";
        case HTTP_STATUS_INTERNAL_SERVER_ERROR: return "Internal Server Error";
        case HTTP_STATUS_BAD_GATEWAY: return "Bad Gateway";
        case HTTP_STATUS_SERVICE_UNAVAILABLE: return "Service Unavailable";
        default: return "Unknown";
    }
}

bool http_is_retryable(HttpStatusCode code) {
    return code >= 500 || code == HTTP_STATUS_TOO_MANY_REQUESTS;
}

char *http_url_encode(const char *src) {
    return url_percent_encode(src); /* Wrapper around utils.c */
}
```

---

## Refactoring Plan

### Phase 1: Create HTTP Client Module

**Files to Create:**
- `src/http_client.h`
- `src/http_client.c`

**Tasks:**
1. Define `HttpClient`, `HttpRequest`, `HttpResponse` structures
2. Implement lifecycle functions (`http_client_new`, `http_client_free`)
3. Implement core execution function (`http_client_execute`)
4. Implement convenience methods (`http_client_get`, `http_client_post_json`, etc.)
5. Implement response management (`http_response_free`, `http_response_is_success`)
6. Add logging hooks and observability features
7. Add retry logic with exponential backoff

**Acceptance Criteria:**
- [ ] All functions have documentation comments
- [ ] Error handling is consistent
- [ ] Memory management is clear (caller vs library ownership)
- [ ] Logging is configurable

---

### Phase 2: Refactor Telegram Bot Module

**Files to Modify:**
- `src/bot.c`
- `src/bot.h` (potentially)

**Tasks:**
1. Add `HttpClient *http_client` field to `TgBot` struct (or keep as module-global)
2. Replace `http_get()` with `http_client_get()`
3. Replace `http_post_json()` with `http_client_post_json()`
4. Update `tg_download_file()` to use new HTTP client
5. Remove duplicate timeout constants (use client config)
6. Update error handling to use new response structure

**Before:**
```c
static char *http_get(const char *url, long timeout_sec) {
    CURL *curl = curl_easy_init();
    if (!curl) return NULL;
    GrowBuf buf = {0};
    /* ... 30 lines of curl setup ... */
    return buf.data;
}
```

**After:**
```c
static char *http_get(HttpClient *client, const char *url, long timeout_sec) {
    HttpResponse resp;
    if (http_client_get(client, url, timeout_sec, &resp) != 0) {
        return NULL;
    }
    /* Transfer ownership of body */
    return resp.body; /* resp.body_len available if needed */
}
```

**Acceptance Criteria:**
- [ ] No direct curl calls in `bot.c`
- [ ] All HTTP logic uses `http_client_*` functions
- [ ] Error handling is consistent
- [ ] All existing tests pass

---

### Phase 3: Refactor Gemini Client Module

**Files to Modify:**
- `src/gemini.c`
- `src/gemini.h`

**Tasks:**
1. Add `HttpClient *http_client` field to `GeminiClient` struct
2. Replace `gemini_post_and_parse()` internal logic with `http_client_post_json()`
3. Move rate-limit fallback logic to business layer (keep it, but separate from HTTP)
4. Move token usage logging to response hook or business layer
5. Update timeout configuration to use client config

**Before:**
```c
static char *gemini_post_and_parse(GeminiClient *client, const char *body) {
    /* ... curl setup ... */
    /* Rate limit handling with goto retry */
    if (http_code == 429 && !tried_fallback && client->fallback_enabled) {
        client->fallback_until = next_midnight();
        goto retry;
    }
    /* ... */
}
```

**After:**
```c
static char *gemini_post_and_parse(GeminiClient *client, const char *body) {
    const char *active_model = get_active_model(client);
    char url[GEMINI_URL_BUF_LEN];
    snprintf(url, sizeof(url), "%s/%s:generateContent?key=%s", 
             GEMINI_API_BASE, active_model, client->api_key);
    
    HttpResponse resp;
    int rc = http_client_post_json(client->http_client, url, body, &resp);
    
    /* Business logic: handle 429 with fallback */
    if (rc != 0 && resp.status_code == HTTP_STATUS_TOO_MANY_REQUESTS) {
        return handle_rate_limit(client, body); /* Extract to separate function */
    }
    
    return gemini_parse_api_response(resp.body);
}
```

**Acceptance Criteria:**
- [ ] No direct curl calls in `gemini.c`
- [ ] Rate-limit fallback is clearly business logic
- [ ] Token logging uses response metadata
- [ ] All existing tests pass

---

### Phase 4: Refactor Supabase Storage Module

**Files to Modify:**
- `src/storage_supabase.c`

**Tasks:**
1. Add `HttpClient *http_client` field to `SupabaseStorage` struct
2. Replace all curl calls with appropriate `http_client_*` functions
3. Move auth header logic to request preparation
4. Consolidate timeout configuration

**Before:**
```c
static int supabase_save_file(StorageBackend *backend, const char *filename, 
                              const uint8_t *data, size_t len) {
    SupabaseStorage *storage = (SupabaseStorage *)backend->internal;
    char url[MAX_COMPOSED_URL];
    snprintf(url, sizeof(url), "%s/storage/v1/object/%s/%s", 
             storage->base_url, storage->bucket, filename);
    
    CURL *curl = curl_easy_init();
    struct curl_slist *headers = build_auth_headers(storage);
    /* ... 40 lines of curl setup ... */
}
```

**After:**
```c
static int supabase_save_file(StorageBackend *backend, const char *filename, 
                              const uint8_t *data, size_t len) {
    SupabaseStorage *storage = (SupabaseStorage *)backend->internal;
    char url[MAX_COMPOSED_URL];
    snprintf(url, sizeof(url), "%s/storage/v1/object/%s/%s", 
             storage->base_url, storage->bucket, filename);
    
    HttpRequest req = {
        .method = HTTP_METHOD_PUT,
        .url = url,
        .body = (const char *)data,
        .body_len = len,
    };
    
    /* Build auth headers */
    HttpHeader auth_headers[3];
    int hdr_count = build_auth_headers(storage, auth_headers, 3);
    req.headers = &(HttpHeaders){ .headers = auth_headers, .count = hdr_count };
    
    HttpResponse resp;
    int rc = http_client_execute(storage->http_client, &req, &resp);
    
    if (rc != 0 || resp.status_code < 200 || resp.status_code >= 300) {
        LOG_ERROR("upload failed: HTTP %ld", (long)resp.status_code);
        return -1;
    }
    return 0;
}
```

**Acceptance Criteria:**
- [ ] No direct curl calls in `storage_supabase.c`
- [ ] All HTTP methods (PUT, HEAD, DELETE, GET) use new client
- [ ] Auth header logic is preserved
- [ ] All existing tests pass

---

### Phase 5: Update Build System and Tests

**Files to Modify:**
- `CMakeLists.txt`
- Test files

**Tasks:**
1. Add `http_client.c` to build
2. Update include paths
3. Create unit tests for HTTP client
4. Update integration tests if needed

**CMakeLists.txt Changes:**
```cmake
add_executable(kaufbot
    src/main.c
    src/bot.c
    src/processor.c
    src/gemini.c
    src/config.c
    src/utils.c
    src/prompt_fetcher.c
    src/db_sqlite.c
    src/db_postgres.c
    src/storage_local.c
    src/storage_supabase.c
    src/http_client.c          # NEW
    third_party/cjson/cJSON.c
)
```

**Acceptance Criteria:**
- [ ] Build succeeds without warnings
- [ ] All tests pass
- [ ] No memory leaks (valgrind clean)

---

## Implementation Checklist

### Phase 1: HTTP Client Module
- [ ] Create `src/http_client.h` with full API
- [ ] Create `src/http_client.c` with implementation
- [ ] Implement `HttpClient` lifecycle (new/free)
- [ ] Implement `http_client_execute()` core function
- [ ] Implement convenience methods (GET, POST, PUT, DELETE, HEAD)
- [ ] Implement response management (free, is_success, parse_json)
- [ ] Add logging hooks (request/response/error)
- [ ] Add retry logic with exponential backoff
- [ ] Add timeout configuration
- [ ] Write unit tests for HTTP client
- [ ] Add documentation comments

### Phase 2: Telegram Bot Refactoring
- [ ] Add `HttpClient` to `TgBot` struct or module
- [ ] Replace `http_get()` with `http_client_get()`
- [ ] Replace `http_post_json()` with `http_client_post_json()`
- [ ] Update `tg_download_file()` to use new client
- [ ] Remove duplicate timeout constants
- [ ] Update error handling
- [ ] Remove curl includes from `bot.c`
- [ ] Run tests and verify functionality

### Phase 3: Gemini Client Refactoring
- [ ] Add `HttpClient` to `GeminiClient` struct
- [ ] Refactor `gemini_post_and_parse()` to use new client
- [ ] Extract rate-limit fallback to separate function
- [ ] Update token logging to use response metadata
- [ ] Update timeout configuration
- [ ] Remove curl includes from `gemini.c`
- [ ] Run tests and verify functionality

### Phase 4: Supabase Storage Refactoring
- [ ] Add `HttpClient` to `SupabaseStorage` struct
- [ ] Refactor `supabase_save_file()` (PUT)
- [ ] Refactor `supabase_file_exists()` (HEAD)
- [ ] Refactor `supabase_signed_url()` (POST)
- [ ] Refactor `supabase_check_public_access()` (HEAD)
- [ ] Refactor `supabase_delete_file()` (DELETE)
- [ ] Refactor `supabase_read_text()` (GET)
- [ ] Update auth header logic
- [ ] Remove curl includes from `storage_supabase.c`
- [ ] Run tests and verify functionality

### Phase 5: Build and Tests
- [ ] Update `CMakeLists.txt` to include `http_client.c`
- [ ] Ensure build succeeds without warnings
- [ ] Run all existing tests
- [ ] Add unit tests for HTTP client module
- [ ] Run valgrind to check for memory leaks
- [ ] Update CI/CD if needed

---

## Testing Strategy

### Unit Tests (New)

**File:** `tests/test_http_client.c`

```c
#include "http_client.h"
#include <assert.h>
#include <stdio.h>

void test_http_client_creation(void) {
    HttpClient *client = http_client_new();
    assert(client != NULL);
    http_client_free(client);
}

void test_http_client_get_success(void) {
    HttpClient *client = http_client_new();
    HttpResponse resp;
    int rc = http_client_get(client, "https://httpbin.org/status/200", 10, &resp);
    assert(rc == 0);
    assert(resp.success == true);
    assert(resp.status_code == HTTP_STATUS_OK);
    http_response_free(&resp);
    http_client_free(client);
}

void test_http_client_get_error(void) {
    HttpClient *client = http_client_new();
    HttpResponse resp;
    int rc = http_client_get(client, "https://httpbin.org/status/404", 10, &resp);
    assert(rc == 0); /* Network succeeded */
    assert(resp.success == false);
    assert(resp.status_code == HTTP_STATUS_NOT_FOUND);
    http_response_free(&resp);
    http_client_free(client);
}

void test_http_client_post_json(void) {
    HttpClient *client = http_client_new();
    const char *json = "{\"test\": \"value\"}";
    HttpResponse resp;
    int rc = http_client_post_json(client, "https://httpbin.org/post", json, &resp);
    assert(rc == 0);
    assert(resp.success == true);
    http_response_free(&resp);
    http_client_free(client);
}

void test_http_client_retry(void) {
    HttpClientConfig config = {
        .default_timeout_secs = 10,
        .max_retries = 3,
        .retry_base_delay_ms = 10,
    };
    HttpClient *client = http_client_new_custom(&config);
    /* Test with mock server that fails first 2 requests */
    http_client_free(client);
}

int main(void) {
    test_http_client_creation();
    test_http_client_get_success();
    test_http_client_get_error();
    test_http_client_post_json();
    test_http_client_retry();
    printf("All HTTP client tests passed!\n");
    return 0;
}
```

### Integration Tests (Existing - Verify No Regression)

**Run Existing Test Suite:**
```bash
./test_bot
./test_config
./test_db
./test_edge_cases
./test_json
./test_processor
./test_prompts
./test_storage
./test_utils
```

### Manual Testing

**Telegram Bot:**
1. Send `/help` command
2. Send image file
3. Send PDF file
4. Test `/list` command
5. Test `/delete` command
6. Test `/retry` command
7. Test callback buttons (Retry OCR, Delete)

**Gemini Integration:**
1. Upload receipt image
2. Verify OCR extraction
3. Verify JSON parsing
4. Test rate-limit fallback (if possible)

**Supabase Storage:**
1. Upload file
2. Verify file exists
3. Download file
4. Generate signed URL
5. Delete file

---

## Migration Guide

### For Developers

#### Before Refactoring
```c
/* bot.c */
#include <curl/curl.h>

static char *http_get(const char *url, long timeout_sec) {
    CURL *curl = curl_easy_init();
    /* ... */
}
```

#### After Refactoring
```c
/* bot.c */
#include "http_client.h"

static char *http_get(HttpClient *client, const char *url, long timeout_sec) {
    HttpResponse resp;
    if (http_client_get(client, url, timeout_sec, &resp) != 0) {
        return NULL;
    }
    return resp.body; /* Caller must free */
}
```

### Key Changes

| Aspect | Before | After |
|--------|--------|-------|
| **HTTP Library** | Direct curl calls | `http_client_*` functions |
| **Error Handling** | Manual curl error checks | `response.success` flag |
| **Response Body** | `GrowBuf` manually managed | `response.body` (freed by `http_response_free`) |
| **Timeouts** | Per-function constants | Client configuration |
| **Retry Logic** | None (except Gemini 429) | Built-in with backoff |
| **Logging** | Manual `LOG_*` calls | Automatic via hooks |
| **Testing** | Hard to mock | Easy to mock via interface |

### Common Patterns

#### Making a GET Request

**Before:**
```c
char *resp = http_get(url, HTTP_TIMEOUT_SECS);
if (!resp) {
    LOG_ERROR("GET failed");
    return -1;
}
/* Use resp */
free(resp);
```

**After:**
```c
HttpResponse resp;
if (http_client_get(client, url, 0, &resp) != 0) {
    LOG_ERROR("GET failed: %s", resp.error);
    return -1;
}
if (!resp.success) {
    LOG_ERROR("GET returned HTTP %ld", (long)resp.status_code);
    http_response_free(&resp);
    return -1;
}
/* Use resp.body */
http_response_free(&resp);
```

#### Making a POST Request with JSON

**Before:**
```c
char *resp = http_post_json(url, json_body);
if (!resp) {
    LOG_ERROR("POST failed");
    return -1;
}
/* Use resp */
free(resp);
```

**After:**
```c
HttpResponse resp;
if (http_client_post_json(client, url, json_body, &resp) != 0) {
    LOG_ERROR("POST failed: %s", resp.error);
    return -1;
}
if (!resp.success) {
    LOG_ERROR("POST returned HTTP %ld", (long)resp.status_code);
    http_response_free(&resp);
    return -1;
}
/* Use resp.body */
http_response_free(&resp);
```

#### Handling Errors

**Before:**
```c
CURLcode res = curl_easy_perform(curl);
if (res != CURLE_OK) {
    LOG_ERROR("curl error: %s", curl_easy_strerror(res));
    return NULL;
}
long http_code = 0;
curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
if (http_code != 200) {
    LOG_ERROR("HTTP %ld", http_code);
    return NULL;
}
```

**After:**
```c
HttpResponse resp;
int rc = http_client_get(client, url, 0, &resp);
if (rc != 0) {
    LOG_ERROR("Network error: %s", resp.error);
    return NULL;
}
if (!resp.success) {
    LOG_ERROR("HTTP error: %ld - %s", 
              (long)resp.status_code, 
              http_status_description(resp.status_code));
    http_response_free(&resp);
    return NULL;
}
```

---

## Benefits of Refactoring

### 1. **Separation of Concerns**
- Business logic no longer knows about curl
- HTTP concerns centralized in one module
- Easier to understand and maintain

### 2. **Improved Testability**
- HTTP client can be mocked for unit tests
- Business logic can be tested without network
- Better code coverage possible

### 3. **Consistent Error Handling**
- Single place to define error handling strategy
- Uniform logging across all HTTP calls
- Easier to add global error recovery

### 4. **Built-in Reliability**
- Retry logic with exponential backoff
- Configurable per-client or per-request
- Reduces transient failure impact

### 5. **Better Observability**
- Request/response hooks for metrics
- Centralized logging configuration
- Easier to add tracing/monitoring

### 6. **Flexibility**
- Easy to swap HTTP library (e.g., for embedded)
- Easy to add features (proxies, auth, etc.)
- Configuration-driven behavior

### 7. **Code Reduction**
- Eliminate ~200+ lines of duplicate curl setup
- Reduce cognitive load in business modules
- Fewer places for bugs to hide

---

## Risks and Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| **Breaking existing functionality** | High | Comprehensive testing, phased rollout |
| **Performance regression** | Medium | Benchmark before/after, profile |
| **Memory leaks** | Medium | Valgrind testing, clear ownership |
| **Incomplete migration** | Low | Code review checklist, grep for curl |
| **Learning curve** | Low | Documentation, examples, pair programming |

---

## Timeline Estimate

| Phase | Effort | Dependencies |
|-------|--------|--------------|
| Phase 1: HTTP Client Module | 2-3 days | None |
| Phase 2: Telegram Bot | 1-2 days | Phase 1 |
| Phase 3: Gemini Client | 1-2 days | Phase 1 |
| Phase 4: Supabase Storage | 2-3 days | Phase 1 |
| Phase 5: Build and Tests | 1 day | Phases 2-4 |
| **Total** | **7-11 days** | |

---

## Conclusion

This refactoring will significantly improve the codebase by:
1. Decoupling technical HTTP concerns from business logic
2. Reducing code duplication (~200+ lines)
3. Improving testability and maintainability
4. Adding reliability features (retry, backoff)
5. Enabling better observability

The phased approach allows incremental progress with minimal risk, and the clear separation of concerns will make future enhancements easier to implement.

---

## Appendix A: File Locations

| File | Purpose |
|------|---------|
| `src/http_client.h` | HTTP client API (NEW) |
| `src/http_client.c` | HTTP client implementation (NEW) |
| `src/bot.c` | Telegram bot (TO REFACTOR) |
| `src/gemini.c` | Gemini API client (TO REFACTOR) |
| `src/storage_supabase.c` | Supabase storage (TO REFACTOR) |
| `src/utils.h` | GrowBuf utilities (KEEP) |
| `src/utils.c` | GrowBuf implementation (KEEP) |
| `tests/test_http_client.c` | HTTP client tests (NEW) |

---

## Appendix B: Glossary

| Term | Definition |
|------|------------|
| **GrowBuf** | Dynamic buffer for HTTP responses |
| **Long-polling** | Telegram Bot API update mechanism |
| **Exponential Backoff** | Retry strategy with increasing delays |
| **RLS** | Row Level Security (Supabase) |
| **Inline Keyboard** | Telegram message with callback buttons |

---

## Appendix C: Related Documentation

- [components.md](components.md) - System architecture overview
- [modules.md](modules.md) - Module descriptions
- [backends.md](backends.md) - Backend implementation details
- [deployment.md](deployment.md) - Deployment guide
