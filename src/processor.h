#ifndef PROCESSOR_H
#define PROCESSOR_H

#include <stddef.h>
#include <stdint.h>
#include "db.h"

/* ── Duplicate strategy ───────────────────────────────────────────────────── *
 *
 * To change duplicate behaviour, implement a function matching this signature
 * and pass it to processor_new().  The default is notify_and_skip_strategy.
 *
 * Parameters:
 *   existing   – the record already stored with the same hash
 *   reply_buf  – write your reply message here (will be sent to the user)
 *   buf_len    – size of reply_buf
 *
 * Return value:
 *   0  – stop processing (skip)
 *   1  – continue processing (e.g. re-run OCR, save separately)
 *
 * ─────────────────────────────────────────────────────────────────────────── */
typedef int (*DuplicateStrategyFn)(const FileRecord *existing,
                                   char *reply_buf, size_t buf_len);

/* Built-in strategies */
int strategy_notify_and_skip(const FileRecord *existing,
                              char *reply_buf, size_t buf_len);

/* ── Processor ────────────────────────────────────────────────────────────── */

typedef struct Processor Processor;

/* All three pointers are borrowed – processor does NOT free them. */
Processor *processor_new(struct DB       *db,
                         const char      *storage_path,
                         void            *gemini,   /* GeminiClient* */
                         DuplicateStrategyFn dup_strategy);

void processor_free(Processor *p);

/* Process an uploaded file.
 *
 * original_name  – original filename as reported by Telegram
 * data           – raw file bytes
 * data_len       – number of bytes
 * reply_buf      – buffer for the reply message sent back to user
 * reply_buf_len  – size of reply_buf
 */
void processor_handle_file(Processor   *p,
                           const char  *original_name,
                           const uint8_t *data, size_t data_len,
                           char        *reply_buf, size_t reply_buf_len);

#endif /* PROCESSOR_H */
