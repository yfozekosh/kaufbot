# Code Refactoring Plan

## Current Issues

### 1. Backend Abstraction Inconsistency
**Problem:** The backend abstraction layer has inconsistent patterns between DB and Storage backends.
- DB backends use `DBBackend` struct with ops table
- Storage backends use `StorageBackend` struct with ops table

**Status:** Mostly resolved. Backward-compatibility wrappers removed from db_sqlite.c. Legacy storage utility functions kept (backend-agnostic).

### 2. Test Architecture
**Problem:** Tests use old API functions that require backward-compatibility wrappers.

**Status:** RESOLVED - all tests now use `db_backend_*` and `storage_backend_*` interfaces.

### 3. Configuration Complexity
**Problem:** Config struct is large with many backend-specific fields.

**Status:** Deferred. Functional for now.

### 4. Error Handling
**Problem:** Inconsistent error handling across modules.

**Status:** Partially addressed. `db_backend_mark_ocr_done` return value now checked in processor.c. Further standardization deferred.

### 5. Memory Management
**Problem:** Manual memory management throughout.

**Status:** Partially addressed. NULL checks added, allocation failures handled more consistently.

### 6. Build System
**Problem:** PostgreSQL is optional but creates conditional compilation.

**Status:** Improved. PostgreSQL always compiles if library found. `compile_commands.json` generation added.

## Refactoring Steps

### Phase 1: Test Modernization ✅ COMPLETED
1. ✅ Create `test_helpers.h/c` with backend setup helpers
2. ✅ Update `test_db.c` to use `db_backend_*` functions
3. ✅ Update `test_storage.c` to use backend interface
4. ✅ Update `test_edge_cases.c` to use backend interface
5. ✅ Fix backend->ops initialization in backends
6. ✅ Remove backward-compatibility wrapper code from `db_sqlite.c`
7. ✅ All tests pass with new backend interface

### Phase 2: Code Quality ✅ COMPLETED
1. ✅ Extract shared code (`GrowBuf`, `write_cb`, `base64`) into `utils.h/c`
2. ✅ Add URL percent-encoding for safe URL construction
3. ✅ Replace magic numbers with named constants across all files
4. ✅ Fix all 5 high-severity bugs from CODE_SMELLS.md
5. ✅ Fix security issues (URL injection, secret logging, command injection)
6. ✅ Add missing NULL checks and error handling
7. ✅ Split overly long functions (gemini HTTP POST consolidated)
8. ✅ Remove dead code (`__attribute__((unused))` functions, legacy wrappers)
9. ✅ Fix `db_postgres.c` missing `backend->ops` assignment
10. ✅ Fix `curl_global_init`/`curl_global_cleanup` contract

### Phase 3: Static Analysis & Formatting ✅ COMPLETED
1. ✅ Add `.clang-tidy` config with bugprone/performance/readability checks
2. ✅ Add `.clang-format` config (LLVM-based)
3. ✅ Add `.clangd` config for LSP support
4. ✅ Add CMake targets: `lint`, `format`, `format-check`
5. ✅ Add `cppcheck` integration
6. ✅ Generate `compile_commands.json` and symlink to project root

### Phase 4: Error Handling Standardization (Priority: Medium)
1. Create `errors.h` with error codes
2. Create `result.h` with Result type
3. Update DB backend functions to use Result type
4. Update Storage backend functions to use Result type
5. Update processor to handle Result types
6. Update bot to handle Result types

### Phase 5: Test Framework Migration (Priority: Medium)
1. Evaluate frameworks: Unity, cmocka, Check
2. Choose one with: test discovery, coverage, JUnit XML, mocking
3. Migrate existing tests from custom `test_runner.h`
4. Add `make test-coverage` target with gcov/lcov
5. Set minimum coverage threshold (80%)
6. Add coverage badge to README

### Phase 6: Build System Cleanup (Priority: Low)
1. Simplify CMakeLists.txt
2. Add build option for backend selection
3. Add Doxygen documentation generation

## Success Criteria

1. ✅ All tests pass without backward-compatibility wrappers
2. ✅ All high-severity code smells fixed
3. ✅ Static analysis tooling integrated
4. ✅ Code formatting standardized
5. Error messages are consistent and helpful
6. Documentation covers all public APIs
