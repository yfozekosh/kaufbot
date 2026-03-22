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
    fprintf(stderr, "\n[main] Signal received, shutting down...\n");
    if (g_bot) bot_stop(g_bot);
}

int main(void)
{
    /* ── Config ─────────────────────────────────────────────────────────── */
    Config cfg;
    if (config_load(&cfg) != 0) {
        fprintf(stderr, "[main] Failed to load config\n");
        return EXIT_FAILURE;
    }
    printf("[main] Config loaded. Model: %s\n", cfg.gemini_model);
    printf("[main] Storage: %s  |  DB: %s\n", cfg.storage_path, cfg.db_path);
    printf("[main] Allowed users: %d\n", cfg.allowed_users_count);

    /* ── Storage dirs ────────────────────────────────────────────────────── */
    if (storage_ensure_dirs(cfg.storage_path) != 0) {
        fprintf(stderr, "[main] Failed to create storage directory: %s\n",
                cfg.storage_path);
        return EXIT_FAILURE;
    }

    /* ── Database ────────────────────────────────────────────────────────── */
    DB *db = db_open(cfg.db_path);
    if (!db) {
        fprintf(stderr, "[main] Failed to open database: %s\n", cfg.db_path);
        return EXIT_FAILURE;
    }
    printf("[main] Database opened: %s\n", cfg.db_path);

    /* ── Gemini client ───────────────────────────────────────────────────── */
    GeminiClient *gemini = gemini_new(cfg.gemini_api_key, cfg.gemini_model);
    if (!gemini) {
        fprintf(stderr, "[main] Failed to create Gemini client\n");
        db_close(db);
        return EXIT_FAILURE;
    }

    /* ── Processor ───────────────────────────────────────────────────────── */
    /* Pass NULL for strategy → uses default strategy_notify_and_skip */
    Processor *processor = processor_new(db, cfg.storage_path, gemini, NULL);
    if (!processor) {
        fprintf(stderr, "[main] Failed to create processor\n");
        gemini_free(gemini);
        db_close(db);
        return EXIT_FAILURE;
    }

    /* ── Bot ─────────────────────────────────────────────────────────────── */
    TgBot *bot = bot_new(&cfg, processor);
    if (!bot) {
        fprintf(stderr, "[main] Failed to create bot\n");
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

    printf("[main] Clean shutdown complete.\n");
    return EXIT_SUCCESS;
}
