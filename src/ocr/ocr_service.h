#ifndef OCR_SERVICE_H
#define OCR_SERVICE_H

#include <stddef.h>
#include <stdint.h>

/* ── Forward Declarations ────────────────────────────────────────────────── */

typedef struct OCRService OCRService;

/* ── Error Codes ─────────────────────────────────────────────────────────── */

typedef enum {
    OCR_OK = 0,
    OCR_ERR_MEMORY = -1,
    OCR_ERR_INVALID_ARG = -2,
    OCR_ERR_API = -3,
    OCR_ERR_TIMEOUT = -4,
    OCR_ERR_RATE_LIMITED = -5,
    OCR_ERR_NO_TEXT = -6,
    OCR_ERR_PARSE = -7,
} OCRError;

/* ── Service Interface ───────────────────────────────────────────────────── */

typedef struct {
    /* Extract text from image/PDF data. Caller must free *out_text on success */
    int (*extract_text)(void *ctx, const uint8_t *data, size_t len, const char *filename,
                        char **out_text);

    /* Extract text with a specific model. Caller must free *out_text on success */
    int (*extract_text_with_model)(void *ctx, const uint8_t *data, size_t len, const char *filename,
                                   const char *model, char **out_text);

    /* Parse receipt OCR text into structured JSON. Caller must free *out_json on success */
    int (*parse_receipt)(void *ctx, const char *ocr_text, char **out_json);

    /* Get current model name (for logging/debugging) */
    const char *(*get_model)(void *ctx);

    /* Check if service is healthy/available */
    int (*is_healthy)(void *ctx);

    /* Get total tokens used in the last API call */
    int (*get_last_tokens)(void *ctx);
} OCRServiceOps;

struct OCRService {
    const OCRServiceOps *ops;
    void *internal; /* Implementation-specific data */
};

/* ── Convenience Wrappers ────────────────────────────────────────────────── */

static inline int ocr_extract_text(OCRService *ocr, const uint8_t *data, size_t len,
                                   const char *filename, char **out_text) {
    if (!ocr || !ocr->ops || !data || !filename || !out_text) {
        return OCR_ERR_INVALID_ARG;
    }
    return ocr->ops->extract_text(ocr->internal, data, len, filename, out_text);
}

static inline int ocr_extract_text_with_model(OCRService *ocr, const uint8_t *data, size_t len,
                                              const char *filename, const char *model,
                                              char **out_text) {
    if (!ocr || !ocr->ops || !data || !filename || !model || !out_text) {
        return OCR_ERR_INVALID_ARG;
    }
    if (ocr->ops->extract_text_with_model) {
        return ocr->ops->extract_text_with_model(ocr->internal, data, len, filename, model,
                                                 out_text);
    }
    /* Fallback: use default extract */
    return ocr->ops->extract_text(ocr->internal, data, len, filename, out_text);
}

static inline int ocr_parse_receipt(OCRService *ocr, const char *ocr_text, char **out_json) {
    if (!ocr || !ocr->ops || !ocr_text || !out_json) {
        return OCR_ERR_INVALID_ARG;
    }
    return ocr->ops->parse_receipt(ocr->internal, ocr_text, out_json);
}

static inline const char *ocr_get_model(OCRService *ocr) {
    if (!ocr || !ocr->ops)
        return NULL;
    return ocr->ops->get_model(ocr->internal);
}

static inline int ocr_is_healthy(OCRService *ocr) {
    if (!ocr || !ocr->ops)
        return 0;
    return ocr->ops->is_healthy ? ocr->ops->is_healthy(ocr->internal) : 0;
}

static inline int ocr_get_last_tokens(OCRService *ocr) {
    if (!ocr || !ocr->ops)
        return 0;
    return ocr->ops->get_last_tokens ? ocr->ops->get_last_tokens(ocr->internal) : 0;
}

/* ── Gemini Implementation ───────────────────────────────────────────────── */

/* Create OCR service backed by Gemini API.
 * api_base and timeout_secs are passed to gemini_new (use NULL/0 for defaults). */
OCRService *ocr_service_gemini_new(const char *api_key, const char *model,
                                   const char *fallback_model, int fallback_enabled,
                                   const char *api_base, long timeout_secs);

/* Free OCR service */
void ocr_service_free(OCRService *ocr);

#endif /* OCR_SERVICE_H */
