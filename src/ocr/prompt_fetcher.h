#ifndef PROMPT_FETCHER_H
#define PROMPT_FETCHER_H

#include "db_backend.h"
#include <time.h>

typedef struct PromptFetcher PromptFetcher;

/* Called when a prompt's content changes. */
typedef void (*prompt_change_cb)(const char *prompt_name, const char *new_content, void *userdata);

/* Create a prompt fetcher state. Polls every `interval_secs` seconds.
 * Returns NULL on error. */
PromptFetcher *prompt_fetcher_new(DBBackend *db, int interval_secs, prompt_change_cb on_change,
                                  void *userdata);

/* Free fetcher resources. Safe to call with NULL. */
void prompt_fetcher_free(PromptFetcher *pf);

/* Check if interval has elapsed; if so, fetch and fire callbacks.
 * Call this each iteration of your main loop.
 * Returns the number of prompts that changed (0 if skipped or no changes), or -1 on error. */
int prompt_fetcher_tick(PromptFetcher *pf);

/* Run a single fetch-and-compare cycle unconditionally. Exposed for testing.
 * Returns the number of prompts that changed, or -1 on error. */
int prompt_fetcher_poll(PromptFetcher *pf);

#endif /* PROMPT_FETCHER_H */
