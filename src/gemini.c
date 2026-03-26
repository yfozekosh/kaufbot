#include "gemini.h"
#include "cJSON.h"
#include "config.h"
#include "storage.h"
#include "utils.h"

#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define GEMINI_API_BASE                  "https://generativelanguage.googleapis.com/v1beta/models"
#define GEMINI_MAX_API_KEY_LEN           256
#define GEMINI_MAX_MODEL_LEN             128
#define GEMINI_URL_BUF_LEN               512
#define GEMINI_HTTP_TIMEOUT_SECS         600L
#define GEMINI_HTTP_CONNECT_TIMEOUT_SECS 15L
#define GEMINI_FALLBACK_MODEL            "gemma-3-27b-it"

struct GeminiClient {
    char api_key[GEMINI_MAX_API_KEY_LEN];
    char model[GEMINI_MAX_MODEL_LEN];
    char fallback_model[GEMINI_MAX_MODEL_LEN];
    time_t fallback_until;
};

char *strip_markdown_json(char *raw) {
    char *start = raw;
    if (strncmp(start, "```json", 7) == 0) {
        start += 7;
        while (*start == '\n' || *start == '\r')
            start++;
    } else if (strncmp(start, "```", 3) == 0) {
        start += 3;
        while (*start == '\n' || *start == '\r')
            start++;
    }
    char *end = strstr(start, "```");
    if (end)
        *end = '\0';

    while (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r')
        start++;

    size_t len = strlen(start);
    while (len > 0 && (start[len - 1] == ' ' || start[len - 1] == '\t' || start[len - 1] == '\n' ||
                       start[len - 1] == '\r')) {
        start[--len] = '\0';
    }
    return start;
}

static cJSON *gemini_check_error(cJSON *json) {
    cJSON *err_obj = cJSON_GetObjectItem(json, "error");
    if (err_obj) {
        cJSON *msg = cJSON_GetObjectItem(err_obj, "message");
        LOG_ERROR("API error: %s", (msg && cJSON_IsString(msg)) ? msg->valuestring : "unknown");
        return NULL;
    }

    cJSON *candidates = cJSON_GetObjectItem(json, "candidates");
    if (!cJSON_IsArray(candidates) || cJSON_GetArraySize(candidates) == 0) {
        LOG_ERROR("no candidates in response");
        return NULL;
    }

    cJSON *cand0 = cJSON_GetArrayItem(candidates, 0);
    if (!cand0) {
        LOG_ERROR("candidates[0] is NULL");
        return NULL;
    }

    cJSON *cont = cJSON_GetObjectItem(cand0, "content");
    if (!cont) {
        LOG_ERROR("content missing in candidate");
        return NULL;
    }

    cJSON *parts = cJSON_GetObjectItem(cont, "parts");
    if (!cJSON_IsArray(parts) || cJSON_GetArraySize(parts) == 0) {
        LOG_ERROR("no parts in response");
        return NULL;
    }

    cJSON *part0 = cJSON_GetArrayItem(parts, 0);
    if (!part0) {
        LOG_ERROR("parts[0] is NULL");
        return NULL;
    }

    cJSON *text = cJSON_GetObjectItem(part0, "text");
    if (!cJSON_IsString(text)) {
        LOG_ERROR("no text in response part");
        return NULL;
    }
    return text;
}

char *gemini_parse_api_response(const char *api_json) {
    if (!api_json || api_json[0] == '\0')
        return NULL;

    cJSON *json = cJSON_Parse(api_json);
    if (!json) {
        LOG_ERROR("failed to parse response JSON");
        return NULL;
    }

    cJSON *text = gemini_check_error(json);
    char *result = text ? strdup(text->valuestring) : NULL;
    cJSON_Delete(json);
    return result;
}

/* ── OCR prompt ───────────────────────────────────────────────────────────── */

static const char *ocr_prompt(void) {
    return "Extract ALL text from this image or document. This document is a receipt from one of "
           "the well-know german supermarkets."
           "Preserve the original layout and formatting as closely as possible. "
           "Items on the same line in the image/pdf should stay on the same line in the output "
           "text."
           "If the line starts with the blank text - use spaces to align text with the text "
           "position on the image/document."
           "If there is no text, respond with: [NO TEXT FOUND]. "
           "Output only the extracted text, nothing else.";
}

static const char *parse_prompt(void) {
    return "You are an expert German supermarket receipt parsing tool. You are given text "
           "extracted from a receipt photo via OCR. The layout is important, as item "
           "descriptions often span multiple lines.\n\n"
           "Your task is to extract the data and return it strictly as a JSON document "
           "matching the schema below. IMPORTANT: Return ONLY the raw JSON. Do NOT wrap it "
           "in markdown code blocks (```json). Do NOT include any conversational text, "
           "explanations, or formatting before or after the JSON.\n\n"
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

GeminiClient *gemini_new(const char *api_key, const char *model) {
    if (!api_key || !model)
        return NULL;

    GeminiClient *c = calloc(1, sizeof(GeminiClient));
    if (!c)
        return NULL;
    snprintf(c->api_key, GEMINI_MAX_API_KEY_LEN, "%s", api_key);
    snprintf(c->model, GEMINI_MAX_MODEL_LEN, "%s", model);
    snprintf(c->fallback_model, GEMINI_MAX_MODEL_LEN, "%s", GEMINI_FALLBACK_MODEL);
    c->fallback_until = 0;
    return c;
}

void gemini_free(GeminiClient *client) {
    free(client);
}

/* Return next midnight as time_t */
static time_t next_midnight(void) {
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    tm.tm_hour = 0;
    tm.tm_min = 0;
    tm.tm_sec = 0;
    tm.tm_mday += 1; /* next day */
    return mktime(&tm);
}

/* Shared helper to POST a JSON body to Gemini and return the parsed text. */
static char *gemini_post_and_parse(GeminiClient *client, const char *body) {
    /* Check if fallback period has expired */
    if (client->fallback_until != 0 && time(NULL) >= client->fallback_until) {
        LOG_INFO("gemini fallback period expired, reverting to primary model: %s", client->model);
        client->fallback_until = 0;
    }

    const char *active_model =
        (client->fallback_until != 0) ? client->fallback_model : client->model;

    char url[GEMINI_URL_BUF_LEN];
    snprintf(url, sizeof(url), "%s/%s:generateContent?key=%s", GEMINI_API_BASE, active_model,
             client->api_key);
    LOG_DEBUG("sending request to Gemini API (model: %s)", active_model);

    CURL *curl = curl_easy_init();
    if (!curl) {
        LOG_ERROR("curl_easy_init failed");
        return NULL;
    }

    GrowBuf resp = {0};

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    if (!headers) {
        curl_easy_cleanup(curl);
        return NULL;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, growbuf_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, GEMINI_HTTP_TIMEOUT_SECS);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, GEMINI_HTTP_CONNECT_TIMEOUT_SECS);

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        LOG_ERROR("curl error: %s", curl_easy_strerror(res));
        free(resp.data);
        return NULL;
    }

    /* Handle 429 rate limit - switch to fallback model */
    if (http_code == 429 && client->fallback_until == 0) {
        LOG_WARN("gemini rate limited (429), switching to fallback model: %s",
                 client->fallback_model);
        client->fallback_until = next_midnight();
        free(resp.data);

        /* Retry with fallback model */
        return gemini_post_and_parse(client, body);
    }

    if (http_code != 200) {
        LOG_ERROR("HTTP %ld: %.400s", http_code, resp.data ? resp.data : "(empty)");
        free(resp.data);
        return NULL;
    }

    char *result = gemini_parse_api_response(resp.data);
    free(resp.data);
    return result;
}

static char *gemini_build_body_and_post(GeminiClient *client, cJSON *parts, const char *prompt,
                                        const char *extra_text) {
    cJSON *root = cJSON_CreateObject();
    cJSON *contents = cJSON_AddArrayToObject(root, "contents");
    cJSON *content = cJSON_CreateObject();
    cJSON_AddItemToArray(contents, content);
    cJSON *p = cJSON_AddArrayToObject(content, "parts");

    /* Copy caller-provided parts */
    if (parts) {
        cJSON *part = NULL;
        cJSON_ArrayForEach(part, parts) {
            cJSON_AddItemToArray(p, cJSON_Duplicate(part, 1));
        }
    }

    /* Prompt text */
    cJSON *prompt_part = cJSON_CreateObject();
    cJSON_AddStringToObject(prompt_part, "text", prompt);
    cJSON_AddItemToArray(p, prompt_part);

    /* Optional extra text (e.g., OCR text for parsing) */
    if (extra_text) {
        cJSON *extra = cJSON_CreateObject();
        cJSON_AddStringToObject(extra, "text", extra_text);
        cJSON_AddItemToArray(p, extra);
    }

    cJSON *gen_cfg = cJSON_CreateObject();
    cJSON_AddNumberToObject(gen_cfg, "temperature", 0);
    cJSON_AddItemToObject(root, "generationConfig", gen_cfg);

    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    cJSON_Delete(parts);
    if (!body) {
        LOG_ERROR("JSON serialization failed");
        return NULL;
    }

    char *result = gemini_post_and_parse(client, body);
    free(body);
    return result;
}

char *gemini_extract_text(GeminiClient *client, const uint8_t *data, size_t len,
                          const char *filename) {
    LOG_INFO("extracting text from: %s (%zu bytes)", filename, len);

    char *b64 = base64_encode(data, len);
    if (!b64) {
        LOG_ERROR("base64 alloc failed");
        return NULL;
    }

    cJSON *parts = cJSON_CreateArray();
    cJSON *part_data = cJSON_CreateObject();
    cJSON *inline_obj = cJSON_CreateObject();
    cJSON_AddStringToObject(inline_obj, "mime_type", storage_mime_type(filename));
    cJSON_AddStringToObject(inline_obj, "data", b64);
    cJSON_AddItemToObject(part_data, "inline_data", inline_obj);
    cJSON_AddItemToArray(parts, part_data);
    free(b64);

    char *result = gemini_build_body_and_post(client, parts, ocr_prompt(), NULL);
    if (result)
        LOG_INFO("text extraction successful");
    return result;
}

char *gemini_parse_receipt(GeminiClient *client, const char *ocr_text) {
    LOG_INFO("parsing receipt text (%zu chars)", strlen(ocr_text));

    char *raw = gemini_build_body_and_post(client, NULL, parse_prompt(), ocr_text);
    if (!raw)
        return NULL;

    char *start = strip_markdown_json(raw);
    char *result = strdup(start);
    free(raw);
    LOG_INFO("receipt parsing successful");
    return result;
}
