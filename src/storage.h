#ifndef STORAGE_H
#define STORAGE_H

#include <stddef.h>
#include <stdint.h>

#define SHA256_HEX_LEN 65 /* 64 hex chars + NUL */
#define MAX_FILENAME   256

/* Ensure storage directory exists. Returns 0 on success. */
int storage_ensure_dirs(const char *base_path);

/* Generate upload_{YYYY-MM-DD}_{HH}_{MM}_{SS}{ext} filename.
 * ext should include the dot, e.g. ".jpg". out must be MAX_FILENAME bytes. */
void storage_gen_filename(const char *ext, char *out, size_t out_len);

/* Build OCR result filename from saved_name:
 * e.g. "upload_2024-01-01_12_00_00.jpg" -> "upload_2024-01-01_12_00_00_ocr_result.txt"
 * out must be MAX_FILENAME bytes. */
void storage_ocr_filename(const char *saved_name, char *out, size_t out_len);

/* Write raw bytes to base_path/filename. Returns 0 on success. */
int storage_save_file(const char *base_path, const char *filename, const uint8_t *data, size_t len);

/* Write UTF-8 text to base_path/filename. Returns 0 on success. */
int storage_save_text(const char *base_path, const char *filename, const char *text);

/* Compute SHA-256 of data and write 64-char hex + NUL into out.
 * out must be at least SHA256_HEX_LEN bytes. */
void storage_sha256_hex(const uint8_t *data, size_t len, char *out);

/* Guess MIME type from filename extension. Returns a static string. */
const char *storage_mime_type(const char *filename);

#endif /* STORAGE_H */
