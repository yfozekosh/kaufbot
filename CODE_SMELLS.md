# Code Smells Report

77 findings across src/ and tests/. 5 high, 33 medium, 39 low severity.

## High Severity (5)

| # | File | Line | Issue |
|---|------|------|-------|
| 1 | `src/bot.c` | 236 | Integer underflow in `list_cb` — if `snprintf` returns >= `c->cap`, `c->cap - c->pos` wraps to huge positive, causing buffer overflow into 4096-byte stack buffer |
| 2 | `src/gemini.c` | 64-66 | NULL dereference on malformed API response — `cand0` and `cont` from cJSON not NULL-checked before dereferencing |
| 3 | `src/gemini.c` | 59,64-66 | Missing NULL checks in `gemini_parse_api_response` — `cJSON_GetArrayItem` and `cJSON_GetObjectItem` results used without verification |
| 4 | `src/processor.c` | 148 | Unchecked `db_backend_mark_ocr_done` return value — DB update silently fails, record stays inconsistent |
| 5 | `src/bot.c` | 171-172 | URL injection — `file_id` from Telegram interpolated into URL without percent-encoding |

## Code Duplication (7)

| # | Files | Description |
|---|-------|-------------|
| 6 | `bot.c`, `gemini.c`, `storage_supabase.c` | `GrowBuf` struct + `write_cb` duplicated in 3 files |
| 7 | `gemini.c`, `storage_supabase.c` | `base64_encode` duplicated (and unused in supabase) |
| 8 | `db_sqlite.c:65-86` and `:435-455` | Schema DDL defined twice |
| 9 | `storage_local.c:157-201` and `:315-345` | `mkdir` logic duplicated for legacy wrapper |
| 10 | `db_sqlite.c` | Row-mapping duplicated between new and legacy APIs |
| 11 | `gemini.c:233-334` and `:336-426` | HTTP POST boilerplate near-identical in both functions |
| 12 | `bot.c:63-87` and `:90-113` | `http_get` and `http_get_poll` differ only in timeout |

## Dead Code (5)

| # | File | Line | Description |
|---|------|------|-------------|
| 13 | `db_postgres.c` | 37-45 | `escape_literal()` — unused, marked `__attribute__((unused))` |
| 14 | `storage_supabase.c` | 79-100 | `base64_encode()` — unused, marked `__attribute__((unused))` |
| 15 | `db_sqlite.c` | 412-655 | ~240 lines of legacy compatibility wrappers, only used by tests |
| 16 | `storage_local.c` | 302-364 | Legacy wrappers for `storage_ensure_dirs`, `storage_save_file` |
| 17 | `config.h` | 5 | `#include <stdio.h>` in header — only needed by LOG macros |

## Magic Numbers (14)

| # | File | Line | Value | Should Be |
|---|------|------|-------|-----------|
| 18 | `bot.c` | 73 | `60L` | `HTTP_TIMEOUT_SECS` |
| 19 | `bot.c` | 74,101,211 | `10L` | `HTTP_CONNECT_TIMEOUT_SECS` |
| 20 | `bot.c` | 131 | `30L` | `HTTP_POST_TIMEOUT_SECS` |
| 21 | `bot.c` | 210 | `120L` | `HTTP_DOWNLOAD_TIMEOUT_SECS` |
| 22 | `bot.c` | 39 | `8192` | `INITIAL_BUF_CAPACITY` |
| 23 | `bot.c` | 235 | `10` | `MAX_LIST_ENTRIES` |
| 24 | `gemini.c` | 281 | `512` | Use `MAX_URL_LEN` |
| 25 | `gemini.c` | 304-305 | `120L`, `15L` | Named constants |
| 26 | `storage_supabase.c` | 53 | `4096` | `INITIAL_BUF_CAPACITY` |
| 27 | `storage_supabase.c` | 171,218,298 | `120L`, `30L` | Named constants |
| 28 | `storage_supabase.c` | 285 | `3600` | `SIGNED_URL_EXPIRY_SECS` |
| 29 | `db_sqlite.c` | 60-63 | `5000`, `2000` | `SQLITE_BUSY_TIMEOUT_MS`, `SQLITE_CACHE_SIZE` |
| 30 | `storage_local.c` | 182,192 | `0755` | `DEFAULT_DIR_MODE` |
| 31 | `config.c` | 101 | `7`, `8` | `strlen("http://")`, `strlen("https://")` |

## Missing Error Handling (7)

| # | File | Line | Description |
|---|------|------|-------------|
| 32 | `processor.c` | 148 | `db_backend_mark_ocr_done()` return ignored |
| 33 | `processor.c` | 167-168 | `db_backend_mark_parsing_done()` failure logged but processing continues |
| 34 | `db_sqlite.c` | 60-63 | 4x `sqlite3_exec()` for PRAGMA — return values ignored |
| 35 | `bot.c` | 364 | `cJSON_GetArraySize(photos)` — empty array not checked before index calc |
| 36 | `bot.c` | 407 | `curl_global_init()` return not checked |
| 37 | `bot.c` | 119-124 | `curl_slist_append()` NULL return not checked |
| 38 | `gemini.c` | 52 | `msg->valuestring` used without confirming `msg` is string type |

## Missing NULL Checks (10)

| # | File | Line | Description |
|---|------|------|-------------|
| 39 | `gemini.c` | 64-66 | `cand0`, `cont` dereferenced without NULL check |
| 40 | `bot.c` | 365-366 | `largest` from `cJSON_GetArrayItem` not NULL-checked |
| 41 | `processor.c` | 66 | `processor_handle_file()` — no NULL check on `p`, `original_name`, `data`, `reply_buf` |
| 42 | `db_sqlite.c` | 110-113 | `backend->internal` not NULL-checked in `sqlite_close()` |
| 43 | `db_sqlite.c` | 122 | `backend->internal` dereferenced without NULL check in `sqlite_find_by_hash` |
| 44 | `bot.c` | 429 | `bot_stop()` dereferences `bot` without NULL check |
| 45 | `bot.c` | 150-151 | `tg_send_message()` — `text` not NULL-checked |
| 46 | `processor.c` | 56 | `processor_free(p)` — no NULL check (inconsistent with other `_free` functions) |
| 47 | `bot.c` | 377-379 | Document handler — `fid`, `fname`, `fsize` obtained without type validation |
| 48 | `gemini.c` | 52 | `msg->valuestring` access without type check |

## Buffer Overflow / Unsafe String Ops (3)

| # | File | Line | Description |
|---|------|------|-------------|
| 49 | `bot.c` | 236 | Integer underflow → buffer overflow (see #1) |
| 50 | `gemini.c` | 223-224 | `strncpy` without explicit null-termination — relies on `calloc` zeroing |
| 51 | `config.c` | 38-39 | `strncpy` silently truncates long `ALLOWED_USER_IDS` without warning |

## Resource Leaks (3)

| # | File | Line | Description |
|---|------|------|-------------|
| 52 | `main.c` | 29,40 | Error paths don't free previously allocated resources |
| 53 | `tests/test_config.c` | 11-13 | `set_env()` leaks via `putenv` (intentional but accumulates) |
| 54 | `bot.c` | 119-124 | `curl_slist_append` allocation failure — NULL headers used |

## Poor Separation of Concerns (6)

| # | File | Line | Description |
|---|------|------|-------------|
| 55 | `bot.c` | 27-50 | HTTP utility (`GrowBuf`, `write_cb`) embedded in bot module |
| 56 | `gemini.c` | 1-426 | Base64, JSON, HTTP, prompts, API orchestration all in one file |
| 57 | `main.c` | 21-105 | Resource lifecycle, signals, and orchestration mixed in `main()` |
| 58 | `db_sqlite.c` | 1-655 | Backend implementation + 240-line legacy compat layer |
| 59 | `storage_local.c` | 1-364 | SHA-256, file I/O, MIME, filename gen, legacy wrappers |
| 60 | `config.c` | 93-131 | Backend-specific config parsing in generic config loader |

## Inconsistent Patterns (6)

| # | File | Line | Description |
|---|------|------|-------------|
| 61 | `gemini.c` vs `config.h` | 86-87 | `MAX_API_KEY_LEN` duplicates `MAX_TOKEN_LEN` (both 256) |
| 62 | `db_postgres.c` | 49 vs 381 | `postgres_open()` doesn't set `backend->ops` (inconsistent with SQLite) |
| 63 | `config.c` | 24 | `LOG_DEBUG` leaks sensitive env var values |
| 64 | `db_sqlite.c` | 414-418 | Legacy `typedef struct DB` shadows forward declaration in `db.h` |
| 65 | `bot.c` | 199 | Hardcoded Telegram file API URL — no `#define` like `TG_API_BASE` |
| 66 | `processor.h` | 24 | `void *gemini` parameter — should use typed forward declaration |

## Security Issues (5)

| # | File | Line | Description |
|---|------|------|-------------|
| 67 | `bot.c` | 171-172 | `file_id` interpolated into URL without encoding |
| 68 | `config.c` | 24 | Secrets logged at DEBUG level |
| 69 | `bot.c` | 248,262 | `/start` and `/list` prefix match too broad — `/startup` matches `/start` |
| 70 | `tests/test_helpers.h` | 49-51 | `system("rm -rf ...")` with `snprintf` — command injection risk |
| 71 | `db_postgres.c` | 37-45 | `escape_literal()` returns PQalloc'd memory — contract undocumented |

## Overly Long Functions (5)

| # | File | Line | Length | Description |
|---|------|------|--------|-------------|
| 72 | `config.c` | `config_load` | 115 lines | Should split into storage config, db config, telegram config |
| 73 | `processor.c` | `processor_handle_file` | 152 lines | Hash, check, save, insert, OCR, parse, reply — too many stages |
| 74 | `gemini.c` | `gemini_extract_text` | ~100 lines | JSON build + HTTP + parse interleaved |
| 75 | `gemini.c` | `gemini_parse_receipt` | ~90 lines | Same interleaving pattern |
| 76 | `db_sqlite.c` | `sqlite_mark_parsing_done` | 57 lines | Two prepare/execute branches |

## Other (1)

| # | File | Line | Description |
|---|------|------|-------------|
| 77 | `bot.c` | 407 | `curl_global_init` called in `bot_new` but `curl_global_cleanup` in `bot_free` — multiple init violates libcurl contract |
