# Code Refactoring Plan

## Overview
This document outlines the refactoring steps needed to improve the codebase structure, maintainability, and testability.

## Current Issues

### 1. Backend Abstraction Inconsistency
**Problem:** The backend abstraction layer has inconsistent patterns between DB and Storage backends.
- DB backends use `DBBackend` struct with ops table
- Storage backends use `StorageBackend` struct with ops table
- Both have backward-compatibility wrappers that duplicate code

**Solution:**
- Create a unified `backend.h` with common patterns
- Use consistent naming conventions
- Remove backward-compatibility wrappers by updating tests to use new API

### 2. Test Architecture
**Problem:** Tests use old API functions that require backward-compatibility wrappers.
- `db_open()`, `db_close()`, `db_insert()`, etc.
- `storage_save_file()`, `storage_ensure_dirs()`, etc.

**Solution:**
- Update test files to use backend interface directly
- Remove backward-compatibility wrapper code from `db_sqlite.c` and `storage_local.c`
- Create test helpers that set up backend instances

### 3. Configuration Complexity
**Problem:** Config struct is large with many backend-specific fields.
- 15+ fields for different backends
- No validation of field combinations

**Solution:**
- Split config into sub-structs: `Config`, `DBConfig`, `StorageConfig`
- Add config validation function
- Consider builder pattern for config creation

### 4. Error Handling
**Problem:** Inconsistent error handling across modules.
- Some functions return -1 on error
- Some return NULL
- Logging is inconsistent

**Solution:**
- Create `result.h` with `Result<T>` type pattern
- Standardize error codes in `errors.h`
- Add error context/messaging

### 5. Memory Management
**Problem:** Manual memory management throughout.
- No clear ownership semantics
- Risk of memory leaks

**Solution:**
- Document ownership in comments
- Consider arena allocator for request-scoped memory
- Add memory tracking in debug builds

### 6. Build System
**Problem:** PostgreSQL is optional but creates conditional compilation.
- `#ifdef HAVE_POSTGRES` throughout codebase
- Test builds don't include all backends

**Solution:**
- Make all backends always compile (stub if library missing)
- Use runtime detection instead of compile-time
- Simplify CMakeLists.txt

## Refactoring Steps

### Phase 1: Test Modernization (Priority: High)
1. Create `test_helpers.h/c` with backend setup helpers
2. Update `test_db.c` to use `db_backend_*` functions
3. Update `test_storage.c` to use `storage_backend_*` functions
4. Remove backward-compatibility wrappers from `db_sqlite.c`
5. Remove backward-compatibility wrappers from `storage_local.c`
6. Verify all tests pass

### Phase 2: Configuration Refactoring (Priority: Medium)
1. Create `config_types.h` with sub-structs
2. Update `config_load()` to populate sub-structs
3. Add `config_validate()` function
4. Update all users of config fields
5. Update `.env.example` with clearer sections

### Phase 3: Error Handling Standardization (Priority: Medium)
1. Create `errors.h` with error codes
2. Create `result.h` with Result type
3. Update DB backend functions to use Result type
4. Update Storage backend functions to use Result type
5. Update processor to handle Result types
6. Update bot to handle Result types

### Phase 4: Build System Cleanup (Priority: Low)
1. Remove `HAVE_POSTGRES` conditional compilation
2. Create stub implementations for missing libraries
3. Simplify CMakeLists.txt
4. Add build option for backend selection

### Phase 5: Code Quality (Priority: Low)
1. Add doxygen comments to all public APIs
2. Create architecture documentation
3. Add memory tracking for debug builds
4. Create performance benchmarks

## Timeline Estimate

| Phase | Estimated Effort | Risk |
|-------|-----------------|------|
| Phase 1 | 4-6 hours | Low |
| Phase 2 | 6-8 hours | Medium |
| Phase 3 | 8-10 hours | High |
| Phase 4 | 2-4 hours | Low |
| Phase 5 | 4-6 hours | Low |

## Success Criteria

1. All tests pass without backward-compatibility wrappers
2. No `#ifdef HAVE_POSTGRES` in source files
3. Config validation catches invalid combinations
4. Error messages are consistent and helpful
5. Documentation covers all public APIs

## Notes

- Each phase should be committed separately
- Run full test suite after each phase
- Maintain backward compatibility for external APIs during refactoring
- Consider feature flags for gradual rollout of changes
