#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#include "bot.h"
#include "config.h"
#include "db_backend.h"
#include "gemini.h"
#include "processor.h"
#include "storage_backend.h"

static TgBot *g_bot = NULL;

static void on_signal(int sig) {
    (void)sig;
    if (g_bot)
        bot_stop(g_bot); // NOLINT (atomic_store is async-signal-safe)
}

int main(void) {
    LOG_INFO("bot starting up");

    Config cfg;
    if (config_load(&cfg) != 0) {
        LOG_ERROR("failed to load config");
        return EXIT_FAILURE;
    }

    StorageBackend *storage = storage_backend_open(&cfg);
    if (!storage) {
        LOG_ERROR("failed to open storage backend");
        return EXIT_FAILURE;
    }

    if (storage_backend_ensure_dirs(storage) != 0) {
        LOG_ERROR("failed to initialize storage");
        storage_backend_close(storage);
        return EXIT_FAILURE;
    }

    DBBackend *db = db_backend_open(&cfg);
    if (!db) {
        LOG_ERROR("failed to open database backend");
        storage_backend_close(storage);
        return EXIT_FAILURE;
    }

    GeminiClient *gemini = gemini_new(cfg.gemini_api_key, cfg.gemini_model);
    if (!gemini) {
        LOG_ERROR("failed to create Gemini client");
        db_backend_close(db);
        storage_backend_close(storage);
        return EXIT_FAILURE;
    }

    Processor *processor = processor_new(db, storage, gemini, NULL);
    if (!processor) {
        LOG_ERROR("failed to create processor");
        gemini_free(gemini);
        db_backend_close(db);
        storage_backend_close(storage);
        return EXIT_FAILURE;
    }

    TgBot *bot = bot_new(&cfg, processor, db, storage);
    if (!bot) {
        LOG_ERROR("failed to create bot");
        processor_free(processor);
        gemini_free(gemini);
        db_backend_close(db);
        storage_backend_close(storage);
        return EXIT_FAILURE;
    }

    g_bot = bot;
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    bot_notify_startup(bot);
    bot_start(bot);

    bot_free(bot);
    processor_free(processor);
    gemini_free(gemini);
    db_backend_close(db);
    storage_backend_close(storage);

    LOG_INFO("clean shutdown complete");
    return EXIT_SUCCESS;
}
