/* ── Prompts module tests ─────────────────────────────────────────────────── */

#include "db.h"
#include "db_backend.h"
#include "prompt_fetcher.h"
#include "test_helpers.h"
#include "test_runner.h"

#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static DBBackend *g_test_db = NULL;
static const char *g_test_db_path = "/tmp/kaufbot_test_prompts.db";

/* ── Helpers ──────────────────────────────────────────────────────────────── */

typedef struct {
    sqlite3 *conn;
} TestSQLiteDB;

static void setup_db(void) {
    test_rm(g_test_db_path);
    g_test_db = test_db_open_sqlite(g_test_db_path);
    ASSERT_NOT_NULL(g_test_db);
}

static void teardown_db(void) {
    if (g_test_db) {
        test_db_close(g_test_db);
        g_test_db = NULL;
    }
    test_rm(g_test_db_path);
}

/* Insert a prompt directly via SQLite for test setup */
static int insert_test_prompt(const char *name, const char *content) {
    TestSQLiteDB *db = (TestSQLiteDB *)g_test_db->internal;
    const char *sql = "INSERT INTO prompts (name, content, created_at, updated_at)"
                      " VALUES (?, ?, '2024-01-01T00:00:00Z', '2024-01-01T00:00:00Z')";
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db->conn, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK)
        return -1;
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, content, -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? 0 : -1;
}

/* ── Collect callback ─────────────────────────────────────────────────────── */

typedef struct {
    Prompt prompts[16];
    int count;
} PromptCollectCtx;

static void prompt_collect_cb(const Prompt *p, void *ud) {
    PromptCollectCtx *ctx = (PromptCollectCtx *)ud;
    if (ctx->count < 16) {
        ctx->prompts[ctx->count] = *p;
        ctx->count++;
    }
}

/* ── DB get_prompts tests ─────────────────────────────────────────────────── */

TEST_CASE(prompts_get_empty) {
    setup_db();

    PromptCollectCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    int result = db_backend_get_prompts(g_test_db, prompt_collect_cb, &ctx);
    ASSERT_EQ(0, result);
    ASSERT_EQ(0, ctx.count);

    teardown_db();
    TEST_PASS();
}

TEST_CASE(prompts_get_with_data) {
    setup_db();
    ASSERT_EQ(0, insert_test_prompt("ocr_system", "You are an OCR system."));
    ASSERT_EQ(0, insert_test_prompt("parser", "Parse the receipt."));

    PromptCollectCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    int result = db_backend_get_prompts(g_test_db, prompt_collect_cb, &ctx);
    ASSERT_EQ(0, result);
    ASSERT_EQ(2, ctx.count);

    /* Results are ordered by name */
    ASSERT_STR_EQ("ocr_system", ctx.prompts[0].name);
    ASSERT_STR_EQ("You are an OCR system.", ctx.prompts[0].content);
    ASSERT_STR_EQ("parser", ctx.prompts[1].name);
    ASSERT_STR_EQ("Parse the receipt.", ctx.prompts[1].content);

    teardown_db();
    TEST_PASS();
}

/* ── DB update_prompt tests ───────────────────────────────────────────────── */

TEST_CASE(prompts_update) {
    setup_db();
    ASSERT_EQ(0, insert_test_prompt("ocr_system", "Old content."));

    /* Get the prompt ID */
    PromptCollectCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    db_backend_get_prompts(g_test_db, prompt_collect_cb, &ctx);
    ASSERT_EQ(1, ctx.count);
    int64_t id = ctx.prompts[0].id;

    /* Update */
    int result = db_backend_update_prompt(g_test_db, id, "New content.");
    ASSERT_EQ(0, result);

    /* Verify */
    PromptCollectCtx ctx2;
    memset(&ctx2, 0, sizeof(ctx2));
    db_backend_get_prompts(g_test_db, prompt_collect_cb, &ctx2);
    ASSERT_EQ(1, ctx2.count);
    ASSERT_STR_EQ("New content.", ctx2.prompts[0].content);

    teardown_db();
    TEST_PASS();
}

TEST_CASE(prompts_update_nonexistent) {
    setup_db();

    int result = db_backend_update_prompt(g_test_db, 9999, "Does not exist.");
    ASSERT_EQ(0, result); /* UPDATE with no match returns OK, 0 rows affected */

    teardown_db();
    TEST_PASS();
}

/* ── DB operations with NULL ──────────────────────────────────────────────── */

TEST_CASE(prompts_get_null_db) {
    PromptCollectCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    int result = db_backend_get_prompts(NULL, prompt_collect_cb, &ctx);
    ASSERT_EQ(-1, result);
    TEST_PASS();
}

TEST_CASE(prompts_update_null_db) {
    int result = db_backend_update_prompt(NULL, 1, "content");
    ASSERT_EQ(-1, result);
    TEST_PASS();
}

/* ── Prompt fetcher tests ─────────────────────────────────────────────────── */

typedef struct {
    char names[16][DB_PROMPT_NAME_LEN];
    char contents[16][DB_PROMPT_CONTENT_LEN];
    int count;
} ChangeCtx;

static void change_cb(const char *prompt_name, const char *new_content, void *userdata) {
    ChangeCtx *ctx = (ChangeCtx *)userdata;
    if (ctx->count < 16) {
        snprintf(ctx->names[ctx->count], DB_PROMPT_NAME_LEN, "%s", prompt_name);
        snprintf(ctx->contents[ctx->count], DB_PROMPT_CONTENT_LEN, "%s", new_content);
        ctx->count++;
    }
}

TEST_CASE(prompt_fetcher_null_args) {
    PromptFetcher *pf = prompt_fetcher_new(NULL, 60, change_cb, NULL);
    ASSERT_TRUE(pf == NULL);

    pf = prompt_fetcher_new(NULL, -1, change_cb, NULL);
    ASSERT_TRUE(pf == NULL);

    prompt_fetcher_stop(NULL);
    prompt_fetcher_free(NULL);
    TEST_PASS();
}

TEST_CASE(prompt_fetcher_poll_empty) {
    setup_db();

    ChangeCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    PromptFetcher *pf = prompt_fetcher_new(g_test_db, 60, change_cb, &ctx);
    ASSERT_NOT_NULL(pf);

    /* First poll: initializes cache, no changes reported */
    int result = prompt_fetcher_poll(pf);
    ASSERT_EQ(0, result);
    ASSERT_EQ(0, ctx.count);

    prompt_fetcher_stop(pf);
    prompt_fetcher_free(pf);
    teardown_db();
    TEST_PASS();
}

TEST_CASE(prompt_fetcher_poll_no_change) {
    setup_db();
    ASSERT_EQ(0, insert_test_prompt("ocr", "Original content."));

    ChangeCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    PromptFetcher *pf = prompt_fetcher_new(g_test_db, 60, change_cb, &ctx);
    ASSERT_NOT_NULL(pf);

    /* First poll: init cache */
    ASSERT_EQ(0, prompt_fetcher_poll(pf));
    ASSERT_EQ(0, ctx.count);

    /* Second poll: no change */
    ASSERT_EQ(0, prompt_fetcher_poll(pf));
    ASSERT_EQ(0, ctx.count);

    prompt_fetcher_stop(pf);
    prompt_fetcher_free(pf);
    teardown_db();
    TEST_PASS();
}

TEST_CASE(prompt_fetcher_poll_detect_change) {
    setup_db();
    ASSERT_EQ(0, insert_test_prompt("ocr", "Original content."));

    ChangeCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    PromptFetcher *pf = prompt_fetcher_new(g_test_db, 60, change_cb, &ctx);
    ASSERT_NOT_NULL(pf);

    /* First poll: init cache */
    ASSERT_EQ(0, prompt_fetcher_poll(pf));
    ASSERT_EQ(0, ctx.count);

    /* Update the prompt in DB */
    PromptCollectCtx pc;
    memset(&pc, 0, sizeof(pc));
    db_backend_get_prompts(g_test_db, prompt_collect_cb, &pc);
    ASSERT_EQ(1, pc.count);
    db_backend_update_prompt(g_test_db, pc.prompts[0].id, "Updated content.");

    /* Second poll: should detect change */
    int changed = prompt_fetcher_poll(pf);
    ASSERT_EQ(1, changed);
    ASSERT_EQ(1, ctx.count);
    ASSERT_STR_EQ("ocr", ctx.names[0]);
    ASSERT_STR_EQ("Updated content.", ctx.contents[0]);

    /* Third poll: no further change */
    ctx.count = 0;
    ASSERT_EQ(0, prompt_fetcher_poll(pf));
    ASSERT_EQ(0, ctx.count);

    prompt_fetcher_stop(pf);
    prompt_fetcher_free(pf);
    teardown_db();
    TEST_PASS();
}

TEST_CASE(prompt_fetcher_poll_new_prompt) {
    setup_db();
    ASSERT_EQ(0, insert_test_prompt("ocr", "Original."));

    ChangeCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    PromptFetcher *pf = prompt_fetcher_new(g_test_db, 60, change_cb, &ctx);
    ASSERT_NOT_NULL(pf);

    /* First poll: init cache with one prompt */
    ASSERT_EQ(0, prompt_fetcher_poll(pf));
    ASSERT_EQ(0, ctx.count);

    /* Insert a new prompt */
    ASSERT_EQ(0, insert_test_prompt("parser", "Parse receipts."));

    /* Second poll: should detect new prompt */
    int changed = prompt_fetcher_poll(pf);
    ASSERT_EQ(1, changed);
    ASSERT_EQ(1, ctx.count);
    ASSERT_STR_EQ("parser", ctx.names[0]);
    ASSERT_STR_EQ("Parse receipts.", ctx.contents[0]);

    prompt_fetcher_stop(pf);
    prompt_fetcher_free(pf);
    teardown_db();
    TEST_PASS();
}

TEST_CASE(prompt_fetcher_poll_multiple_changes) {
    setup_db();
    ASSERT_EQ(0, insert_test_prompt("ocr", "OCR v1."));
    ASSERT_EQ(0, insert_test_prompt("parser", "Parser v1."));

    ChangeCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    PromptFetcher *pf = prompt_fetcher_new(g_test_db, 60, change_cb, &ctx);
    ASSERT_NOT_NULL(pf);

    /* First poll: init */
    ASSERT_EQ(0, prompt_fetcher_poll(pf));

    /* Update both prompts */
    PromptCollectCtx pc;
    memset(&pc, 0, sizeof(pc));
    db_backend_get_prompts(g_test_db, prompt_collect_cb, &pc);
    ASSERT_EQ(2, pc.count);
    db_backend_update_prompt(g_test_db, pc.prompts[0].id, "OCR v2.");
    db_backend_update_prompt(g_test_db, pc.prompts[1].id, "Parser v2.");

    /* Poll: should detect 2 changes */
    int changed = prompt_fetcher_poll(pf);
    ASSERT_EQ(2, changed);
    ASSERT_EQ(2, ctx.count);

    prompt_fetcher_stop(pf);
    prompt_fetcher_free(pf);
    teardown_db();
    TEST_PASS();
}

TEST_CASE(prompt_fetcher_poll_null) {
    int result = prompt_fetcher_poll(NULL);
    ASSERT_EQ(-1, result);
    TEST_PASS();
}

/* ── Prompt fetcher thread lifecycle ──────────────────────────────────────── */

TEST_CASE(prompt_fetcher_thread_lifecycle) {
    setup_db();
    ASSERT_EQ(0, insert_test_prompt("ocr", "Content."));

    ChangeCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    PromptFetcher *pf = prompt_fetcher_new(g_test_db, 1, change_cb, &ctx);
    ASSERT_NOT_NULL(pf);

    /* Let it run briefly, then stop */
    prompt_fetcher_stop(pf);
    prompt_fetcher_free(pf);
    teardown_db();
    TEST_PASS();
}
