# Next Steps

## 1. Static Analysis

- [x] Add `clang-tidy` integration via `.clang-tidy` config
- [x] Add CMake target `make lint`
- [x] Enable checks: bugprone-*, concurrency-*, memory-*, performance-*, readability-*
- [x] Add `cppcheck` as a secondary analyzer
- [x] Fix all existing warnings from `-Wall -Wextra -Wpedantic -Werror`
- [x] Run static analysis in CI (GitHub Actions / local script)

## 2. Code Formatter

- [x] Add `.clang-format` config (LLVM-based style)
- [x] Add CMake target `make format`
- [x] Add `make format-check` for CI to reject unformatted code
- [x] Document style decisions in `.clang-format`

## 3. LSP / clangd

- [x] Ensure `compile_commands.json` is generated (`CMAKE_EXPORT_COMPILE_COMMANDS=ON`)
- [x] Add symlink `build/compile_commands.json -> project root`
- [x] Add `.clangd` config for include path resolution and warnings
- [x] Verify clangd works with third_party headers (cJSON)
- [x] Verify clangd works across all build configurations (with/without PostgreSQL)
- [x] Add clangd setup instructions to README

## 4. Test Framework

- [x] Tests use `db_backend_*` and `storage_backend_*` interfaces consistently
- [x] Legacy API wrappers removed from db_sqlite.c
- [x] Evaluate/test frameworks: Unity, cmocka, Check
- [x] Add CMake target `make test-coverage`
- [x] Generate coverage report with `lcov` / `genhtml`
- [x] Set minimum coverage threshold

## 5. Code Quality

### 5.1 Structure & Readability

- [x] Replace magic numbers with named constants
- [x] Use `const` everywhere possible
- [x] Reduce function length (extracted sub-functions)
- [x] Remove dead code (`__attribute__((unused))`, legacy wrappers)
- [x] Consistent naming: `snake_case` for functions/vars, `UPPER_CASE` for macros

### 5.2 Security

- [x] Fix URL injection - percent-encode `file_id` before interpolating into URLs
- [x] Fix integer underflow in `list_cb` - proper bounds checking
- [x] Add NULL checks for cJSON results in `gemini_parse_api_response`
- [x] Check `db_backend_mark_ocr_done` return value in processor
- [x] Fix command prefix matching (`/start` no longer matches `/startup`)
- [x] Fix command injection in `test_cleanup_dir` - removed shell-based cleanup
- [x] Stop logging secrets in config.c DEBUG output
- [x] Check `curl_global_init` return value
- [x] Check `curl_slist_append` return value
- [x] Check NULL pointer in `bot_stop`
- [x] Check NULL text in `tg_send_message`
- [x] Check empty photos array before index access

### 5.3 Shared Code

- [x] Extract `GrowBuf` + `write_cb` into `utils.h/c` (was duplicated in 3 files)
- [x] Extract `base64_encode` into `utils.h/c` (was duplicated)
- [x] Add `url_percent_encode` utility for safe URL construction
- [x] Consolidate duplicated HTTP POST boilerplate in gemini.c

### 5.4 Best Practices

- [x] Fix `postgres_open` not setting `backend->ops` (inconsistent with SQLite)
- [x] Fix `curl_global_init`/`curl_global_cleanup` contract (single init)
- [x] Use `strtok_r` instead of `strtok` (thread-safe)
- [x] Use `snprintf` instead of `strncpy` everywhere
- [x] Add `_GNU_SOURCE` for POSIX function declarations
- [x] Add `#pragma once` style include guards (kept traditional for portability)
- [x] Remove `#include <stdio.h>` dependency from config.h LOG macros (still needed)

## 6. CI / Automation

- [x] CMake targets: `build`, `test`, `lint`, `format`, `format-check`
- [x] Coverage support with `ENABLE_COVERAGE` option
- [x] Add GitHub Actions workflow
- [x] Gate merges on: build, test, lint, format-check passing

---

## Appendix: Test Framework Evaluation

The project uses a custom minimal test framework (`tests/test_runner.h`). Evaluated alternatives:

| Framework | Pros | Cons | Verdict |
|-----------|------|------|---------|
| **Unity** | Single-file, zero dependencies, portable C99/C11, good assertion macros | No built-in mocking, simple runner | **Recommended** if we want to migrate. Drop-in replacement for our custom macros with better error messages and XML output for CI. |
| **cmocka** | Built-in mocking (`expect_value`, `will_return`), widely used | Requires linking external library, more setup per test | Good fit if we need mock-based testing for HTTP/DB isolation. |
| **Check** | Fork-based isolation (crash-safe), TAP output | Heavy (requires POSIX), slow due to fork(), complex setup | Overkill for this project size. |

**Decision**: Keep the current custom framework. It is lightweight, has no external dependencies, and matches the project's simplicity. If mock-based testing becomes necessary (e.g., isolating HTTP calls in bot.c/gemini.c), migrate to **cmocka**. If better CI reporting is needed, migrate to **Unity**.
