#include "gemini.h"
#include "storage.h"
#include "../third_party/cjson/cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>

#define GEMINI_API_BASE "https://generativelanguage.googleapis.com/v1beta/models"
#define MAX_API_KEY_LEN  256
#define MAX_MODEL_LEN    128

struct GeminiClient {
    char api_key[MAX_API_KEY_LEN];
    char model[MAX_MODEL_LEN];
};

/* ── grow-buffer for libcurl response ────────────────────────────────────── */

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
        if (!tmp) return 0; /* signal error to curl */
        buf->data = tmp;
        buf->cap  = new_cap;
    }
    memcpy(buf->data + buf->len, ptr, incoming);
    buf->len += incoming;
    buf->data[buf->len] = '\0';
    return incoming;
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
    /* padding */
    if (len % 3 == 1) { out[j-1] = '='; out[j-2] = '='; }
    if (len % 3 == 2) { out[j-1] = '='; }
    out[j] = '\0';
    return out;
}

/* ── OCR prompt ───────────────────────────────────────────────────────────── */

static const char *ocr_prompt(void)
{
    return "Extract ALL text from this image or document. This document is a receipt from one of the well-know german supermarkets."
           "Preserve the original layout and formatting as closely as possible. "
           "Items on the same line in the image/pdf should stay on the same line in the output text."
           "If the line starts with the blank text - use spaces to align text with the text position on the image/document."
           "If there is no text, respond with: [NO TEXT FOUND]. "
           "Output only the extracted text, nothing else.";
}

static const char *parse_prompt(void)
{
    return "You are an expert German supermarket receipt parsing tool. You are given text "
           "extracted from a receipt photo via OCR. The layout is important, as item "
           "descriptions often span multiple lines.\n\n"
           "Your task is to extract the data and return it strictly as a JSON document "
           "matching the schema below. Do not include any conversational text, explanations, "
           "or Markdown formatting outside of the JSON block.\n\n"
           "Extraction Rules:\n\n"
           "Multi-line Items: Item names often wrap to the next line (e.g., an ID and "
           "partial name on line 1, and the rest of the name on line 2). Merge these into "
           "a single original_name string.\n\n"
           "Translation: Provide an accurate English translation for the full item name.\n\n"
           "Categorization: Intelligently assign a broad category (e.g., Electronics, "
           "Household, Groceries) and a more specific sub_category.\n\n"
           "Amounts and Units: If an amount isn't explicitly stated, default to 1. Extract "
           "the unit of measure (e.g., pieces, kg, g, m) from the item name if possible.\n\n"
           "Other Info: Group all remaining data (payment details, taxes, dates, times, "
           "terminal IDs) into the other object.\n\n"
           "Required JSON Schema:\n\n"
           "{\n"
           "  \"store_information\": {\n"
           "    \"name\": \"string\",\n"
           "    \"address\": \"string\"\n"
           "  },\n"
           "  \"line_items\": [\n"
           "    {\n"
           "      \"id\": \"string (if present)\",\n"
           "      \"original_name\": \"string\",\n"
           "      \"english_translation\": \"string\",\n"
           "      \"category\": \"string\",\n"
           "      \"sub_category\": \"string\",\n"
           "      \"price\": number,\n"
           "      \"tax_group\": \"string (if identifiable, else null)\",\n"
           "      \"amount\": number,\n"
           "      \"unit_of_measure\": \"string (e.g., pieces, m, cm, pack)\"\n"
           "    }\n"
           "  ],\n"
           "  \"total_sum\": number,\n"
           "  \"number_of_items\": number,\n"
           "  \"other\": {\n"
           "    \"date\": \"string\",\n"
           "    \"time\": \"string\",\n"
           "    \"receipt_number\": \"string\",\n"
           "    \"payment_method\": \"string\",\n"
           "    \"tax_details\": {},\n"
           "    \"card_details\": {},\n"
           "    \"raw_unmapped_text\": \"string (optional)\"\n"
           "  }\n"
           "}\n\n"
           "Input Text:\n";
}

/* ── public API ───────────────────────────────────────────────────────────── */

GeminiClient *gemini_new(const char *api_key, const char *model)
{
    GeminiClient *c = calloc(1, sizeof(GeminiClient));
    if (!c) return NULL;
    strncpy(c->api_key, api_key, MAX_API_KEY_LEN - 1);
    strncpy(c->model,   model,   MAX_MODEL_LEN   - 1);
    return c;
}

void gemini_free(GeminiClient *client)
{
    free(client);
}

char *gemini_extract_text(GeminiClient *client,
                          const uint8_t *data, size_t len,
                          const char *filename)
{
    /* 1. Base64-encode the file */
    char *b64 = base64_encode(data, len);
    if (!b64) {
        fprintf(stderr, "[gemini] base64 alloc failed\n");
        return NULL;
    }

    /* 2. Build JSON payload with cJSON */
    cJSON *root    = cJSON_CreateObject();
    cJSON *contents = cJSON_AddArrayToObject(root, "contents");
    cJSON *content  = cJSON_CreateObject();
    cJSON_AddItemToArray(contents, content);
    cJSON *parts    = cJSON_AddArrayToObject(content, "parts");

    /* inline_data part */
    cJSON *part_data  = cJSON_CreateObject();
    cJSON *inline_obj = cJSON_CreateObject();
    cJSON_AddStringToObject(inline_obj, "mime_type", storage_mime_type(filename));
    cJSON_AddStringToObject(inline_obj, "data",      b64);
    cJSON_AddItemToObject(part_data, "inline_data", inline_obj);
    cJSON_AddItemToArray(parts, part_data);
    free(b64);

    /* text prompt part */
    cJSON *part_text = cJSON_CreateObject();
    cJSON_AddStringToObject(part_text, "text", ocr_prompt());
    cJSON_AddItemToArray(parts, part_text);

    /* generationConfig */
    cJSON *gen_cfg = cJSON_CreateObject();
    cJSON_AddNumberToObject(gen_cfg, "temperature", 0);
    cJSON_AddItemToObject(root, "generationConfig", gen_cfg);

    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!body) {
        fprintf(stderr, "[gemini] JSON serialization failed\n");
        return NULL;
    }

    /* 3. Build URL */
    char url[512];
    snprintf(url, sizeof(url), "%s/%s:generateContent?key=%s",
             GEMINI_API_BASE, client->model, client->api_key);

    /* 4. HTTP POST via libcurl */
    CURL *curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "[gemini] curl_easy_init failed\n");
        free(body);
        return NULL;
    }

    GrowBuf resp = {0};

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL,            url);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS,     body);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER,     headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,  write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,      &resp);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT,        120L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    free(body);

    if (res != CURLE_OK) {
        fprintf(stderr, "[gemini] curl error: %s\n", curl_easy_strerror(res));
        free(resp.data);
        return NULL;
    }

    if (http_code != 200) {
        fprintf(stderr, "[gemini] HTTP %ld: %.400s\n",
                http_code, resp.data ? resp.data : "(empty)");
        free(resp.data);
        return NULL;
    }

    /* 5. Parse response */
    cJSON *json = cJSON_Parse(resp.data);
    free(resp.data);

    if (!json) {
        fprintf(stderr, "[gemini] failed to parse response JSON\n");
        return NULL;
    }

    /* Check for API-level error */
    cJSON *err_obj = cJSON_GetObjectItem(json, "error");
    if (err_obj) {
        cJSON *msg = cJSON_GetObjectItem(err_obj, "message");
        fprintf(stderr, "[gemini] API error: %s\n",
                msg ? msg->valuestring : "unknown");
        cJSON_Delete(json);
        return NULL;
    }

    /* Navigate: candidates[0].content.parts[0].text */
    cJSON *candidates = cJSON_GetObjectItem(json, "candidates");
    if (!cJSON_IsArray(candidates) || cJSON_GetArraySize(candidates) == 0) {
        fprintf(stderr, "[gemini] no candidates in response\n");
        cJSON_Delete(json);
        return NULL;
    }
    cJSON *cand0   = cJSON_GetArrayItem(candidates, 0);
    cJSON *cont    = cJSON_GetObjectItem(cand0, "content");
    cJSON *parts2  = cJSON_GetObjectItem(cont,  "parts");
    if (!cJSON_IsArray(parts2) || cJSON_GetArraySize(parts2) == 0) {
        fprintf(stderr, "[gemini] no parts in response\n");
        cJSON_Delete(json);
        return NULL;
    }
    cJSON *part0 = cJSON_GetArrayItem(parts2, 0);
    cJSON *text  = cJSON_GetObjectItem(part0, "text");
    if (!cJSON_IsString(text)) {
        fprintf(stderr, "[gemini] no text in response part\n");
        cJSON_Delete(json);
        return NULL;
    }

    char *result = strdup(text->valuestring);
    cJSON_Delete(json);
    return result; /* caller must free() */
}

char *gemini_parse_receipt(GeminiClient *client, const char *ocr_text)
{
    /* 1. Build JSON payload with cJSON */
    cJSON *root    = cJSON_CreateObject();
    cJSON *contents = cJSON_AddArrayToObject(root, "contents");
    cJSON *content  = cJSON_CreateObject();
    cJSON_AddItemToArray(contents, content);
    cJSON *parts    = cJSON_AddArrayToObject(content, "parts");

    /* text prompt part */
    cJSON *part_text = cJSON_CreateObject();
    cJSON_AddStringToObject(part_text, "text", parse_prompt());
    cJSON_AddItemToArray(parts, part_text);

    /* OCR text part */
    cJSON *part_ocr = cJSON_CreateObject();
    cJSON_AddStringToObject(part_ocr, "text", ocr_text);
    cJSON_AddItemToArray(parts, part_ocr);

    /* generationConfig */
    cJSON *gen_cfg = cJSON_CreateObject();
    cJSON_AddNumberToObject(gen_cfg, "temperature", 0);
    cJSON_AddItemToObject(root, "generationConfig", gen_cfg);

    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!body) {
        fprintf(stderr, "[gemini] JSON serialization failed\n");
        return NULL;
    }

    /* 2. Build URL */
    char url[512];
    snprintf(url, sizeof(url), "%s/%s:generateContent?key=%s",
             GEMINI_API_BASE, client->model, client->api_key);

    /* 3. HTTP POST via libcurl */
    CURL *curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "[gemini] curl_easy_init failed\n");
        free(body);
        return NULL;
    }

    GrowBuf resp = {0};

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL,            url);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS,     body);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER,     headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,  write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,      &resp);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT,        120L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    free(body);

    if (res != CURLE_OK) {
        fprintf(stderr, "[gemini] curl error: %s\n", curl_easy_strerror(res));
        free(resp.data);
        return NULL;
    }

    if (http_code != 200) {
        fprintf(stderr, "[gemini] HTTP %ld: %.400s\n",
                http_code, resp.data ? resp.data : "(empty)");
        free(resp.data);
        return NULL;
    }

    /* 4. Parse response */
    cJSON *json = cJSON_Parse(resp.data);
    free(resp.data);

    if (!json) {
        fprintf(stderr, "[gemini] failed to parse response JSON\n");
        return NULL;
    }

    /* Check for API-level error */
    cJSON *err_obj = cJSON_GetObjectItem(json, "error");
    if (err_obj) {
        cJSON *msg = cJSON_GetObjectItem(err_obj, "message");
        fprintf(stderr, "[gemini] API error: %s\n",
                msg ? msg->valuestring : "unknown");
        cJSON_Delete(json);
        return NULL;
    }

    /* Navigate: candidates[0].content.parts[0].text */
    cJSON *candidates = cJSON_GetObjectItem(json, "candidates");
    if (!cJSON_IsArray(candidates) || cJSON_GetArraySize(candidates) == 0) {
        fprintf(stderr, "[gemini] no candidates in response\n");
        cJSON_Delete(json);
        return NULL;
    }
    cJSON *cand0   = cJSON_GetArrayItem(candidates, 0);
    cJSON *cont    = cJSON_GetObjectItem(cand0, "content");
    cJSON *parts2  = cJSON_GetObjectItem(cont,  "parts");
    if (!cJSON_IsArray(parts2) || cJSON_GetArraySize(parts2) == 0) {
        fprintf(stderr, "[gemini] no parts in response\n");
        cJSON_Delete(json);
        return NULL;
    }
    cJSON *part0 = cJSON_GetArrayItem(parts2, 0);
    cJSON *text  = cJSON_GetObjectItem(part0, "text");
    if (!cJSON_IsString(text)) {
        fprintf(stderr, "[gemini] no text in response part\n");
        cJSON_Delete(json);
        return NULL;
    }

    char *result = strdup(text->valuestring);
    cJSON_Delete(json);
    return result; /* caller must free() */
}
