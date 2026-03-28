#include "ocr_service.h"
#include "config.h"
#include "gemini.h"

#include <stdlib.h>
#include <string.h>

/* ── Internal Structure ──────────────────────────────────────────────────── */

typedef struct {
    GeminiClient *gemini;
    char model[GEMINI_MAX_MODEL_LEN];
} GeminiOCRService;

/* ── Gemini Implementation ───────────────────────────────────────────────── */

static int gemini_ocr_extract_text(void *ctx, const uint8_t *data, size_t len, const char *filename,
                                   char **out_text) {
    if (!ctx || !data || !filename || !out_text) {
        return OCR_ERR_INVALID_ARG;
    }

    GeminiOCRService *service = (GeminiOCRService *)ctx;
    char *text = gemini_extract_text(service->gemini, data, len, filename);

    if (!text) {
        return OCR_ERR_API;
    }

    /* Check for no text found marker */
    if (strstr(text, "[NO TEXT FOUND]") != NULL) {
        free(text);
        return OCR_ERR_NO_TEXT;
    }

    *out_text = text;
    return OCR_OK;
}

static int gemini_ocr_extract_text_with_model(void *ctx, const uint8_t *data, size_t len,
                                              const char *filename, const char *model,
                                              char **out_text) {
    if (!ctx || !data || !filename || !model || !out_text) {
        return OCR_ERR_INVALID_ARG;
    }

    GeminiOCRService *service = (GeminiOCRService *)ctx;
    char *text = gemini_extract_text_with_model(service->gemini, data, len, filename, model);

    if (!text) {
        return OCR_ERR_API;
    }

    if (strstr(text, "[NO TEXT FOUND]") != NULL) {
        free(text);
        return OCR_ERR_NO_TEXT;
    }

    *out_text = text;
    return OCR_OK;
}

static int gemini_ocr_parse_receipt(void *ctx, const char *ocr_text, char **out_json) {
    if (!ctx || !ocr_text || !out_json) {
        return OCR_ERR_INVALID_ARG;
    }

    GeminiOCRService *service = (GeminiOCRService *)ctx;
    char *json = gemini_parse_receipt(service->gemini, ocr_text);

    if (!json) {
        return OCR_ERR_PARSE;
    }

    *out_json = json;
    return OCR_OK;
}

static const char *gemini_ocr_get_model(void *ctx) {
    if (!ctx)
        return NULL;
    GeminiOCRService *service = (GeminiOCRService *)ctx;
    return service->model;
}

static int gemini_ocr_is_healthy(void *ctx) {
    /* Gemini client doesn't have a health check - assume healthy if initialized */
    return (ctx != NULL) ? 1 : 0;
}

/* ── VTable ──────────────────────────────────────────────────────────────── */

static const OCRServiceOps gemini_ocr_ops = {
    .extract_text = gemini_ocr_extract_text,
    .extract_text_with_model = gemini_ocr_extract_text_with_model,
    .parse_receipt = gemini_ocr_parse_receipt,
    .get_model = gemini_ocr_get_model,
    .is_healthy = gemini_ocr_is_healthy,
};

/* ── Public API ──────────────────────────────────────────────────────────── */

OCRService *ocr_service_gemini_new(const char *api_key, const char *model,
                                   const char *fallback_model, int fallback_enabled,
                                   const char *api_base, long timeout_secs) {
    if (!api_key || !model) {
        return NULL;
    }

    GeminiClient *gemini =
        gemini_new(api_key, model, fallback_model, fallback_enabled, api_base, timeout_secs);
    if (!gemini) {
        return NULL;
    }

    GeminiOCRService *service = calloc(1, sizeof(GeminiOCRService));
    if (!service) {
        gemini_free(gemini);
        return NULL;
    }

    service->gemini = gemini;
    strncpy(service->model, model, sizeof(service->model) - 1);

    OCRService *public = calloc(1, sizeof(OCRService));
    if (!public) {
        gemini_free(gemini);
        free(service);
        return NULL;
    }

    public->ops = &gemini_ocr_ops;
    public->internal = service;

    return public;
}

void ocr_service_free(OCRService *ocr) {
    if (!ocr)
        return;

    if (ocr->internal) {
        GeminiOCRService *service = (GeminiOCRService *)ocr->internal;
        if (service->gemini) {
            gemini_free(service->gemini);
        }
        free(service);
    }
    free(ocr);
}
