#ifndef PROCESSOR_H
#define PROCESSOR_H

#include "db_backend.h"
#include "storage_backend.h"
#include <stddef.h>
#include <stdint.h>

/* ── Duplicate strategy ───────────────────────────────────────────────────── */

typedef int (*DuplicateStrategyFn)(const FileRecord *existing, char *reply_buf, size_t buf_len);

int strategy_notify_and_skip(const FileRecord *existing, char *reply_buf, size_t buf_len);

/* ── Processor ────────────────────────────────────────────────────────────── */

typedef struct Processor Processor;

/* All pointers are borrowed – processor does NOT free them. */
Processor *processor_new(DBBackend *db, StorageBackend *storage, void *gemini, /* GeminiClient* */
                         DuplicateStrategyFn dup_strategy);

void processor_free(Processor *p);

/* Process an uploaded file. */
void processor_handle_file(Processor *p, const char *original_name, const uint8_t *data,
                           size_t data_len, char *reply_buf, size_t reply_buf_len);

/* ── Test helpers ─────────────────────────────────────────────────────────── */

#ifdef TEST_BUILD
#include "cJSON.h"

/* Build reply from parsed JSON - exposed for testing */
void processor_build_reply_ok(char *reply_buf, size_t buf_len, const char *saved_name,
                              const char *ocr_filename, const char *original_name, size_t data_len,
                              cJSON *json);
#endif

#endif /* PROCESSOR_H */
