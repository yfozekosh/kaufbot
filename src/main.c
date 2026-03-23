#include <stdio.h>
#include <stdlib.h>
#include <signal.h>

#include "config.h"
#include "db.h"
#include "storage.h"
#include "gemini.h"
#include "processor.h"
#include "bot.h"

static TgBot *g_bot = NULL;

static void on_signal(int sig)
{
    (void)sig;
    LOG_INFO("signal received, shutting down...");
    if (g_bot) bot_stop(g_bot);
}

int main(void)
{
    LOG_INFO("bot starting up");
    
    /* ── Config ─────────────────────────────────────────────────────────── */
    Config cfg;
    if (config_load(&cfg) != 0) {
        LOG_ERROR("failed to load config");
        return EXIT_FAILURE;
    }
    LOG_INFO("config loaded. Model: %s", cfg.gemini_model);
    LOG_INFO("storage: %s  |  DB: %s", cfg.storage_path, cfg.db_path);
    LOG_INFO("allowed users: %d", cfg.allowed_users_count);

    /* ── Storage dirs ────────────────────────────────────────────────────── */
    if (storage_ensure_dirs(cfg.storage_path) != 0) {
        LOG_ERROR("failed to create storage directory: %s", cfg.storage_path);
        return EXIT_FAILURE;
    }

    /* ── Database ────────────────────────────────────────────────────────── */
    DB *db = db_open(cfg.db_path);
    if (!db) {
        LOG_ERROR("failed to open database: %s", cfg.db_path);
        return EXIT_FAILURE;
    }
    LOG_INFO("database opened: %s", cfg.db_path);

    /* ── Gemini client ───────────────────────────────────────────────────── */
    GeminiClient *gemini = gemini_new(cfg.gemini_api_key, cfg.gemini_model);
    if (!gemini) {
        LOG_ERROR("failed to create Gemini client");
        db_close(db);
        return EXIT_FAILURE;
    }

    /* ── Processor ───────────────────────────────────────────────────────── */
    /* Pass NULL for strategy → uses default strategy_notify_and_skip */
    Processor *processor = processor_new(db, cfg.storage_path, gemini, NULL);
    if (!processor) {
        LOG_ERROR("failed to create processor");
        gemini_free(gemini);
        db_close(db);
        return EXIT_FAILURE;
    }

    /* ── Bot ─────────────────────────────────────────────────────────────── */
    TgBot *bot = bot_new(&cfg, processor, db);
    if (!bot) {
        LOG_ERROR("failed to create bot");
        processor_free(processor);
        gemini_free(gemini);
        db_close(db);
        return EXIT_FAILURE;
    }

    /* ── Signal handlers ─────────────────────────────────────────────────── */
    g_bot = bot;
    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);

    /* ── Run ─────────────────────────────────────────────────────────────── */
    bot_start(bot); /* blocks until bot_stop() */

    /* ── Cleanup ─────────────────────────────────────────────────────────── */
    bot_free(bot);
    processor_free(processor);
    gemini_free(gemini);
    db_close(db);

    LOG_INFO("clean shutdown complete");
    return EXIT_SUCCESS;
}
