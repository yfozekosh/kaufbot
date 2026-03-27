/* ── File Repository tests ──────────────────────────────────────────────── */

#include "config.h"
#include "db_backend.h"
#include "file_repository.h"
#include "storage_backend.h"
#include "test_helpers.h"
#include "test_runner.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_STORAGE_PATH "/tmp/kaufbot_test_repo_storage"
#define TEST_DB_PATH      "/tmp/kaufbot_test_repo.db"

/* ── Memory Repository Tests ──────────────────────────────────────────────── */

TEST_CASE(file_repo_memory_create_and_free) {
    FileRepository *repo = file_repository_memory_new(NULL);
    ASSERT_NOT_NULL(repo);

    ASSERT_EQ(0, file_repository_memory_count(repo));
    file_repository_memory_free(repo);
    TEST_PASS();
}

TEST_CASE(file_repo_memory_null_free) {
    file_repository_memory_free(NULL);
    TEST_PASS();
}

TEST_CASE(file_repo_memory_insert) {
    FileRepository *repo = file_repository_memory_new(NULL);
    ASSERT_NOT_NULL(repo);

    int64_t id = 0;
    int rc = file_repo_insert(repo, "test.jpg", "abc123hash", "saved_001.jpg", &id);
    ASSERT_EQ(FILE_REPO_OK, rc);
    ASSERT_TRUE(id > 0);
    ASSERT_EQ(1, file_repository_memory_count(repo));

    file_repository_memory_free(repo);
    TEST_PASS();
}

TEST_CASE(file_repo_memory_insert_null_args) {
    FileRepository *repo = file_repository_memory_new(NULL);
    ASSERT_NOT_NULL(repo);

    int64_t id = 0;
    ASSERT_EQ(FILE_REPO_ERR_INVALID_ARG, file_repo_insert(NULL, "a", "b", "c", &id));
    ASSERT_EQ(FILE_REPO_ERR_INVALID_ARG, file_repo_insert(repo, NULL, "b", "c", &id));
    ASSERT_EQ(FILE_REPO_ERR_INVALID_ARG, file_repo_insert(repo, "a", NULL, "c", &id));
    ASSERT_EQ(FILE_REPO_ERR_INVALID_ARG, file_repo_insert(repo, "a", "b", NULL, &id));
    ASSERT_EQ(FILE_REPO_ERR_INVALID_ARG, file_repo_insert(repo, "a", "b", "c", NULL));

    file_repository_memory_free(repo);
    TEST_PASS();
}

TEST_CASE(file_repo_memory_insert_duplicate) {
    FileRepository *repo = file_repository_memory_new(NULL);
    ASSERT_NOT_NULL(repo);

    int64_t id1 = 0, id2 = 0;
    ASSERT_EQ(FILE_REPO_OK, file_repo_insert(repo, "a.jpg", "hash1", "saved1.jpg", &id1));
    ASSERT_EQ(FILE_REPO_ERR_DUPLICATE,
              file_repo_insert(repo, "b.jpg", "hash1", "saved2.jpg", &id2));

    file_repository_memory_free(repo);
    TEST_PASS();
}

TEST_CASE(file_repo_memory_find_by_hash) {
    FileRepository *repo = file_repository_memory_new(NULL);
    ASSERT_NOT_NULL(repo);

    int64_t id = 0;
    ASSERT_EQ(FILE_REPO_OK, file_repo_insert(repo, "test.jpg", "findme", "saved.jpg", &id));

    FileRecord rec;
    memset(&rec, 0, sizeof(rec));
    ASSERT_EQ(FILE_REPO_OK, file_repo_find_by_hash(repo, "findme", &rec));
    ASSERT_STR_EQ("test.jpg", rec.original_file_name);
    ASSERT_STR_EQ("findme", rec.file_hash);
    ASSERT_STR_EQ("saved.jpg", rec.saved_file_name);

    file_repository_memory_free(repo);
    TEST_PASS();
}

TEST_CASE(file_repo_memory_find_by_hash_not_found) {
    FileRepository *repo = file_repository_memory_new(NULL);
    ASSERT_NOT_NULL(repo);

    FileRecord rec;
    ASSERT_EQ(FILE_REPO_ERR_NOT_FOUND, file_repo_find_by_hash(repo, "nonexistent", &rec));

    file_repository_memory_free(repo);
    TEST_PASS();
}

TEST_CASE(file_repo_memory_find_by_id) {
    FileRepository *repo = file_repository_memory_new(NULL);
    ASSERT_NOT_NULL(repo);

    int64_t id = 0;
    ASSERT_EQ(FILE_REPO_OK, file_repo_insert(repo, "test.jpg", "hash2", "saved.jpg", &id));

    FileRecord rec;
    memset(&rec, 0, sizeof(rec));
    ASSERT_EQ(FILE_REPO_OK, file_repo_find_by_id(repo, id, &rec));
    ASSERT_EQ(id, rec.id);
    ASSERT_STR_EQ("test.jpg", rec.original_file_name);

    file_repository_memory_free(repo);
    TEST_PASS();
}

TEST_CASE(file_repo_memory_find_by_id_not_found) {
    FileRepository *repo = file_repository_memory_new(NULL);
    ASSERT_NOT_NULL(repo);

    FileRecord rec;
    ASSERT_EQ(FILE_REPO_ERR_NOT_FOUND, file_repo_find_by_id(repo, 9999, &rec));

    file_repository_memory_free(repo);
    TEST_PASS();
}

TEST_CASE(file_repo_memory_mark_ocr_complete) {
    FileRepository *repo = file_repository_memory_new(NULL);
    ASSERT_NOT_NULL(repo);

    int64_t id = 0;
    ASSERT_EQ(FILE_REPO_OK, file_repo_insert(repo, "test.jpg", "hash3", "saved.jpg", &id));
    ASSERT_EQ(FILE_REPO_OK, file_repo_mark_ocr_complete(repo, id, "saved_ocr.txt"));

    FileRecord rec;
    ASSERT_EQ(FILE_REPO_OK, file_repo_find_by_id(repo, id, &rec));
    ASSERT_STR_EQ("saved_ocr.txt", rec.ocr_file_name);
    ASSERT_EQ(1, rec.is_ocr_processed);

    file_repository_memory_free(repo);
    TEST_PASS();
}

TEST_CASE(file_repo_memory_mark_ocr_not_found) {
    FileRepository *repo = file_repository_memory_new(NULL);
    ASSERT_NOT_NULL(repo);

    ASSERT_EQ(FILE_REPO_ERR_NOT_FOUND, file_repo_mark_ocr_complete(repo, 9999, "ocr.txt"));

    file_repository_memory_free(repo);
    TEST_PASS();
}

TEST_CASE(file_repo_memory_delete) {
    FileRepository *repo = file_repository_memory_new(NULL);
    ASSERT_NOT_NULL(repo);

    int64_t id = 0;
    ASSERT_EQ(FILE_REPO_OK, file_repo_insert(repo, "test.jpg", "hash4", "saved.jpg", &id));
    ASSERT_EQ(1, file_repository_memory_count(repo));

    ASSERT_EQ(FILE_REPO_OK, file_repo_delete_by_id(repo, id));
    ASSERT_EQ(0, file_repository_memory_count(repo));

    file_repository_memory_free(repo);
    TEST_PASS();
}

TEST_CASE(file_repo_memory_delete_not_found) {
    FileRepository *repo = file_repository_memory_new(NULL);
    ASSERT_NOT_NULL(repo);

    ASSERT_EQ(FILE_REPO_ERR_NOT_FOUND, file_repo_delete_by_id(repo, 9999));

    file_repository_memory_free(repo);
    TEST_PASS();
}

/* Callback for counting records in list */
static void repo_count_cb(const FileRecord *rec, void *ud) {
    (void)rec;
    (*(int *)ud)++;
}

TEST_CASE(file_repo_memory_list) {
    FileRepository *repo = file_repository_memory_new(NULL);
    ASSERT_NOT_NULL(repo);

    int64_t id1 = 0, id2 = 0;
    file_repo_insert(repo, "a.jpg", "h1", "s1.jpg", &id1);
    file_repo_insert(repo, "b.jpg", "h2", "s2.jpg", &id2);

    int count = 0;
    ASSERT_EQ(FILE_REPO_OK, file_repo_list(repo, repo_count_cb, &count, 0));
    ASSERT_EQ(2, count);

    file_repository_memory_free(repo);
    TEST_PASS();
}

TEST_CASE(file_repo_memory_list_with_limit) {
    FileRepository *repo = file_repository_memory_new(NULL);
    ASSERT_NOT_NULL(repo);

    int64_t id1 = 0, id2 = 0, id3 = 0;
    file_repo_insert(repo, "a.jpg", "h1", "s1.jpg", &id1);
    file_repo_insert(repo, "b.jpg", "h2", "s2.jpg", &id2);
    file_repo_insert(repo, "c.jpg", "h3", "s3.jpg", &id3);

    int count = 0;
    ASSERT_EQ(FILE_REPO_OK, file_repo_list(repo, repo_count_cb, &count, 2));
    ASSERT_EQ(2, count);

    file_repository_memory_free(repo);
    TEST_PASS();
}

TEST_CASE(file_repo_memory_clear) {
    FileRepository *repo = file_repository_memory_new(NULL);
    ASSERT_NOT_NULL(repo);

    int64_t id = 0;
    file_repo_insert(repo, "a.jpg", "h1", "s1.jpg", &id);
    file_repo_insert(repo, "b.jpg", "h2", "s2.jpg", &id);
    ASSERT_EQ(2, file_repository_memory_count(repo));

    file_repository_memory_clear(repo);
    ASSERT_EQ(0, file_repository_memory_count(repo));

    file_repository_memory_free(repo);
    TEST_PASS();
}

TEST_CASE(file_repo_memory_get_storage_null) {
    ASSERT_TRUE(file_repo_get_storage(NULL) == NULL);

    FileRepository *repo = file_repository_memory_new(NULL);
    ASSERT_TRUE(file_repo_get_storage(repo) == NULL);
    file_repository_memory_free(repo);
    TEST_PASS();
}

TEST_CASE(file_repo_memory_convenience_null_args) {
    FileRecord rec;
    ASSERT_EQ(FILE_REPO_ERR_INVALID_ARG, file_repo_find_by_hash(NULL, "x", &rec));
    ASSERT_EQ(FILE_REPO_ERR_INVALID_ARG, file_repo_find_by_id(NULL, 1, &rec));
    ASSERT_EQ(FILE_REPO_ERR_INVALID_ARG, file_repo_mark_ocr_complete(NULL, 1, "x"));
    ASSERT_EQ(FILE_REPO_ERR_INVALID_ARG, file_repo_mark_parsing_complete(NULL, 1, "x"));
    ASSERT_EQ(FILE_REPO_ERR_INVALID_ARG, file_repo_delete_by_id(NULL, 1));
    TEST_PASS();
}
