# Kaufbot

Telegram bot that processes receipt photos — extracts text via OCR, parses line items, and stores results. Built in C for minimal footprint.

## Features

- Receipt photo upload via Telegram
- OCR extraction using Google Gemini
- Receipt parsing (store name, line items, totals, payment info)
- Duplicate detection via SHA-256 hashing
- Pluggable storage backends (local filesystem, Supabase)
- Pluggable database backends (SQLite, PostgreSQL)
- Docker support (static binary, scratch image)

## Architecture

```
┌─────────┐     ┌──────────┐     ┌─────────┐
│ Telegram │────▶│   Bot    │────▶│ Gemini  │  (OCR + parsing)
│  API     │◀────│          │◀────│  API    │
└─────────┘     └────┬─────┘     └─────────┘
                     │
              ┌──────┴──────┐
              │  Processor  │  (dedup, save, parse)
              └──────┬──────┘
               ┌─────┴─────┐
               │           │
         ┌─────┴──┐   ┌────┴────┐
         │Storage │   │Database │
         │Backend │   │Backend  │
         └────────┘   └─────────┘
         local/        sqlite/
         supabase      postgres
```

## Quick Start

### 1. Prerequisites

- C compiler (GCC/Clang)
- CMake 3.16+
- libcurl
- SQLite3
- PostgreSQL client library (optional, for Supabase/PostgreSQL)

**Fedora:**
```bash
sudo dnf install cmake gcc libcurl-devel sqlite-devel postgresql-devel
```

**Debian/Ubuntu:**
```bash
sudo apt install cmake gcc libcurl4-openssl-dev libsqlite3-dev libpq-dev
```

### 2. Configure

```bash
cp .env.example .env
# Edit .env with your credentials
```

Required:
| Variable | Description |
|----------|-------------|
| `TELEGRAM_TOKEN` | Bot token from [@BotFather](https://t.me/BotFather) |
| `GEMINI_API_KEY` | API key from [Google AI Studio](https://makersuite.google.com/app/apikey) |
| `ALLOWED_USER_IDS` | Comma-separated Telegram user IDs |

Optional (storage):
| Variable | Default | Description |
|----------|---------|-------------|
| `STORAGE_BACKEND` | `local` | `local` or `supabase` |
| `STORAGE_PATH` | `/data/files` | Local file storage path |
| `SUPABASE_URL` | — | Supabase project URL |
| `SUPABASE_ANON_KEY` | — | Supabase anon/publishable key |
| `SUPABASE_SERVICE_KEY` | — | Supabase service role key (bypasses RLS) |
| `SUPABASE_BUCKET` | `uploads` | Storage bucket name |

Optional (database):
| Variable | Default | Description |
|----------|---------|-------------|
| `DB_BACKEND` | `sqlite` | `sqlite` or `postgres` |
| `DB_PATH` | `/data/bot.db` | SQLite database path |
| `POSTGRES_HOST` | — | PostgreSQL host |
| `POSTGRES_PORT` | `5432` | PostgreSQL port |
| `POSTGRES_DB` | — | Database name |
| `POSTGRES_USER` | — | Database user |
| `POSTGRES_PASSWORD` | — | Database password |

### 3. Build & Run

```bash
./build.sh          # configure + compile
source .env && ./build/tgbot   # run
```

Or use the convenience script:
```bash
./run.sh
```

### 4. Test

```bash
./test.sh           # run test suite
```

### 5. Docker

```bash
docker build -t kaufbot .
docker run -v /path/to/data:/data kaufbot
```

## Usage

Send a photo to the bot on Telegram. It will:
1. Download the image
2. Check for duplicates (by SHA-256 hash)
3. Send to Gemini for OCR
4. Parse receipt data (store, items, totals)
5. Save file and OCR result to storage
6. Reply with parsed receipt summary

Commands:
- `/start` — welcome message
- `/list` — show recent receipts

## Project Structure

```
├── src/
│   ├── main.c              # entry point, lifecycle
│   ├── bot.c / bot.h       # Telegram bot (polling, commands)
│   ├── processor.c / .h    # orchestration (hash, save, OCR, parse)
│   ├── gemini.c / .h       # Gemini API client (OCR + parsing)
│   ├── config.c / .h       # environment variable loading
│   ├── db_backend.h        # database backend interface
│   ├── db.h                # data types (FileRecord, ParsedReceipt)
│   ├── db_sqlite.c         # SQLite implementation
│   ├── db_postgres.c       # PostgreSQL implementation
│   ├── storage_backend.h   # storage backend interface
│   ├── storage.h           # storage utilities (SHA-256, filename, MIME)
│   ├── storage_local.c     # local filesystem implementation
│   └── storage_supabase.c  # Supabase Storage implementation
├── tests/
│   ├── test_db.c           # database tests
│   ├── test_json.c         # JSON parsing + Gemini mock tests
│   ├── test_storage.c      # storage utility tests
│   ├── test_config.c       # config loading tests
│   ├── test_edge_cases.c   # boundary/edge case tests
│   └── test_runner.h       # minimal test framework
├── third_party/cjson/      # cJSON library (vendored)
├── migrations/             # database schema files
├── build.sh                # build script
├── test.sh                 # test script
├── run.sh                  # run script
├── CMakeLists.txt          # build configuration
├── Dockerfile              # multi-stage Docker build
├── .env.example            # configuration template
├── RULES.md                # coding conventions
├── CODE_SMELLS.md          # code quality audit
└── NEXT_STEPS.md           # improvement roadmap
```

## Development

### Build Options

```bash
# Debug build with tests
cmake -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON -S . -B build
cmake --build build

# Run tests
cd build && ctest --output-on-failure
```

### Adding a New Backend

1. Implement the interface (`StorageBackendOps` or `DBBackendOps`)
2. Add the `_open` factory function
3. Register in the `*_open` factory in `storage_backend.h` or `db_backend.h`

### Coding Conventions

See [RULES.md](RULES.md). Key points:
- Write tests alongside new code
- Never suppress compiler warnings
- Treat all `-Wall -Wextra -Wpedantic` warnings as errors
- 70% minimum code coverage target

## License

Private project.
