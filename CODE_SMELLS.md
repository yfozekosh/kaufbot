# Code Smells Report

77 findings across src/ and tests/. All high-severity issues have been fixed.

## High Severity (5) - ALL FIXED

| # | File | Line | Issue | Status |
|---|------|------|-------|--------|
| 1 | `src/bot.c` | 236 | Integer underflow in `list_cb` | **FIXED** - proper bounds checking |
| 2 | `src/gemini.c` | 64-66 | NULL dereference on malformed API response | **FIXED** - NULL checks added |
| 3 | `src/gemini.c` | 59,64-66 | Missing NULL checks in `gemini_parse_api_response` | **FIXED** |
| 4 | `src/processor.c` | 148 | Unchecked `db_backend_mark_ocr_done` return value | **FIXED** |
| 5 | `src/bot.c` | 171-172 | URL injection - `file_id` not percent-encoded | **FIXED** - `url_percent_encode` used |

## Code Duplication (7) - PARTIALLY FIXED

| # | Files | Description | Status |
|---|-------|-------------|--------|
| 6 | `bot.c`, `gemini.c`, `storage_supabase.c` | `GrowBuf` + `write_cb` duplicated | **FIXED** - moved to `utils.h/c` |
| 7 | `gemini.c`, `storage_supabase.c` | `base64_encode` duplicated | **FIXED** - moved to `utils.h/c` |
| 8 | `db_sqlite.c` | Schema DDL defined twice | **FIXED** - legacy wrappers removed |
| 9 | `storage_local.c` | `mkdir` logic duplicated | **FIXED** - legacy wrappers removed |
| 10 | `db_sqlite.c` | Row-mapping duplicated | **FIXED** - legacy wrappers removed |
| 11 | `gemini.c` | HTTP POST boilerplate near-identical | **FIXED** - `gemini_post_and_parse` shared |
| 12 | `bot.c` | `http_get` and `http_get_poll` differ only in timeout | Remaining - acceptable |

## Dead Code (5) - ALL FIXED

| # | File | Description | Status |
|---|------|-------------|--------|
| 13 | `db_postgres.c` | `escape_literal()` unused | **FIXED** - removed |
| 14 | `storage_supabase.c` | `base64_encode()` unused | **FIXED** - removed |
| 15 | `db_sqlite.c` | ~240 lines of legacy wrappers | **FIXED** - removed |
| 16 | `storage_local.c` | Legacy wrappers | **FIXED** - removed |
| 17 | `config.h` | `#include <stdio.h>` in header | Remaining - needed for LOG macros |

## Magic Numbers (14) - ALL FIXED

All magic numbers replaced with named constants across all source files.

## Security Issues (5) - ALL FIXED

| # | File | Issue | Status |
|---|------|-------|--------|
| 67 | `bot.c` | `file_id` interpolated without encoding | **FIXED** |
| 68 | `config.c` | Secrets logged at DEBUG level | **FIXED** |
| 69 | `bot.c` | `/start` prefix match too broad | **FIXED** - `str_starts_with` checks boundary |
| 70 | `tests/test_helpers.h` | `system("rm -rf ...")` injection risk | **FIXED** - shell removed |
| 71 | `db_postgres.c` | `escape_literal()` contract undocumented | **FIXED** - function removed |

## Other Fixes Applied

- `curl_global_init`/`curl_global_cleanup` contract fixed (single init)
- `postgres_open` now sets `backend->ops` (was missing)
- `strtok` replaced with `strtok_r` (thread-safe)
- `strncpy` replaced with `snprintf` throughout
- `curl_slist_append` NULL return checked
- Empty photos array checked before index access
- NULL check added to `bot_stop` and `tg_send_message`
