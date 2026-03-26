#include "prompt_fetcher.h"
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

    time_t last_poll;
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
    memset(prompts, 0, sizeof(prompts));
    CollectCtx ctx = {.prompts = prompts, .count = 0, .capacity = MAX_CACHED_PROMPTS};

    if (db_backend_get_prompts(pf->db, collect_cb, &ctx) != 0) {
        LOG_ERROR("prompt_fetcher: failed to fetch prompts");
        return -1;
    }

    int changed = 0;

    if (!pf->initialized) {
        for (int i = 0; i < ctx.count && i < MAX_CACHED_PROMPTS; i++) {
            snprintf(pf->cache[i].name, DB_PROMPT_NAME_LEN, "%s", prompts[i].name);
            snprintf(pf->cache[i].content, DB_PROMPT_CONTENT_LEN, "%s", prompts[i].content);
        }
        pf->cache_count = ctx.count;
        pf->initialized = 1;
        pf->last_poll = time(NULL);
        return 0;
    }

    for (int i = 0; i < ctx.count; i++) {
        int found_idx = -1;
        for (int j = 0; j < pf->cache_count; j++) {
            if (strcmp(prompts[i].name, pf->cache[j].name) == 0) {
                found_idx = j;
                break;
            }
        }

        if (found_idx < 0) {
            LOG_INFO("prompt_fetcher: new prompt '%s'", prompts[i].name);
            if (pf->on_change)
                pf->on_change(prompts[i].name, prompts[i].content, pf->userdata);
            changed++;

            if (pf->cache_count < MAX_CACHED_PROMPTS) {
                int idx = pf->cache_count++;
                snprintf(pf->cache[idx].name, DB_PROMPT_NAME_LEN, "%s", prompts[i].name);
                snprintf(pf->cache[idx].content, DB_PROMPT_CONTENT_LEN, "%s", prompts[i].content);
            }
        } else if (strcmp(prompts[i].content, pf->cache[found_idx].content) != 0) {
            LOG_INFO("prompt_fetcher: prompt '%s' changed", prompts[i].name);
            if (pf->on_change)
                pf->on_change(prompts[i].name, prompts[i].content, pf->userdata);
            changed++;

            snprintf(pf->cache[found_idx].content, DB_PROMPT_CONTENT_LEN, "%s", prompts[i].content);
        }
    }

    pf->last_poll = time(NULL);
    return changed;
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
    pf->last_poll = 0;

    return pf;
}

void prompt_fetcher_free(PromptFetcher *pf) {
    if (pf)
        free(pf);
}

int prompt_fetcher_tick(PromptFetcher *pf) {
    if (!pf)
        return -1;

    time_t now = time(NULL);
    if (!pf->initialized || difftime(now, pf->last_poll) >= (double)pf->interval_secs) {
        return prompt_fetcher_poll(pf);
    }
    return 0;
}
