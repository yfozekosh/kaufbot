#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bot.h"
#include "config.h"
#include "db_backend.h"
#include "file_repository.h"
#include "ocr_service.h"
#include "processor.h"
#include "storage_backend.h"

/* ── Application Container ────────────────────────────────────────────────── */

typedef struct {
    Config cfg;
    StorageBackend *storage;
    DBBackend *db;
    FileRepository *repo;
    OCRService *ocr;
    Processor *processor;
    TgBot *bot;
} AppContainer;

static TgBot *g_bot = NULL;

static void on_signal(int sig) {
    (void)sig;
    if (g_bot)
        bot_stop(g_bot); /* NOLINT (atomic_store is async-signal-safe) */
}

static void container_destroy(AppContainer *c) {
    if (!c)
        return;
    if (c->bot) {
        bot_free(c->bot);
        c->bot = NULL;
    }
    if (c->processor) {
        processor_free(c->processor);
        c->processor = NULL;
    }
    if (c->ocr) {
        ocr_service_free(c->ocr);
        c->ocr = NULL;
    }
    if (c->repo) {
        file_repository_free(c->repo);
        c->repo = NULL;
    }
    if (c->db) {
        db_backend_close(c->db);
        c->db = NULL;
    }
    if (c->storage) {
        storage_backend_close(c->storage);
        c->storage = NULL;
    }
}

static int container_init_infra(AppContainer *c) {
    if (config_load(&c->cfg) != 0) {
        LOG_ERROR("failed to load config");
        return -1;
    }

    c->storage = storage_backend_open(&c->cfg);
    if (!c->storage) {
        LOG_ERROR("failed to open storage backend");
        return -1;
    }

    if (storage_backend_ensure_dirs(c->storage) != 0) {
        LOG_ERROR("failed to initialize storage");
        return -1;
    }

    c->db = db_backend_open(&c->cfg);
    if (!c->db) {
        LOG_ERROR("failed to open database backend");
        return -1;
    }
    return 0;
}

static int container_init_services(AppContainer *c) {
    c->repo = file_repository_db_backend(c->db, c->storage);
    if (!c->repo) {
        LOG_ERROR("failed to create file repository");
        return -1;
    }
    c->ocr = ocr_service_gemini_new(c->cfg.gemini_api_key, c->cfg.gemini_model,
                                    c->cfg.gemini_fallback_model, c->cfg.gemini_fallback_enabled,
                                    c->cfg.gemini_api_base, c->cfg.gemini_http_timeout_secs);
    if (!c->ocr) {
        LOG_ERROR("failed to create OCR service");
        return -1;
    }

    c->processor = processor_new(c->repo, c->storage, c->ocr, NULL);
    if (!c->processor) {
        LOG_ERROR("failed to create processor");
        return -1;
    }

    c->bot = bot_new(&c->cfg, c->processor, c->db, c->storage);
    if (!c->bot) {
        LOG_ERROR("failed to create bot");
        return -1;
    }

    return 0;
}

static int container_init(AppContainer *c) {
    if (container_init_infra(c) != 0)
        return -1;
    return container_init_services(c);
}

int main(void) {
    LOG_INFO("bot starting up");

    AppContainer container;
    memset(&container, 0, sizeof(container));

    if (container_init(&container) != 0) {
        container_destroy(&container);
        return EXIT_FAILURE;
    }

    g_bot = container.bot;
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    bot_notify_startup(container.bot);
    bot_start(container.bot);

    container_destroy(&container);

    LOG_INFO("clean shutdown complete");
    return EXIT_SUCCESS;
}
