# LLM Agent Coding Rules

## Testing

- **Always write tests alongside new code.** Every new function, module, or feature must have corresponding tests in the same change.
- **Target 60% minimum code coverage.** Measure with gcov/lcov. Fail CI if coverage drops below threshold.
- **Test business logic first.** Core logic (parsing, validation, data transformation) gets tests before UI, glue code, or plumbing.
- **Test both happy and sad paths.** For every function, test: valid input, invalid input, edge cases, NULL, empty, boundary values.
- **Mock external dependencies.** HTTP calls, databases, filesystem — isolate them behind interfaces and use mocks in unit tests.

## Warnings

- **Never suppress warnings.** Treat all compiler warnings (`-Wall -Wextra -Wpedantic`) as errors (`-Werror`). Fix the root cause.
- **Never use `#pragma GCC diagnostic ignored` or `__attribute__((unused))` to silence warnings.** Remove dead code instead.
- **Never use casts to silence implicit conversion warnings.** Fix the types.

## Code Quality

- **Write code for humans.** Prefer clarity over cleverness. Use descriptive names.
- **Keep functions short.** Max ~50 lines. Extract sub-functions for complex logic.
- **No magic numbers.** Use named constants (`#define`, `const`, `enum`).
- **Check every return value.** Especially from allocators, I/O, and library calls.
- **Use `const` everywhere possible.** Function parameters, local variables, pointer targets.
- **Use `static` for file-local functions.** Only expose what the header declares.
- **Free every allocation.** Every `malloc`/`calloc` must have a corresponding `free` on all code paths, including error paths.

## Security

- **Never log secrets.** Mask API keys, tokens, passwords in all log output.
- **Validate all external input.** Telegram payloads, API responses, user strings.
- **Use `snprintf` over `strncpy`/`strncat`.** Always null-terminate explicitly.
- **Sanitize data before interpolating into URLs, SQL, or shell commands.**

## Structure

- **One concern per file.** HTTP client code, JSON parsing, business logic, and I/O should be in separate modules.
- **Use dependency injection.** Pass interfaces (`typedef struct`) instead of calling globals or hardcoding dependencies.
- **Keep backward-compat wrappers in separate files.** Don't bloat implementation files with legacy shims.

## Git

- **Atomic commits.** Each commit should be one logical change that compiles and passes tests.
- **Commit message format:** `type: description` (e.g., `fix: handle NULL in parser`, `feat: add PostgreSQL backend`).
- **Never commit secrets.** Use `.env` files (gitignored) for credentials.
