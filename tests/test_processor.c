/* ── Processor tests ───────────────────────────────────────────────────── */

#include "cJSON.h"
#include "config.h"
#include "db_backend.h"
#include "gemini.h"
#include "processor.h"
#include "storage_backend.h"
#include "test_helpers.h"
#include "test_runner.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Test helper: build mock JSON response ─────────────────────────────── */

static cJSON *build_mock_receipt_json(const char *store_name, double total_sum, cJSON *line_items) {
    cJSON *json = cJSON_CreateObject();

    cJSON *store_info = cJSON_CreateObject();
    cJSON_AddStringToObject(store_info, "name", store_name);
    cJSON_AddStringToObject(store_info, "address", "Test Street 123");
    cJSON_AddItemToObject(json, "store_information", store_info);

    cJSON_AddItemToObject(json, "line_items", line_items);
    cJSON_AddNumberToObject(json, "total_sum", total_sum);
    cJSON_AddNumberToObject(json, "number_of_items", cJSON_GetArraySize(line_items));

    cJSON *other = cJSON_CreateObject();
    cJSON_AddStringToObject(other, "date", "2024-01-01");
    cJSON_AddStringToObject(other, "time", "12:00");
    cJSON_AddItemToObject(json, "other", other);

    return json;
}

static cJSON *create_line_item(const char *name, double price, double amount) {
    cJSON *item = cJSON_CreateObject();
    cJSON_AddStringToObject(item, "original_name", name);
    cJSON_AddStringToObject(item, "english_translation", name);
    cJSON_AddStringToObject(item, "category", "grocery");
    cJSON_AddNumberToObject(item, "price", price);
    cJSON_AddNumberToObject(item, "amount", amount);
    return item;
}

/* ── Test cases ────────────────────────────────────────────────────────── */

TEST_CASE(processor_line_items_single_item) {
    cJSON *items = cJSON_CreateArray();
    cJSON_AddItemToArray(items, create_line_item("Milk", 1.50, 2.0));

    cJSON *json = build_mock_receipt_json("Test Store", 3.00, items);
    ASSERT_NOT_NULL(json);

    /* Verify structure */
    cJSON *line_items = cJSON_GetObjectItem(json, "line_items");
    ASSERT_NOT_NULL(line_items);
    ASSERT_EQ(1, cJSON_GetArraySize(line_items));

    cJSON *item0 = cJSON_GetArrayItem(line_items, 0);
    cJSON *name = cJSON_GetObjectItem(item0, "original_name");
    cJSON *price = cJSON_GetObjectItem(item0, "price");
    cJSON *amount = cJSON_GetObjectItem(item0, "amount");

    ASSERT_STR_EQ("Milk", name->valuestring);
    ASSERT_TRUE(cJSON_IsNumber(price));
    ASSERT_TRUE(cJSON_IsNumber(amount));

    cJSON_Delete(json);
    TEST_PASS();
}

TEST_CASE(processor_line_items_multiple_items) {
    cJSON *items = cJSON_CreateArray();
    cJSON_AddItemToArray(items, create_line_item("Bread", 2.00, 1.0));
    cJSON_AddItemToArray(items, create_line_item("Cheese", 5.50, 2.0));
    cJSON_AddItemToArray(items, create_line_item("Eggs", 3.00, 1.0));

    cJSON *json = build_mock_receipt_json("Supermarket", 16.00, items);
    ASSERT_NOT_NULL(json);

    cJSON *line_items = cJSON_GetObjectItem(json, "line_items");
    ASSERT_EQ(3, cJSON_GetArraySize(line_items));

    /* Verify each item */
    cJSON *item;
    double total = 0.0;
    cJSON_ArrayForEach(item, line_items) {
        cJSON *price = cJSON_GetObjectItem(item, "price");
        cJSON *amount = cJSON_GetObjectItem(item, "amount");
        total += price->valuedouble * amount->valuedouble;
    }

    ASSERT_EQ(16.0, total);

    cJSON_Delete(json);
    TEST_PASS();
}

TEST_CASE(processor_total_calculation_matches) {
    /* Create items that sum to exactly 10.00 */
    cJSON *items = cJSON_CreateArray();
    cJSON_AddItemToArray(items, create_line_item("Item A", 3.50, 2.0)); /* 7.00 */
    cJSON_AddItemToArray(items, create_line_item("Item B", 3.00, 1.0)); /* 3.00 */

    cJSON *json = build_mock_receipt_json("Store", 10.00, items);
    ASSERT_NOT_NULL(json);

    cJSON *line_items = cJSON_GetObjectItem(json, "line_items");
    cJSON *total_sum = cJSON_GetObjectItem(json, "total_sum");

    /* Calculate total from line items */
    double calculated = 0.0;
    cJSON *item;
    cJSON_ArrayForEach(item, line_items) {
        cJSON *price = cJSON_GetObjectItem(item, "price");
        cJSON *amount = cJSON_GetObjectItem(item, "amount");
        calculated += price->valuedouble * amount->valuedouble;
    }

    double parsed = total_sum->valuedouble;
    double diff = calculated - parsed;
    if (diff < 0)
        diff = -diff;

    /* Should match exactly */
    ASSERT_TRUE(diff <= 0.01);

    cJSON_Delete(json);
    TEST_PASS();
}

TEST_CASE(processor_total_calculation_mismatch) {
    /* Create items that sum to 10.00 but parsed total is 11.00 */
    cJSON *items = cJSON_CreateArray();
    cJSON_AddItemToArray(items, create_line_item("Item A", 3.50, 2.0)); /* 7.00 */
    cJSON_AddItemToArray(items, create_line_item("Item B", 3.00, 1.0)); /* 3.00 */

    cJSON *json = build_mock_receipt_json("Store", 11.00, items); /* Wrong total */
    ASSERT_NOT_NULL(json);

    cJSON *line_items = cJSON_GetObjectItem(json, "line_items");
    cJSON *total_sum = cJSON_GetObjectItem(json, "total_sum");

    /* Calculate total from line items */
    double calculated = 0.0;
    cJSON *item;
    cJSON_ArrayForEach(item, line_items) {
        cJSON *price = cJSON_GetObjectItem(item, "price");
        cJSON *amount = cJSON_GetObjectItem(item, "amount");
        calculated += price->valuedouble * amount->valuedouble;
    }

    double parsed = total_sum->valuedouble;
    double diff = calculated - parsed;
    if (diff < 0)
        diff = -diff;

    /* Should detect mismatch (> 0.01) */
    ASSERT_TRUE(diff > 0.01);
    ASSERT_EQ(10.0, calculated);
    ASSERT_EQ(11.0, parsed);

    cJSON_Delete(json);
    TEST_PASS();
}

TEST_CASE(processor_total_calculation_zero_items) {
    cJSON *items = cJSON_CreateArray();
    cJSON *json = build_mock_receipt_json("Empty Store", 0.00, items);
    ASSERT_NOT_NULL(json);

    cJSON *line_items = cJSON_GetObjectItem(json, "line_items");
    cJSON *total_sum = cJSON_GetObjectItem(json, "total_sum");

    ASSERT_EQ(0, cJSON_GetArraySize(line_items));
    ASSERT_EQ(0.0, total_sum->valuedouble);

    cJSON_Delete(json);
    TEST_PASS();
}

TEST_CASE(processor_total_calculation_missing_fields) {
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "store_information", "Test");

    /* Missing line_items and total_sum */
    cJSON *line_items = cJSON_GetObjectItem(json, "line_items");
    cJSON *total_sum = cJSON_GetObjectItem(json, "total_sum");

    ASSERT_TRUE(line_items == NULL);
    ASSERT_TRUE(total_sum == NULL);

    cJSON_Delete(json);
    TEST_PASS();
}

TEST_CASE(processor_total_with_fractional_amounts) {
    /* Test with fractional amounts */
    cJSON *items = cJSON_CreateArray();
    cJSON_AddItemToArray(items, create_line_item("Fabric", 12.50, 0.5)); /* 6.25 */
    cJSON_AddItemToArray(items, create_line_item("Ribbon", 2.00, 2.5));  /* 5.00 */

    cJSON *json = build_mock_receipt_json("Craft Store", 11.25, items);
    ASSERT_NOT_NULL(json);

    cJSON *line_items = cJSON_GetObjectItem(json, "line_items");

    double calculated = 0.0;
    cJSON *item;
    cJSON_ArrayForEach(item, line_items) {
        cJSON *price = cJSON_GetObjectItem(item, "price");
        cJSON *amount = cJSON_GetObjectItem(item, "amount");
        calculated += price->valuedouble * amount->valuedouble;
    }

    /* Allow small floating point tolerance */
    double diff = calculated - 11.25;
    if (diff < 0)
        diff = -diff;
    ASSERT_TRUE(diff < 0.01);

    cJSON_Delete(json);
    TEST_PASS();
}

TEST_CASE(processor_total_boundary_one_cent) {
    /* Test boundary: exactly 1 cent difference should NOT trigger warning */
    cJSON *items = cJSON_CreateArray();
    cJSON_AddItemToArray(items, create_line_item("Item", 1.00, 1.0));

    cJSON *json = build_mock_receipt_json("Store", 1.01, items);
    ASSERT_NOT_NULL(json);

    cJSON *line_items = cJSON_GetObjectItem(json, "line_items");
    cJSON *total_sum = cJSON_GetObjectItem(json, "total_sum");

    double calculated = 0.0;
    cJSON *item;
    cJSON_ArrayForEach(item, line_items) {
        cJSON *price = cJSON_GetObjectItem(item, "price");
        cJSON *amount = cJSON_GetObjectItem(item, "amount");
        calculated += price->valuedouble * amount->valuedouble;
    }

    double parsed = total_sum->valuedouble;
    double diff = calculated - parsed;
    if (diff < 0)
        diff = -diff;

    /* Exactly 1 cent - should be at boundary (warning threshold is > 0.015) */
    ASSERT_TRUE(diff <= 0.015);

    cJSON_Delete(json);
    TEST_PASS();
}

TEST_CASE(processor_total_boundary_over_one_cent) {
    /* Test boundary: over 1.5 cents difference SHOULD trigger warning */
    cJSON *items = cJSON_CreateArray();
    cJSON_AddItemToArray(items, create_line_item("Item", 1.00, 1.0));

    cJSON *json = build_mock_receipt_json("Store", 1.02, items);
    ASSERT_NOT_NULL(json);

    cJSON *line_items = cJSON_GetObjectItem(json, "line_items");
    cJSON *total_sum = cJSON_GetObjectItem(json, "total_sum");

    double calculated = 0.0;
    cJSON *item;
    cJSON_ArrayForEach(item, line_items) {
        cJSON *price = cJSON_GetObjectItem(item, "price");
        cJSON *amount = cJSON_GetObjectItem(item, "amount");
        calculated += price->valuedouble * amount->valuedouble;
    }

    double parsed = total_sum->valuedouble;
    double diff = calculated - parsed;
    if (diff < 0)
        diff = -diff;

    /* Over 1.5 cents - should trigger warning */
    ASSERT_TRUE(diff > 0.015);

    cJSON_Delete(json);
    TEST_PASS();
}

/* ── Test cases: reply building ────────────────────────────────────────── */

#ifdef TEST_BUILD

TEST_CASE(processor_reply_single_item) {
    char reply[4096];
    cJSON *items = cJSON_CreateArray();
    cJSON_AddItemToArray(items, create_line_item("Milk", 1.50, 2.0));

    cJSON *json = build_mock_receipt_json("Test Store", 3.00, items);

    processor_build_reply_ok(reply, sizeof(reply), "saved.jpg", "ocr.txt", "original.jpg", 1024,
                             json, 0);

    ASSERT_NOT_NULL(reply);
    ASSERT_TRUE(strstr(reply, "Test Store") != NULL);
    ASSERT_TRUE(strstr(reply, "Milk") != NULL);
    ASSERT_TRUE(strstr(reply, "1.50 EUR") != NULL);
    ASSERT_TRUE(strstr(reply, "3.00 EUR") != NULL);
    /* No warning expected - totals match */
    ASSERT_TRUE(strstr(reply, "calc:") == NULL);

    cJSON_Delete(json);
    TEST_PASS();
}

TEST_CASE(processor_reply_multiple_items) {
    char reply[4096];
    cJSON *items = cJSON_CreateArray();
    cJSON_AddItemToArray(items, create_line_item("Bread", 2.00, 1.0));
    cJSON_AddItemToArray(items, create_line_item("Cheese", 5.00, 2.0));
    cJSON_AddItemToArray(items, create_line_item("Wine", 8.00, 1.0));

    cJSON *json = build_mock_receipt_json("Supermarket", 20.00, items);

    processor_build_reply_ok(reply, sizeof(reply), "receipt.jpg", "ocr.txt", "img.jpg", 2048, json,
                             0);

    ASSERT_TRUE(strstr(reply, "Supermarket") != NULL);
    ASSERT_TRUE(strstr(reply, "Bread") != NULL);
    ASSERT_TRUE(strstr(reply, "Cheese") != NULL);
    ASSERT_TRUE(strstr(reply, "Wine") != NULL);
    ASSERT_TRUE(strstr(reply, "20.00 EUR") != NULL);
    /* No warning - totals match */
    ASSERT_TRUE(strstr(reply, "calc:") == NULL);

    cJSON_Delete(json);
    TEST_PASS();
}

TEST_CASE(processor_reply_total_mismatch_warning) {
    char reply[4096];
    cJSON *items = cJSON_CreateArray();
    cJSON_AddItemToArray(items, create_line_item("Item", 10.00, 1.0));

    /* Parsed total is wrong (11.00 instead of 10.00) */
    cJSON *json = build_mock_receipt_json("Store", 11.00, items);

    processor_build_reply_ok(reply, sizeof(reply), "r.jpg", "o.txt", "i.jpg", 512, json, 0);

    /* Should contain warning */
    ASSERT_TRUE(strstr(reply, "calc:") != NULL);
    ASSERT_TRUE(strstr(reply, "10.00 EUR") != NULL);
    ASSERT_TRUE(strstr(reply, "11.00 EUR") != NULL);

    cJSON_Delete(json);
    TEST_PASS();
}

TEST_CASE(processor_reply_no_warning_at_one_cent) {
    char reply[4096];
    cJSON *items = cJSON_CreateArray();
    cJSON_AddItemToArray(items, create_line_item("Item", 1.00, 1.0));

    /* Exactly 1 cent difference - should NOT warn (threshold is > 1.5 cents) */
    cJSON *json = build_mock_receipt_json("Store", 1.01, items);

    processor_build_reply_ok(reply, sizeof(reply), "r.jpg", "o.txt", "i.jpg", 100, json, 0);

    /* Should NOT contain warning (boundary is > 0.015 for FP tolerance) */
    ASSERT_TRUE(strstr(reply, "calc:") == NULL);

    cJSON_Delete(json);
    TEST_PASS();
}

TEST_CASE(processor_reply_warning_over_one_cent) {
    char reply[4096];
    cJSON *items = cJSON_CreateArray();
    cJSON_AddItemToArray(items, create_line_item("Item", 1.00, 1.0));

    /* Over 1.5 cents difference - SHOULD warn */
    cJSON *json = build_mock_receipt_json("Store", 1.02, items);

    processor_build_reply_ok(reply, sizeof(reply), "r.jpg", "o.txt", "i.jpg", 100, json, 0);

    ASSERT_TRUE(strstr(reply, "calc:") != NULL);

    cJSON_Delete(json);
    TEST_PASS();
}

TEST_CASE(processor_reply_zero_items) {
    char reply[4096];
    cJSON *items = cJSON_CreateArray();
    cJSON *json = build_mock_receipt_json("Empty Store", 0.00, items);

    processor_build_reply_ok(reply, sizeof(reply), "empty.jpg", "empty_ocr.txt", "e.jpg", 0, json,
                             0);

    ASSERT_TRUE(strstr(reply, "Empty Store") != NULL);
    ASSERT_TRUE(strstr(reply, "0.00 EUR") != NULL);

    cJSON_Delete(json);
    TEST_PASS();
}

TEST_CASE(processor_reply_missing_store_name) {
    char reply[4096];
    cJSON *items = cJSON_CreateArray();
    cJSON *json = cJSON_CreateObject();
    cJSON_AddItemToObject(json, "line_items", items);
    cJSON_AddNumberToObject(json, "total_sum", 5.00);

    processor_build_reply_ok(reply, sizeof(reply), "x.jpg", "x.txt", "x.jpg", 100, json, 0);

    /* Should use "Unknown" for missing store name */
    ASSERT_TRUE(strstr(reply, "Unknown") != NULL);

    cJSON_Delete(json);
    TEST_PASS();
}

TEST_CASE(processor_reply_missing_line_items) {
    char reply[4096];
    cJSON *json = cJSON_CreateObject();
    cJSON *store_info = cJSON_CreateObject();
    cJSON_AddStringToObject(store_info, "name", "Store");
    cJSON_AddItemToObject(json, "store_information", store_info);
    cJSON_AddNumberToObject(json, "total_sum", 10.00);
    /* No line_items */

    processor_build_reply_ok(reply, sizeof(reply), "x.jpg", "x.txt", "x.jpg", 100, json, 0);

    ASSERT_TRUE(strstr(reply, "Store") != NULL);
    ASSERT_TRUE(strstr(reply, "0.00 EUR") != NULL);
    /* Warning expected since 0.00 != 10.00 */
    ASSERT_TRUE(strstr(reply, "calc:") != NULL);

    cJSON_Delete(json);
    TEST_PASS();
}

TEST_CASE(processor_reply_fractional_quantities) {
    char reply[4096];
    cJSON *items = cJSON_CreateArray();
    cJSON_AddItemToArray(items, create_line_item("Fabric", 15.00, 0.5)); /* 7.50 */
    cJSON_AddItemToArray(items, create_line_item("Thread", 2.00, 3.5));  /* 7.00 */

    cJSON *json = build_mock_receipt_json("Craft Store", 14.50, items);

    processor_build_reply_ok(reply, sizeof(reply), "craft.jpg", "craft.txt", "c.jpg", 500, json, 0);

    ASSERT_TRUE(strstr(reply, "Fabric") != NULL);
    ASSERT_TRUE(strstr(reply, "Thread") != NULL);
    ASSERT_TRUE(strstr(reply, "14.50 EUR") != NULL);
    /* No warning - totals match */
    ASSERT_TRUE(strstr(reply, "calc:") == NULL);

    cJSON_Delete(json);
    TEST_PASS();
}

#endif /* TEST_BUILD */
