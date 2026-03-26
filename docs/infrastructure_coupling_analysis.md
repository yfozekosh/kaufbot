# Infrastructure Coupling Analysis

## Executive Summary

The kaufbot codebase exhibits **moderate to high coupling** between business logic and infrastructure concerns. While some abstractions exist (`StorageBackend`, `DBBackend` interfaces), critical areas remain tightly coupled, making testing difficult and infrastructure changes painful.

**Key Findings:**
- 10 major coupling issues identified
- 4 high-severity issues requiring immediate attention
- Business logic in `processor.c` and `bot.c` most affected
- Testability severely impacted in 6 areas

---

## 1. Direct Database Calls in Business Logic

### Location: `src/processor.c`

| Function | Lines | DB Calls |
|----------|-------|----------|
| `processor_save_file()` | 114-145 | `db_backend_insert()` |
| `processor_run_ocr()` | 154-175 | `db_backend_mark_ocr_done()` |
| `processor_parse_receipt()` | 177-199 | `db_backend_mark_parsing_done()` |
| `processor_handle_file()` | 207-247 | `db_backend_find_by_hash()` |
| `processor_retry_ocr()` | 256-324 | `db_backend_find_by_id()`, `db_backend_mark_parsing_done()` |

### Problem

The processor module orchestrates the receipt processing pipeline (save → OCR → parse), but it directly depends on concrete database operations. This creates several issues:

```c
// processor.c:137 - Direct DB call in business logic
int db_file_id = db_backend_insert(db, original_name, hash, saved_filename);
if (db_file_id < 0) {
    snprintf(reply, reply_len, "Failed to save file metadata.");
    return;
}
```

**Why it's problematic:**
- Cannot unit test processor logic without a real database
- Database schema changes require changes to `processor.c`
- Cannot easily mock database for testing
- Business rules (duplicate detection, retry logic) are entangled with persistence

### Suggested Refactoring

Create a **Repository Pattern** abstraction:

```c
// New file: src/file_repository.h
#ifndef FILE_REPOSITORY_H
#define FILE_REPOSITORY_H

#include "db.h"
#include <stdint.h>

typedef struct FileRepository FileRepository;

typedef struct {
    int (*find_duplicate)(void *ctx, const char *hash, FileRecord *out);
    int (*save_record)(void *ctx, FileRecord *rec, int64_t *out_id);
    int (*mark_ocr_complete)(void *ctx, int64_t id, const char *ocr_filename);
    int (*mark_parsing_complete)(void *ctx, int64_t id, const char *json);
    int (*get_by_id)(void *ctx, int64_t id, FileRecord *out);
    int (*delete_record)(void *ctx, int64_t id);
} FileRepositoryOps;

struct FileRepository {
    const FileRepositoryOps *ops;
    void *internal;  /* DBBackend or other implementation */
};

/* Implementation backed by DBBackend */
FileRepository *file_repository_db_backend(DBBackend *db);
void file_repository_free(FileRepository *repo);

#endif
```

**Updated processor signature:**
```c
// Before
Processor *processor_new(DBBackend *db, StorageBackend *storage, 
                         void *gemini, DuplicateStrategyFn dup_strategy);

// After
Processor *processor_new(FileRepository *repo, StorageBackend *storage, 
                         OCRService *ocr, DuplicateStrategyFn dup_strategy);
```

**Benefits:**
- Processor depends on abstraction, not concrete database
- Can create `file_repository_memory()` for unit tests
- Database changes isolated to repository implementation
- Clearer separation: processor = orchestration, repository = persistence

---

## 2. Direct Storage Calls in Business Logic

### Location: `src/processor.c`

| Function | Lines | Storage Calls |
|----------|-------|---------------|
| `processor_save_file()` | 119-124 | `storage_backend_save_file()` |
| `processor_run_ocr()` | 160-165 | `storage_backend_save_text()` |
| `processor_retry_ocr()` | 289-295 | `storage_backend_read_text()` |

### Problem

While `StorageBackend` interface exists, the processor module knows too much about storage internals:

```c
// processor.c:121 - Processor knows about file paths and binary data
int rc = storage_backend_save_file(storage, saved_filename, data, len);
if (rc != 0) {
    snprintf(reply, reply_len, "Failed to save file.");
    db_backend_delete_file(db, db_file_id); /* Cleanup */
    return;
}
```

**Why it's problematic:**
- Processor handles storage error recovery (cleanup logic)
- Cannot test processor without real filesystem or cloud storage
- Storage failures mixed with business logic flow
- No abstraction for "document" concept - just raw bytes and filenames

### Suggested Refactoring

Create a **Document Store** abstraction:

```c
// New file: src/document_store.h
#ifndef DOCUMENT_STORE_H
#define DOCUMENT_STORE_H

#include <stdint.h>
#include <stddef.h>

typedef struct DocumentStore DocumentStore;

/* High-level document operations */
typedef struct {
    /* Store original document, returns document ID */
    int (*store_document)(void *ctx, const char *original_name, 
                          const uint8_t *data, size_t len, 
                          char *out_doc_id);
    
    /* Store associated text (OCR result, parsed JSON) */
    int (*store_text)(void *ctx, const char *doc_id, const char *text);
    
    /* Read stored text */
    char* (*read_text)(void *ctx, const char *doc_id);
    
    /* Delete document and all associated files */
    int (*delete_document)(void *ctx, const char *doc_id);
} DocumentStoreOps;

struct DocumentStore {
    const DocumentStoreOps *ops;
    void *internal;  /* StorageBackend implementation */
};

DocumentStore *document_store_new(StorageBackend *storage);
void document_store_free(DocumentStore *store);

#endif
```

**Updated processor usage:**
```c
// Before
int rc = storage_backend_save_file(storage, saved_filename, data, len);

// After
char doc_id[64];
int rc = document_store->ops->store_document(
    document_store->internal, 
    original_name, data, len, doc_id
);
```

**Benefits:**
- Processor works with document IDs, not file paths
- Storage implementation details hidden
- Easier to add caching, CDN, or multi-storage strategies
- Cleanup logic moves to document store

---

## 3. Hardcoded URLs and Configuration

### Location: `src/bot.c`, `src/gemini.c`

#### Telegram API URLs (bot.c:17-18)
```c
#define TG_API_BASE      "https://api.telegram.org/bot"
#define TG_FILE_BASE     "https://api.telegram.org/file/bot"
```

#### Gemini API URL (gemini.c:14)
```c
#define GEMINI_API_BASE  "https://generativelanguage.googleapis.com/v1beta/models"
```

#### Timeouts and Limits (bot.c:20-28)
```c
#define POLL_TIMEOUT                 30
#define MAX_REPLY_LEN                4096
#define MAX_FILE_MB                  20
#define HTTP_TIMEOUT_SECS            60L
#define HTTP_DOWNLOAD_TIMEOUT_SECS   120L
#define RECONNECT_DELAY_SECS         5
#define RETRY_DELAY_SECS             2
```

#### Gemini Timeouts (gemini.c:15-16)
```c
#define GEMINI_HTTP_TIMEOUT_SECS         600L
#define GEMINI_HTTP_CONNECT_TIMEOUT_SECS 15L
```

### Problem

All external API endpoints and operational parameters are compile-time constants.

**Why it's problematic:**
- Cannot test against mock APIs without code changes
- Cannot use Telegram test environment (`https://api.telegram.org/bot<token>/test/`)
- Cannot tune timeouts per environment (dev vs production)
- Cannot adjust file size limits without recompilation
- Different deployments may need different values

### Suggested Refactoring

**Move to Config struct:**

```c
// src/config.h - Add to Config struct
typedef struct {
    /* Existing fields... */
    
    /* Telegram API configuration */
    char telegram_api_base[MAX_URL_LEN];
    char telegram_file_base[MAX_URL_LEN];
    long telegram_poll_timeout_secs;
    long telegram_http_timeout_secs;
    long telegram_download_timeout_secs;
    size_t max_file_size_bytes;
    size_t max_reply_len;
    
    /* Gemini API configuration */
    char gemini_api_base[GEMINI_URL_BUF_LEN];
    long gemini_http_timeout_secs;
    long gemini_connect_timeout_secs;
    char gemini_fallback_model[GEMINI_MAX_MODEL_LEN];
} Config;
```

**Environment variables:**
```bash
# .env.example
TELEGRAM_API_BASE=https://api.telegram.org/bot
TELEGRAM_POLL_TIMEOUT=30
TELEGRAM_HTTP_TIMEOUT=60
MAX_FILE_MB=20

GEMINI_API_BASE=https://generativelanguage.googleapis.com/v1beta/models
GEMINI_HTTP_TIMEOUT=600
GEMINI_FALLBACK_MODEL=gemma-3-27b-it
```

**Usage in bot.c:**
```c
// Before
snprintf(url, sizeof(url), "%s%s/sendMessage", TG_API_BASE, bot->cfg->telegram_token);

// After
snprintf(url, sizeof(url), "%s%s/sendMessage", 
         bot->cfg->telegram_api_base, bot->cfg->telegram_token);
```

**Benefits:**
- Test environments can use mock APIs
- Production can tune timeouts without code changes
- Easier A/B testing of different configurations
- Clear documentation of operational parameters

---

## 4. Monolithic bot.c Structure

### Location: `src/bot.c` (799 lines)

### Problem

The `bot.c` file contains multiple responsibilities tightly coupled together:

| Responsibility | Lines | Description |
|----------------|-------|-------------|
| HTTP client helpers | 52-106 | `http_get()`, `http_post_json()` |
| Telegram API protocol | 114-287 | `tg_send_message()`, `tg_download_file()` |
| Command handling | 354-450 | `handle_command()` |
| File handling | 452-493 | `handle_file()` |
| Update dispatch | 496-680 | `dispatch_update()`, `dispatch_photo()` |
| Long-polling loop | 704-760 | `bot_start()` |
| Notifications | 762-796 | `bot_notify_startup()`, `bot_notify_prompt_change()` |

**Why it's problematic:**
- 799 lines doing too many things
- Cannot test command routing without Telegram API stack
- Cannot test file handling without HTTP + Telegram
- Changes to Telegram protocol risk breaking business logic
- Violates Single Responsibility Principle

### Specific Examples

#### Example 1: `tg_send_message()` (lines 136-153)

```c
void tg_send_message(const TgBot *bot, int64_t chat_id, const char *text) {
    // URL construction (infrastructure)
    char url[MAX_URL_LEN];
    snprintf(url, sizeof(url), "%s%s/sendMessage", TG_API_BASE, bot->cfg->telegram_token);

    // JSON serialization (infrastructure)
    cJSON *payload = cJSON_CreateObject();
    cJSON_AddNumberToObject(payload, "chat_id", (double)chat_id);
    cJSON_AddStringToObject(payload, "text", text);
    char *body = cJSON_PrintUnformatted(payload);
    cJSON_Delete(payload);

    // HTTP POST (infrastructure)
    char *resp = http_post_json(url, body);
    free(body);
    
    // Error handling (cross-cutting)
    tg_check_ok(resp, "sendMessage");
    free(resp);
}
```

**Problem:** Mixes URL construction, JSON serialization, HTTP transport, and error handling.

#### Example 2: `handle_command()` (lines 354-450)

```c
static void handle_command(TgBot *bot, int64_t chat_id, const char *text) {
    if (str_starts_with(text, TG_CMD_START) || str_starts_with(text, TG_CMD_HELP)) {
        // Business logic: command routing
        tg_send_message(bot, chat_id, "OCR Bot\n\nSend me any image...");
        return;
    }
    
    if (str_starts_with(text, TG_CMD_LIST)) {
        // Business logic: list files
        ListCtx ctx = {reply, pos, (int)sizeof(reply), 0};
        db_backend_list(bot->db, list_cb, &ctx);  // Direct DB call!
        tg_send_message(bot, chat_id, reply);
        return;
    }
    // ... more commands
}
```

**Problem:** Command routing (business logic) directly calls infrastructure (`tg_send_message`, `db_backend_list`).

### Suggested Refactoring

**Split into separate modules:**

```
src/
├── bot_telegram.c      # Telegram protocol adapter
├── bot_commands.c      # Command routing and business logic
├── bot_polling.c       # Long-polling loop
├── bot_notifications.c # Startup/prompt notifications
└── bot.h               # Public API
```

**Define interfaces:**

```c
// src/bot_interfaces.h
#ifndef BOT_INTERFACES_H
#define BOT_INTERFACES_H

#include <stdint.h>

/* Message sending abstraction */
typedef struct {
    void (*send_message)(void *ctx, int64_t chat_id, const char *text);
    void (*send_with_keyboard)(void *ctx, int64_t chat_id, const char *text, 
                               int64_t file_id);
    void (*answer_callback)(void *ctx, const char *query_id, const char *text);
    void (*delete_message)(void *ctx, int64_t chat_id, int64_t message_id);
} MessageSender;

/* Update receiving abstraction */
typedef struct {
    void (*start_polling)(void *ctx);
    void (*stop_polling)(void *ctx);
} UpdatePoller;

/* Command handler depends on abstractions */
typedef struct {
    MessageSender *sender;
    FileRepository *repo;
    Processor *processor;
} CommandHandler;

CommandHandler *command_handler_new(MessageSender *sender, FileRepository *repo, 
                                    Processor *processor);
void command_handler_handle(CommandHandler *handler, int64_t chat_id, 
                            const char *text);
void command_handler_free(CommandHandler *handler);

#endif
```

**Benefits:**
- Each module has single responsibility
- Can test command routing with mock `MessageSender`
- Can test Telegram protocol with mock business logic
- Easier to add new messaging platforms (WhatsApp, Slack)

---

## 5. Missing Abstractions

### 5.1 No Prompt Repository

#### Location: `src/prompt_fetcher.c` (lines 73-117)

**Current state:**
```c
// prompt_fetcher.c:83 - Direct DB call
db_backend_get_prompts(fetcher->db, prompt_callback, fetcher);
```

**Problem:**
- Prompt fetching tightly coupled to database
- Cannot test without real database
- Cannot swap in file-based or HTTP-based prompt source

**Suggested refactoring:**
```c
// src/prompt_repository.h
typedef struct {
    int (*get_prompts)(void *ctx, db_prompts_cb cb, void *userdata);
    int (*update_prompt)(void *ctx, int64_t id, const char *content);
} PromptRepository;

PromptFetcher *prompt_fetcher_new(PromptRepository *repo, int interval_secs,
                                  prompt_change_cb cb, void *userdata);
```

---

### 5.2 No OCR Service Abstraction

#### Location: `src/processor.c` + `src/gemini.c`

**Current state:**
```c
// processor.c:157 - Direct Gemini call
char *ocr_text = gemini_extract_text(gemini, data, len, saved_filename);
```

**Problem:**
- Processor directly depends on Gemini client
- Cannot swap AI providers (e.g., AWS Textract, Azure OCR)
- Cannot mock OCR for testing

**Suggested refactoring:**
```c
// src/ocr_service.h
typedef struct {
    char* (*extract_text)(void *ctx, const uint8_t *data, size_t len, 
                          const char *filename);
    char* (*parse_receipt)(void *ctx, const char *ocr_text);
} OCRService;

// Processor uses abstraction
Processor *processor_new(FileRepository *repo, StorageBackend *storage, 
                         OCRService *ocr, DuplicateStrategyFn dup_strategy);
```

---

### 5.3 No Event/Notification System

#### Location: `src/bot.c` (lines 684-697)

**Current state:**
```c
// Direct function calls for notifications
bot_notify_startup(bot);
bot_notify_prompt_change(bot, prompt_name, new_content);
```

**Problem:**
- Cannot add notification channels (email, webhook, Slack) without modifying bot.c
- Cannot test notification logic in isolation
- No way to disable notifications in certain environments

**Suggested refactoring:**
```c
// src/event_bus.h
typedef enum {
    EVENT_BOT_STARTED,
    EVENT_PROMPT_CHANGED,
    EVENT_FILE_PROCESSED,
    EVENT_OCR_FAILED,
} EventType;

typedef void (*event_handler_t)(EventType type, const char *data, void *userdata);

typedef struct {
    void (*subscribe)(void *ctx, EventType type, event_handler_t handler, void *userdata);
    void (*publish)(void *ctx, EventType type, const char *data);
} EventBus;

// Bot publishes events
event_bus_publish(event_bus, EVENT_BOT_STARTED, NULL);

// Notification service subscribes
event_bus_subscribe(event_bus, EVENT_BOT_STARTED, on_bot_started, telegram_bot);
```

---

## 6. Testing Difficulties

### 6.1 processor.c Constructor (lines 36-52)

```c
Processor *processor_new(DBBackend *db, StorageBackend *storage, 
                         void *gemini, DuplicateStrategyFn dup_strategy) {
    // Requires ALL dependencies
}
```

**Testing problems:**
| Dependency | Required for Testing | Mock Available |
|------------|---------------------|----------------|
| `DBBackend` | Yes | ❌ No |
| `StorageBackend` | Yes | ❌ No |
| `GeminiClient` | Yes | ❌ No (void*) |
| `DuplicateStrategyFn` | Yes | ✅ Yes |

**Impact:** Cannot unit test processor without full infrastructure stack.

---

### 6.2 bot.c Constructor (lines 684-700)

```c
TgBot *bot_new(const Config *cfg, Processor *processor, 
               DBBackend *db, StorageBackend *storage) {
    // Requires ALL dependencies
}
```

**Testing problems:**
- Cannot test command handling without Telegram + Processor + DB + Storage
- Long-polling starts immediately in `bot_start()` - no isolated testing
- `dispatch_update()` is static - cannot test without full bot

---

### 6.3 No Dependency Injection

#### Location: `src/main.c` (lines 24-68)

**Current state:**
```c
int main(void) {
    Config cfg;
    config_load(&cfg);
    
    DBBackend *db = db_backend_open(&cfg);
    StorageBackend *storage = storage_backend_open(&cfg);
    GeminiClient *gemini = gemini_new(...);
    Processor *processor = processor_new(db, storage, gemini, ...);
    TgBot *bot = bot_new(&cfg, processor, db, storage);
    
    bot_notify_startup(bot);
    bot_start(bot);
    
    // Manual cleanup...
}
```

**Problem:**
- All dependencies wired manually in `main()`
- No way to inject test doubles
- Error handling is repetitive cleanup code

**Suggested refactoring:**
```c
// src/container.h - Simple DI container
typedef struct {
    Config *config;
    DBBackend *db;
    StorageBackend *storage;
    OCRService *ocr;
    FileRepository *repo;
    Processor *processor;
    MessageSender *telegram;
    CommandHandler *commands;
    TgBot *bot;
} AppContainer;

AppContainer *container_create(void);
int container_init_production(AppContainer *c);  /* Real implementations */
int container_init_testing(AppContainer *c);     /* Mock implementations */
void container_destroy(AppContainer *c);
```

---

## 7. Infrastructure Change Impact

### 7.1 Adding a New Storage Backend

**Current impact:**

| File | Changes Required |
|------|-----------------|
| `src/storage_backend.h` | Add enum value, extend `StorageBackendOps` |
| `src/storage_backend.c` | Extend factory function |
| `src/storage_new_backend.c` | Implement all 9 ops functions |
| `src/processor.c` | Test all storage calls (5 locations) |
| `CMakeLists.txt` | Add new source file |

**Risk:** Every storage operation in `processor.c` could be affected.

---

### 7.2 Adding a New Database Backend

**Current impact:**

| File | Changes Required |
|------|-----------------|
| `src/db_backend.h` | Add enum value, extend `DBBackendOps` |
| `src/db_backend.c` | Extend factory function |
| `src/db_new_backend.c` | Implement all 12 ops functions |
| `src/processor.c` | Test all DB calls (7 locations) |
| `src/prompt_fetcher.c` | Test direct DB calls |
| `src/bot.c` | Test direct DB calls in commands |

**Risk:** Business logic in `processor.c` knows about:
- Hash-based duplicate detection
- OCR status tracking
- JSON storage for parsed receipts

---

### 7.3 Changing Telegram API Protocol

**Current impact:**

| Change Type | Files Affected |
|-------------|----------------|
| New API endpoint | `bot.c` (URL construction + JSON) |
| Response format change | `bot.c` (JSON parsing) |
| New message type | `bot.c` (dispatch logic + handling) |
| Authentication change | `bot.c` (all HTTP calls) |

**Risk:** All Telegram API calls are inline in `bot.c` - any change touches the same file as command handling.

---

### 7.4 Adding New AI Provider

**Current impact:**

| Component | Changes Required |
|-----------|-----------------|
| New provider module | Create parallel to `gemini.c` (380 lines) |
| `src/processor.c` | Add provider selection logic |
| `src/config.h` | Add provider config fields |
| `src/config.c` | Parse new config fields |
| `src/main.c` | Wire up new provider |

**Why it's hard:**
- Prompts embedded in `gemini.c` (lines 97-157)
- `processor.c` directly calls `gemini_extract_text()` and `gemini_parse_receipt()`
- No `OCRService` abstraction

---

## Summary Matrix

| Issue | Location | Severity | Testability | Change Impact |
|-------|----------|----------|-------------|---------------|
| Direct DB calls | processor.c:114-247 | 🔴 High | ❌ Cannot unit test | 🔴 High |
| Direct storage calls | processor.c:119-295 | 🔴 High | ❌ Cannot unit test | 🟡 Medium |
| Hardcoded URLs | bot.c:17-18, gemini.c:14 | 🟡 Medium | ⚠️ Mock at HTTP level | 🟢 Low |
| Hardcoded limits | bot.c:20-28 | 🟢 Low | ⚠️ Recompilation needed | 🟢 Low |
| Monolithic bot.c | bot.c:1-799 | 🔴 High | ❌ Cannot isolate | 🔴 High |
| No prompt repository | prompt_fetcher.c:73-117 | 🟡 Medium | ❌ Requires DB | 🟡 Medium |
| No OCR abstraction | processor.c + gemini.c | 🔴 High | ❌ Cannot mock AI | 🔴 High |
| No event system | bot.c:684-697 | 🟢 Low | ⚠️ Hard to extend | 🟢 Low |
| Manual DI | main.c:24-68 | 🟡 Medium | ❌ Integration only | 🟡 Medium |

---

## Recommended Refactoring Priority

### Phase 1: Foundation (Highest Impact)

1. **Extract Repository interfaces** for DB operations
   - Create `FileRepository` abstraction
   - Implement `file_repository_db_backend()`
   - Update `processor.c` to use repository
   - Create `file_repository_memory()` for tests

2. **Split bot.c** into focused modules
   - `bot_telegram.c` - Protocol adapter
   - `bot_commands.c` - Command business logic
   - `bot_polling.c` - Long-polling loop
   - Define `MessageSender` interface

### Phase 2: Testability

3. **Add configuration** for URLs, timeouts, limits
   - Move constants to `Config` struct
   - Add environment variable support
   - Update `bot.c`, `gemini.c` to use config

4. **Create OCR Service abstraction**
   - Define `OCRService` interface
   - Wrap `GeminiClient` implementation
   - Enable future AI provider swaps

### Phase 3: Extensibility

5. **Introduce Document Store**
   - Higher-level abstraction over `StorageBackend`
   - Handle document lifecycle (save, read, delete)
   - Move cleanup logic from processor

6. **Add Event System**
   - Create `EventBus` abstraction
   - Replace direct notification calls
   - Enable multiple notification channels

### Phase 4: Infrastructure

7. **Create DI Container**
   - Simple container for app composition
   - Support production and testing configurations
   - Reduce boilerplate in `main.c`

8. **Build Mock Implementations**
   - `file_repository_memory()`
   - `message_sender_mock()`
   - `ocr_service_mock()`
   - Enable comprehensive unit testing

---

## Estimated Effort

| Phase | Tasks | Effort | Risk Reduction |
|-------|-------|--------|----------------|
| Phase 1 | Repository + bot split | 3-4 days | 40% |
| Phase 2 | Config + OCR abstraction | 2-3 days | 25% |
| Phase 3 | Document store + events | 2-3 days | 20% |
| Phase 4 | DI container + mocks | 2-3 days | 15% |
| **Total** | **8 modules, 5 interfaces** | **9-13 days** | **100%** |

---

## Conclusion

The kaufbot codebase has good foundational abstractions (`StorageBackend`, `DBBackend`), but business logic modules (`processor.c`, `bot.c`) are tightly coupled to infrastructure. This makes testing difficult and infrastructure changes risky.

**Immediate actions:**
1. Extract `FileRepository` to decouple processor from database
2. Split `bot.c` to enable isolated testing
3. Add configuration for operational parameters

**Long-term benefits:**
- Unit test coverage >80%
- Infrastructure changes without business logic modifications
- Easier onboarding (clear module boundaries)
- Multiple AI providers, storage backends, notification channels
