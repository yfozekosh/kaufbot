#ifndef GEMINI_H
#define GEMINI_H

#include <stddef.h>
#include <stdint.h>

typedef struct GeminiClient GeminiClient;

/* Create a Gemini client. api_key and model are copied internally. */
GeminiClient *gemini_new(const char *api_key, const char *model);

/* Free the client. */
void gemini_free(GeminiClient *client);

/* Send filedata (image or PDF bytes) to Gemini for OCR / text extraction.
 * filename is used to determine MIME type.
 *
 * On success: returns a heap-allocated NUL-terminated string — caller must free().
 * On error:   returns NULL and writes a message to stderr. */
char *gemini_extract_text(GeminiClient *client,
                          const uint8_t *data, size_t len,
                          const char *filename);

#endif /* GEMINI_H */
