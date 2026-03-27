#ifndef GEMINI_H
#define GEMINI_H

#include <stddef.h>
#include <stdint.h>

typedef struct GeminiClient GeminiClient;

/* Create a Gemini client. api_key, model, and fallback_model are copied internally.
 * fallback_enabled controls whether rate-limit fallback is active.
 * api_base overrides the API endpoint (use NULL for default).
 * timeout_secs sets HTTP timeout in seconds (use 0 for default 600s). */
GeminiClient *gemini_new(const char *api_key, const char *model, const char *fallback_model,
                         int fallback_enabled, const char *api_base, long timeout_secs);

/* Free the client. */
void gemini_free(GeminiClient *client);

/* Send filedata (image or PDF bytes) to Gemini for OCR / text extraction.
 * filename is used to determine MIME type.
 *
 * On success: returns a heap-allocated NUL-terminated string — caller must free().
 * On error:   returns NULL and writes a message to stderr. */
char *gemini_extract_text(GeminiClient *client, const uint8_t *data, size_t len,
                          const char *filename);

/* Send OCR-extracted text to Gemini for receipt parsing.
 * Returns a heap-allocated NUL-terminated JSON string — caller must free().
 * On error: returns NULL and writes a message to stderr. */
char *gemini_parse_receipt(GeminiClient *client, const char *ocr_text);

/* Extract text from a raw Gemini API JSON response.
 * Parses candidates[0].content.parts[0].text.
 * Returns heap-allocated string or NULL on error. */
char *gemini_parse_api_response(const char *api_json);

/* Strip markdown code fences (```json ... ```) in-place.
 * Returns pointer to trimmed content within the same buffer. */
char *strip_markdown_json(char *raw);

#endif /* GEMINI_H */
