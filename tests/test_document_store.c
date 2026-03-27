/* ── Document Store tests ────────────────────────────────────────────────── */

#include "document_store.h"
#include "storage_backend.h"
#include "test_helpers.h"
#include "test_runner.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_DOC_STORAGE_PATH "/tmp/kaufbot_test_doc_store"

/* ── Helper ───────────────────────────────────────────────────────────────── */

static StorageBackend *open_test_storage(void) {
    test_rmrf(TEST_DOC_STORAGE_PATH);
    StorageBackend *storage = test_storage_open_local(TEST_DOC_STORAGE_PATH);
    if (storage) {
        storage_backend_ensure_dirs(storage);
    }
    return storage;
}

/* ── Lifecycle Tests ──────────────────────────────────────────────────────── */

TEST_CASE(doc_store_create_and_free) {
    StorageBackend *storage = open_test_storage();
    ASSERT_NOT_NULL(storage);

    DocumentStore *store = document_store_new(storage);
    ASSERT_NOT_NULL(store);

    document_store_free(store);
    document_store_free(NULL);
    storage_backend_close(storage);
    test_rmrf(TEST_DOC_STORAGE_PATH);
    TEST_PASS();
}

TEST_CASE(doc_store_create_null) {
    DocumentStore *store = document_store_new(NULL);
    ASSERT_TRUE(store == NULL);
    TEST_PASS();
}

/* ── Store Document Tests ─────────────────────────────────────────────────── */

TEST_CASE(doc_store_store_document) {
    StorageBackend *storage = open_test_storage();
    ASSERT_NOT_NULL(storage);

    DocumentStore *store = document_store_new(storage);
    ASSERT_NOT_NULL(store);

    const uint8_t data[] = "test document content";
    char doc_id[64] = {0};

    int rc = doc_store_store(store, "test.txt", data, sizeof(data), doc_id, sizeof(doc_id));
    ASSERT_EQ(DOC_STORE_OK, rc);
    ASSERT_TRUE(strlen(doc_id) > 0);

    document_store_free(store);
    storage_backend_close(storage);
    test_rmrf(TEST_DOC_STORAGE_PATH);
    TEST_PASS();
}

TEST_CASE(doc_store_store_document_null_args) {
    StorageBackend *storage = open_test_storage();
    DocumentStore *store = document_store_new(storage);

    const uint8_t data[] = "data";
    char doc_id[64] = {0};

    ASSERT_EQ(DOC_STORE_ERR_INVALID_ARG, doc_store_store(NULL, "a.txt", data, 4, doc_id, 64));
    ASSERT_EQ(DOC_STORE_ERR_INVALID_ARG, doc_store_store(store, NULL, data, 4, doc_id, 64));
    ASSERT_EQ(DOC_STORE_ERR_INVALID_ARG, doc_store_store(store, "a.txt", NULL, 4, doc_id, 64));
    ASSERT_EQ(DOC_STORE_ERR_INVALID_ARG, doc_store_store(store, "a.txt", data, 4, NULL, 64));

    document_store_free(store);
    storage_backend_close(storage);
    test_rmrf(TEST_DOC_STORAGE_PATH);
    TEST_PASS();
}

/* ── Store/Read Text Tests ────────────────────────────────────────────────── */

TEST_CASE(doc_store_store_and_read_text) {
    StorageBackend *storage = open_test_storage();
    DocumentStore *store = document_store_new(storage);

    const uint8_t data[] = "original";
    char doc_id[64] = {0};
    doc_store_store(store, "test.txt", data, sizeof(data), doc_id, sizeof(doc_id));

    /* Store OCR text */
    int rc = doc_store_store_text(store, doc_id, "ocr", "Extracted text here");
    ASSERT_EQ(DOC_STORE_OK, rc);

    /* Read it back */
    char *read_text = doc_store_read_text(store, doc_id, "ocr");
    ASSERT_NOT_NULL(read_text);
    ASSERT_STR_EQ("Extracted text here", read_text);
    free(read_text);

    document_store_free(store);
    storage_backend_close(storage);
    test_rmrf(TEST_DOC_STORAGE_PATH);
    TEST_PASS();
}

TEST_CASE(doc_store_read_text_not_found) {
    StorageBackend *storage = open_test_storage();
    DocumentStore *store = document_store_new(storage);

    char *text = doc_store_read_text(store, "nonexistent", "ocr");
    ASSERT_TRUE(text == NULL);

    document_store_free(store);
    storage_backend_close(storage);
    test_rmrf(TEST_DOC_STORAGE_PATH);
    TEST_PASS();
}

TEST_CASE(doc_store_store_text_null_args) {
    StorageBackend *storage = open_test_storage();
    DocumentStore *store = document_store_new(storage);

    ASSERT_EQ(DOC_STORE_ERR_INVALID_ARG, doc_store_store_text(NULL, "id", "ocr", "text"));
    ASSERT_EQ(DOC_STORE_ERR_INVALID_ARG, doc_store_store_text(store, NULL, "ocr", "text"));
    ASSERT_EQ(DOC_STORE_ERR_INVALID_ARG, doc_store_store_text(store, "id", NULL, "text"));
    ASSERT_EQ(DOC_STORE_ERR_INVALID_ARG, doc_store_store_text(store, "id", "ocr", NULL));

    document_store_free(store);
    storage_backend_close(storage);
    test_rmrf(TEST_DOC_STORAGE_PATH);
    TEST_PASS();
}

/* ── Delete Tests ─────────────────────────────────────────────────────────── */

TEST_CASE(doc_store_delete_document) {
    StorageBackend *storage = open_test_storage();
    DocumentStore *store = document_store_new(storage);

    const uint8_t data[] = "original";
    char doc_id[64] = {0};
    doc_store_store(store, "test.txt", data, sizeof(data), doc_id, sizeof(doc_id));
    doc_store_store_text(store, doc_id, "ocr", "text");

    int rc = doc_store_delete(store, doc_id);
    ASSERT_EQ(DOC_STORE_OK, rc);

    document_store_free(store);
    storage_backend_close(storage);
    test_rmrf(TEST_DOC_STORAGE_PATH);
    TEST_PASS();
}

TEST_CASE(doc_store_delete_null_args) {
    ASSERT_EQ(DOC_STORE_ERR_INVALID_ARG, doc_store_delete(NULL, "id"));
    TEST_PASS();
}

/* ── Exists Tests ─────────────────────────────────────────────────────────── */

TEST_CASE(doc_store_exists) {
    StorageBackend *storage = open_test_storage();
    DocumentStore *store = document_store_new(storage);

    const uint8_t data[] = "content";
    char doc_id[64] = {0};
    doc_store_store(store, "exists.txt", data, sizeof(data), doc_id, sizeof(doc_id));

    ASSERT_EQ(1, doc_store_exists(store, doc_id));

    document_store_free(store);
    storage_backend_close(storage);
    test_rmrf(TEST_DOC_STORAGE_PATH);
    TEST_PASS();
}

TEST_CASE(doc_store_exists_not_found) {
    StorageBackend *storage = open_test_storage();
    DocumentStore *store = document_store_new(storage);

    ASSERT_EQ(0, doc_store_exists(store, "nonexistent_file"));

    document_store_free(store);
    storage_backend_close(storage);
    test_rmrf(TEST_DOC_STORAGE_PATH);
    TEST_PASS();
}

TEST_CASE(doc_store_exists_null) {
    ASSERT_EQ(0, doc_store_exists(NULL, "id"));
    TEST_PASS();
}
