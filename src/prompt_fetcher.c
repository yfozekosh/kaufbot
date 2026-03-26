#include "prompt_fetcher.h"
#include "config.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_CACHED_PROMPTS 64

typedef struct {
    char name[DB_PROMPT_NAME_LEN];
    char content[DB_PROMPT_CONTENT_LEN];
} CachedPrompt;

struct PromptFetcher {
    DBBackend *db;
    int interval_secs;
    prompt_change_cb on_change;
    void *userdata;

    CachedPrompt cache[MAX_CACHED_PROMPTS];
    int cache_count;
    int initialized;

    pthread_t thread;
    atomic_int running;
};

/* ── Collect callback context ─────────────────────────────────────────────── */

typedef struct {
    Prompt *prompts;
    int count;
    int capacity;
} CollectCtx;

static void collect_cb(const Prompt *p, void *ud) {
    CollectCtx *ctx = (CollectCtx *)ud;
    if (ctx->count < ctx->capacity) {
        ctx->prompts[ctx->count] = *p;
        ctx->count++;
    }
}

/* ── Poll logic ───────────────────────────────────────────────────────────── */

int prompt_fetcher_poll(PromptFetcher *pf) {
    if (!pf || !pf->db)
        return -1;

    Prompt prompts[MAX_CACHED_PROMPTS];
    CollectCtx ctx = {.prompts = prompts, .count = 0, .capacity = MAX_CACHED_PROMPTS};

    if (db_backend_get_prompts(pf->db, collect_cb, &ctx) != 0) {
        LOG_ERROR("prompt_fetcher: failed to fetch prompts");
        return -1;
    }

    int changed = 0;

    if (!pf->initialized) {
        /* First poll: populate cache silently */
        for (int i = 0; i < ctx.count && i < MAX_CACHED_PROMPTS; i++) {
            snprintf(pf->cache[i].name, DB_PROMPT_NAME_LEN, "%s", prompts[i].name);
            snprintf(pf->cache[i].content, DB_PROMPT_CONTENT_LEN, "%s", prompts[i].content);
        }
        pf->cache_count = ctx.count;
        pf->initialized = 1;
        return 0;
    }

    /* Build a set of current prompt names for removed-detection */
    for (int i = 0; i < ctx.count; i++) {
        /* Find in cache */
        int found_idx = -1;
        for (int j = 0; j < pf->cache_count; j++) {
            if (strcmp(prompts[i].name, pf->cache[j].name) == 0) {
                found_idx = j;
                break;
            }
        }

        if (found_idx < 0) {
            /* New prompt not in cache — treat as change */
            LOG_INFO("prompt_fetcher: new prompt '%s'", prompts[i].name);
            if (pf->on_change)
                pf->on_change(prompts[i].name, prompts[i].content, pf->userdata);
            changed++;

            /* Add to cache */
            if (pf->cache_count < MAX_CACHED_PROMPTS) {
                int idx = pf->cache_count++;
                snprintf(pf->cache[idx].name, DB_PROMPT_NAME_LEN, "%s", prompts[i].name);
                snprintf(pf->cache[idx].content, DB_PROMPT_CONTENT_LEN, "%s", prompts[i].content);
            }
        } else if (strcmp(prompts[i].content, pf->cache[found_idx].content) != 0) {
            /* Content changed */
            LOG_INFO("prompt_fetcher: prompt '%s' changed", prompts[i].name);
            if (pf->on_change)
                pf->on_change(prompts[i].name, prompts[i].content, pf->userdata);
            changed++;

            /* Update cache */
            snprintf(pf->cache[found_idx].content, DB_PROMPT_CONTENT_LEN, "%s", prompts[i].content);
        }
    }

    return changed;
}

/* ── Background thread ────────────────────────────────────────────────────── */

static void *fetcher_thread(void *arg) {
    PromptFetcher *pf = (PromptFetcher *)arg;
    LOG_INFO("prompt_fetcher: thread started (interval=%ds)", pf->interval_secs);

    while (atomic_load(&pf->running)) {
        prompt_fetcher_poll(pf);

        /* Sleep in small increments so we can exit promptly */
        for (int i = 0; i < pf->interval_secs && atomic_load(&pf->running); i++) {
            sleep(1);
        }
    }

    LOG_INFO("prompt_fetcher: thread stopped");
    return NULL;
}

/* ── Public API ───────────────────────────────────────────────────────────── */

PromptFetcher *prompt_fetcher_new(DBBackend *db, int interval_secs, prompt_change_cb on_change,
                                  void *userdata) {
    if (!db || interval_secs <= 0)
        return NULL;

    PromptFetcher *pf = calloc(1, sizeof(PromptFetcher));
    if (!pf)
        return NULL;

    pf->db = db;
    pf->interval_secs = interval_secs;
    pf->on_change = on_change;
    pf->userdata = userdata;
    pf->cache_count = 0;
    pf->initialized = 0;
    atomic_store(&pf->running, 1);

    int rc = pthread_create(&pf->thread, NULL, fetcher_thread, pf);
    if (rc != 0) {
        LOG_ERROR("prompt_fetcher: failed to create thread: %d", rc);
        free(pf);
        return NULL;
    }

    return pf;
}

void prompt_fetcher_stop(PromptFetcher *pf) {
    if (!pf)
        return;
    atomic_store(&pf->running, 0);
    pthread_join(pf->thread, NULL);
}

void prompt_fetcher_free(PromptFetcher *pf) {
    if (pf)
        free(pf);
}
