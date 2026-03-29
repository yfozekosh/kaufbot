#ifndef PROCESSOR_H
#define PROCESSOR_H

#include "cJSON.h"
#include "file_repository.h"
#include "ocr_service.h"
#include "reply_builder.h"

/* Opaque type — construction details are internal. */
typedef struct Processor Processor;

/* User-supplied callback for duplicate files.
 * Must format a message into reply_buf (no larger than buf_len).
 * Return 0 to skip the file (no OCR). */
typedef int (*DuplicateStrategyFn)(const FileRecord *existing, char *reply_buf, size_t buf_len);

/* Built-in duplicate strategy: notify the user and skip processing */
int strategy_notify_and_skip(const FileRecord *existing, char *reply_buf, size_t buf_len);

/* Create a processor. Takes ownership of repo/storage/ocr (but NOT db).
 * repo, storage, ocr must all be non-NULL.  Returns NULL on error. */
Processor *processor_new(FileRepository *repo, StorageBackend *storage, OCRService *ocr,
                         DuplicateStrategyFn dup_strategy);

/* Process an uploaded file. Writes a human-readable summary into reply_buf
 * (no more than reply_buf_len bytes, always NUL-terminated).
 * If the file was saved, *out_id is set to the new row id (>=1);
 * otherwise *out_id is 0. */
void processor_handle_file(Processor *p, const char *original_name, const uint8_t *data,
                           size_t data_len, char *reply_buf, size_t reply_buf_len, int64_t *out_id);

/* Re-run OCR parsing for an existing file. Returns 0 on success. */
int processor_retry_ocr(Processor *p, int64_t file_id, char *reply_buf, size_t reply_buf_len);

/* Re-run OCR with a specific model, re-extracting text from the original file.
 * Returns 0 on success. */
int processor_retry_ocr_with_model(Processor *p, int64_t file_id, const char *model,
                                   char *reply_buf, size_t reply_buf_len);

/* Build a reply message from parsed JSON. Returns a heap-allocated ReplyMessage.
 * Caller must call reply_message_free(). */
ReplyMessage *processor_build_reply(cJSON *json, int64_t file_id, int tokens,
                                    StorageBackend *storage, const char *saved_name,
                                    const char *ocr_name);

/* Free the processor.  Safe to call with NULL. */
void processor_free(Processor *p);

#endif /* PROCESSOR_H */
