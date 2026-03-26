#ifndef PROMPT_FETCHER_H
#define PROMPT_FETCHER_H

#include "db_backend.h"

typedef struct PromptFetcher PromptFetcher;

/* Called when a prompt's content changes.
 * `prompt_name` is the name of the changed prompt.
 * `new_content` is the updated content.
 * `userdata` is the pointer passed to prompt_fetcher_new(). */
typedef void (*prompt_change_cb)(const char *prompt_name, const char *new_content, void *userdata);

/* Create a prompt fetcher that polls the DB every `interval_secs` seconds.
 * Calls `on_change` for each prompt whose content differs from the cached version.
 * `db` is borrowed (not freed by the fetcher).
 * Returns NULL on error. */
PromptFetcher *prompt_fetcher_new(DBBackend *db, int interval_secs, prompt_change_cb on_change,
                                  void *userdata);

/* Signal the fetcher thread to stop and join it. Safe to call with NULL. */
void prompt_fetcher_stop(PromptFetcher *pf);

/* Free fetcher resources. Assumes the thread has been stopped. Safe to call with NULL. */
void prompt_fetcher_free(PromptFetcher *pf);

/* Run a single fetch-and-compare cycle. Exposed for testing.
 * Returns the number of prompts that changed, or -1 on error. */
int prompt_fetcher_poll(PromptFetcher *pf);

#endif /* PROMPT_FETCHER_H */
