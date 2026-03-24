/* ── JSON/Parsing tests ───────────────────────────────────────────────────── */

#include "../third_party/cjson/cJSON.h"
#include "gemini.h"
#include "test_runner.h"
#include <stdlib.h>
#include <string.h>

/* Test valid receipt JSON structure */
TEST_CASE(json_valid_receipt_structure) {
    const char *json_str =
        "{"
        "  \"store_information\": {\"name\": \"REWE\", \"address\": \"Berlin Str. 1\"},"
        "  \"line_items\": [{\"original_name\": \"Milk\", \"price\": 1.99, \"amount\": 2}],"
        "  \"total_sum\": 50.00,"
        "  \"number_of_items\": 5,"
        "  \"other\": {\"date\": \"2024-01-01\", \"time\": \"12:00\"}"
        "}";

    cJSON *json = cJSON_Parse(json_str);
    ASSERT_NOT_NULL(json);

    /* Verify structure */
    cJSON *store = cJSON_GetObjectItem(json, "store_information");
    ASSERT_NOT_NULL(store);

    cJSON *name = cJSON_GetObjectItem(store, "name");
    ASSERT_NOT_NULL(name);
    ASSERT_STR_EQ("REWE", name->valuestring);

    cJSON *items = cJSON_GetObjectItem(json, "line_items");
    ASSERT_NOT_NULL(items);
    ASSERT_TRUE(cJSON_IsArray(items));
    ASSERT_EQ(1, cJSON_GetArraySize(items));

    cJSON *total = cJSON_GetObjectItem(json, "total_sum");
    ASSERT_NOT_NULL(total);
    ASSERT_TRUE(cJSON_IsNumber(total));

    cJSON_Delete(json);
    TEST_PASS();
}

TEST_CASE(json_parse_line_items) {
    const char *json_str =
        "{"
        "  \"line_items\": ["
        "    {\"id\": \"1\", \"original_name\": \"Brot\", \"price\": 2.50, \"amount\": 1},"
        "    {\"id\": \"2\", \"original_name\": \"Käse\", \"price\": 4.99, \"amount\": 2}"
        "  ]"
        "}";

    cJSON *json = cJSON_Parse(json_str);
    ASSERT_NOT_NULL(json);

    cJSON *items = cJSON_GetObjectItem(json, "line_items");
    ASSERT_EQ(2, cJSON_GetArraySize(items));

    cJSON *item0 = cJSON_GetArrayItem(items, 0);
    cJSON *name0 = cJSON_GetObjectItem(item0, "original_name");
    ASSERT_STR_EQ("Brot", name0->valuestring);

    cJSON *price0 = cJSON_GetObjectItem(item0, "price");
    ASSERT_TRUE(cJSON_IsNumber(price0));

    cJSON_Delete(json);
    TEST_PASS();
}

TEST_CASE(json_parse_store_info) {
    const char *json_str = "{"
                           "  \"store_information\": {"
                           "    \"name\": \"Edeka Müller\","
                           "    \"address\": \"Hauptstraße 42, 10115 Berlin\""
                           "  }"
                           "}";

    cJSON *json = cJSON_Parse(json_str);
    ASSERT_NOT_NULL(json);

    cJSON *store = cJSON_GetObjectItem(json, "store_information");
    cJSON *name = cJSON_GetObjectItem(store, "name");
    cJSON *address = cJSON_GetObjectItem(store, "address");

    ASSERT_STR_EQ("Edeka Müller", name->valuestring);
    ASSERT_STR_EQ("Hauptstraße 42, 10115 Berlin", address->valuestring);

    cJSON_Delete(json);
    TEST_PASS();
}

TEST_CASE(json_parse_other_fields) {
    const char *json_str = "{"
                           "  \"other\": {"
                           "    \"date\": \"2024-03-21\","
                           "    \"time\": \"14:30:00\","
                           "    \"receipt_number\": \"12345\","
                           "    \"payment_method\": \"EC-Karte\""
                           "  }"
                           "}";

    cJSON *json = cJSON_Parse(json_str);
    ASSERT_NOT_NULL(json);

    cJSON *other = cJSON_GetObjectItem(json, "other");
    ASSERT_NOT_NULL(other);

    cJSON *date = cJSON_GetObjectItem(other, "date");
    cJSON *time = cJSON_GetObjectItem(other, "time");
    cJSON *receipt = cJSON_GetObjectItem(other, "receipt_number");
    cJSON *payment = cJSON_GetObjectItem(other, "payment_method");

    ASSERT_STR_EQ("2024-03-21", date->valuestring);
    ASSERT_STR_EQ("14:30:00", time->valuestring);
    ASSERT_STR_EQ("12345", receipt->valuestring);
    ASSERT_STR_EQ("EC-Karte", payment->valuestring);

    cJSON_Delete(json);
    TEST_PASS();
}

TEST_CASE(json_create_receipt) {
    cJSON *root = cJSON_CreateObject();
    ASSERT_NOT_NULL(root);

    /* Add store information */
    cJSON *store = cJSON_AddObjectToObject(root, "store_information");
    cJSON_AddStringToObject(store, "name", "Test Store");
    cJSON_AddStringToObject(store, "address", "Test Address");

    /* Add line items */
    cJSON *items = cJSON_AddArrayToObject(root, "line_items");
    cJSON *item = cJSON_CreateObject();
    cJSON_AddStringToObject(item, "original_name", "Test Item");
    cJSON_AddNumberToObject(item, "price", 9.99);
    cJSON_AddNumberToObject(item, "amount", 1);
    cJSON_AddItemToArray(items, item);

    /* Add totals */
    cJSON_AddNumberToObject(root, "total_sum", 9.99);
    cJSON_AddNumberToObject(root, "number_of_items", 1);

    /* Verify structure */
    cJSON *verify_store = cJSON_GetObjectItem(root, "store_information");
    ASSERT_NOT_NULL(verify_store);
    ASSERT_STR_EQ("Test Store", cJSON_GetObjectItem(verify_store, "name")->valuestring);

    cJSON *verify_items = cJSON_GetObjectItem(root, "line_items");
    ASSERT_EQ(1, cJSON_GetArraySize(verify_items));

    cJSON_Delete(root);
    TEST_PASS();
}

TEST_CASE(json_null_fields) {
    const char *json_str = "{"
                           "  \"store_information\": {\"name\": null, \"address\": \"Address\"},"
                           "  \"total_sum\": 0,"
                           "  \"line_items\": []"
                           "}";

    cJSON *json = cJSON_Parse(json_str);
    ASSERT_NOT_NULL(json);

    cJSON *store = cJSON_GetObjectItem(json, "store_information");
    cJSON *name = cJSON_GetObjectItem(store, "name");

    ASSERT_TRUE(cJSON_IsNull(name));

    cJSON_Delete(json);
    TEST_PASS();
}

TEST_CASE(json_invalid_returns_null) {
    const char *invalid_json = "{ invalid json }";
    cJSON *json = cJSON_Parse(invalid_json);
    ASSERT_TRUE(json == NULL);
    TEST_PASS();
}

TEST_CASE(json_markdown_wrapped_fails_parse) {
    const char *markdown_json = "```json\n{\"total\": 10.00}\n```";
    cJSON *json = cJSON_Parse(markdown_json);
    ASSERT_TRUE(json == NULL); /* cJSON rejects markdown fences */
    TEST_PASS();
}

TEST_CASE(json_markdown_fails_then_stripped_ok) {
    const char *markdown_json = "```json\n{\"total\": 10.00}\n```";

    /* Verify raw markdown fails */
    cJSON *raw = cJSON_Parse(markdown_json);
    ASSERT_TRUE(raw == NULL);

    /* Simulate stripping: skip past ```json\n and up to ``` */
    char buf[256];
    strncpy(buf, markdown_json, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *start = buf;
    if (strncmp(start, "```json", 7) == 0) {
        start += 7;
        while (*start == '\n' || *start == '\r')
            start++;
    }
    char *end = strstr(start, "```");
    if (end)
        *end = '\0';

    /* Stripped version should parse */
    cJSON *json = cJSON_Parse(start);
    ASSERT_NOT_NULL(json);
    cJSON_Delete(json);
    TEST_PASS();
}

TEST_CASE(json_extract_value) {
    const char *json_str = "{\"total_sum\": 123.45}";
    cJSON *json = cJSON_Parse(json_str);
    ASSERT_NOT_NULL(json);

    cJSON *total = cJSON_GetObjectItem(json, "total_sum");
    ASSERT_TRUE(cJSON_IsNumber(total));
    ASSERT_TRUE(total->valuedouble > 123.4 && total->valuedouble < 123.5);

    cJSON_Delete(json);
    TEST_PASS();
}

/* ── Mocked Gemini API response tests ────────────────────────────────────── */

/* Helper: build a Gemini API response JSON with given text content */
static char *mock_gemini_response(const char *text) {
    /* {"candidates":[{"content":{"parts":[{"text":"..."}]}}]} */
    cJSON *root = cJSON_CreateObject();
    cJSON *candidates = cJSON_AddArrayToObject(root, "candidates");
    cJSON *cand = cJSON_CreateObject();
    cJSON *content = cJSON_CreateObject();
    cJSON *parts = cJSON_AddArrayToObject(content, "parts");
    cJSON *part = cJSON_CreateObject();
    cJSON_AddStringToObject(part, "text", text);
    cJSON_AddItemToArray(parts, part);
    cJSON_AddItemToObject(cand, "content", content);
    cJSON_AddItemToArray(candidates, cand);
    char *result = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return result;
}

/* Helper: build a Gemini API error response */
static char *mock_gemini_error(const char *message) {
    cJSON *root = cJSON_CreateObject();
    cJSON *error = cJSON_AddObjectToObject(root, "error");
    cJSON_AddStringToObject(error, "message", message);
    char *result = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return result;
}

/* Happy: plain JSON text */
TEST_CASE(gemini_mock_happy_plain_json) {
    const char *receipt = "{\"store\":\"Kaufland\",\"total\":15.99}";
    char *api_resp = mock_gemini_response(receipt);

    char *result = gemini_parse_api_response(api_resp);
    free(api_resp);

    ASSERT_NOT_NULL(result);
    ASSERT_STR_EQ(receipt, result);
    free(result);
    TEST_PASS();
}

/* Happy: markdown-wrapped JSON */
TEST_CASE(gemini_mock_happy_markdown_wrapped) {
    const char *receipt = "{\"store\":\"REWE\",\"total\":8.50}";
    char wrapped[512];
    snprintf(wrapped, sizeof(wrapped), "```json\n%s\n```", receipt);
    char *api_resp = mock_gemini_response(wrapped);

    char *raw = gemini_parse_api_response(api_resp);
    free(api_resp);
    ASSERT_NOT_NULL(raw);

    char *stripped = strip_markdown_json(raw);
    ASSERT_STR_EQ(receipt, stripped);
    free(raw);
    TEST_PASS();
}

/* Happy: markdown with extra whitespace */
TEST_CASE(gemini_mock_happy_markdown_whitespace) {
    const char *receipt = "{\"store\":\"Edeka\",\"total\":22.00}";
    char wrapped[512];
    snprintf(wrapped, sizeof(wrapped), "```json\n  \n%s\n  \n```", receipt);
    char *api_resp = mock_gemini_response(wrapped);

    char *raw = gemini_parse_api_response(api_resp);
    free(api_resp);
    ASSERT_NOT_NULL(raw);

    char *stripped = strip_markdown_json(raw);
    cJSON *json = cJSON_Parse(stripped);
    ASSERT_NOT_NULL(json);
    cJSON_Delete(json);
    free(raw);
    TEST_PASS();
}

/* Sad: API error response */
TEST_CASE(gemini_mock_api_error) {
    char *api_resp = mock_gemini_error("Invalid API key");

    char *result = gemini_parse_api_response(api_resp);
    free(api_resp);

    ASSERT_TRUE(result == NULL);
    TEST_PASS();
}

/* Sad: empty candidates array */
TEST_CASE(gemini_mock_no_candidates) {
    char *api_resp = strdup("{\"candidates\":[]}");

    char *result = gemini_parse_api_response(api_resp);
    free(api_resp);

    ASSERT_TRUE(result == NULL);
    TEST_PASS();
}

/* Sad: missing content/parts */
TEST_CASE(gemini_mock_missing_parts) {
    char *api_resp = strdup("{\"candidates\":[{\"content\":{}}]}");

    char *result = gemini_parse_api_response(api_resp);
    free(api_resp);

    ASSERT_TRUE(result == NULL);
    TEST_PASS();
}

/* Sad: text is not a string */
TEST_CASE(gemini_mock_text_not_string) {
    char *api_resp = strdup("{\"candidates\":[{\"content\":{\"parts\":[{\"text\":123}]}}]}");

    char *result = gemini_parse_api_response(api_resp);
    free(api_resp);

    ASSERT_TRUE(result == NULL);
    TEST_PASS();
}

/* Sad: completely invalid JSON */
TEST_CASE(gemini_mock_invalid_json) {
    char *api_resp = strdup("not json at all");

    char *result = gemini_parse_api_response(api_resp);
    free(api_resp);

    ASSERT_TRUE(result == NULL);
    TEST_PASS();
}

/* Sad: empty string */
TEST_CASE(gemini_mock_empty_string) {
    char *api_resp = strdup("");

    char *result = gemini_parse_api_response(api_resp);
    free(api_resp);

    ASSERT_TRUE(result == NULL);
    TEST_PASS();
}
