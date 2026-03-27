/* ── Event Bus tests ──────────────────────────────────────────────────────── */

#include "event_bus.h"
#include "test_runner.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Test helpers ─────────────────────────────────────────────────────────── */

static int g_handler1_called = 0;
static int g_handler2_called = 0;
static char g_handler1_data[256] = {0};
static EventType g_handler1_type = EVENT_BOT_STARTED;

static void reset_test_state(void) {
    g_handler1_called = 0;
    g_handler2_called = 0;
    memset(g_handler1_data, 0, sizeof(g_handler1_data));
    g_handler1_type = EVENT_BOT_STARTED;
}

static void test_handler1(EventType type, const char *data, void *userdata) {
    (void)userdata;
    g_handler1_called++;
    g_handler1_type = type;
    if (data) {
        snprintf(g_handler1_data, sizeof(g_handler1_data), "%s", data);
    }
}

static void test_handler2(EventType type, const char *data, void *userdata) {
    (void)type;
    (void)data;
    (void)userdata;
    g_handler2_called++;
}

/* ── Lifecycle Tests ──────────────────────────────────────────────────────── */

TEST_CASE(event_bus_create_and_free) {
    EventBus *bus = event_bus_new();
    ASSERT_NOT_NULL(bus);

    event_bus_free(bus);
    event_bus_free(NULL);
    TEST_PASS();
}

/* ── Subscribe/Publish Tests ──────────────────────────────────────────────── */

TEST_CASE(event_bus_publish_no_subscribers) {
    EventBus *bus = event_bus_new();
    ASSERT_NOT_NULL(bus);

    /* Should not crash when publishing with no subscribers */
    event_bus_publish(bus, EVENT_BOT_STARTED, NULL);

    event_bus_free(bus);
    TEST_PASS();
}

TEST_CASE(event_bus_subscribe_and_publish) {
    reset_test_state();
    EventBus *bus = event_bus_new();
    ASSERT_NOT_NULL(bus);

    int rc = event_bus_subscribe(bus, EVENT_BOT_STARTED, test_handler1, NULL);
    ASSERT_EQ(0, rc);

    event_bus_publish(bus, EVENT_BOT_STARTED, "test_data");

    ASSERT_EQ(1, g_handler1_called);
    ASSERT_STR_EQ("test_data", g_handler1_data);
    ASSERT_EQ(EVENT_BOT_STARTED, g_handler1_type);

    event_bus_free(bus);
    TEST_PASS();
}

TEST_CASE(event_bus_multiple_subscribers) {
    reset_test_state();
    EventBus *bus = event_bus_new();
    ASSERT_NOT_NULL(bus);

    event_bus_subscribe(bus, EVENT_BOT_STARTED, test_handler1, NULL);
    event_bus_subscribe(bus, EVENT_BOT_STARTED, test_handler2, NULL);

    event_bus_publish(bus, EVENT_BOT_STARTED, NULL);

    ASSERT_EQ(1, g_handler1_called);
    ASSERT_EQ(1, g_handler2_called);

    event_bus_free(bus);
    TEST_PASS();
}

TEST_CASE(event_bus_different_event_types) {
    reset_test_state();
    EventBus *bus = event_bus_new();
    ASSERT_NOT_NULL(bus);

    event_bus_subscribe(bus, EVENT_BOT_STARTED, test_handler1, NULL);
    event_bus_subscribe(bus, EVENT_PROMPT_CHANGED, test_handler2, NULL);

    /* Only EVENT_BOT_STARTED should trigger handler1 */
    event_bus_publish(bus, EVENT_BOT_STARTED, NULL);
    ASSERT_EQ(1, g_handler1_called);
    ASSERT_EQ(0, g_handler2_called);

    /* Only EVENT_PROMPT_CHANGED should trigger handler2 */
    event_bus_publish(bus, EVENT_PROMPT_CHANGED, NULL);
    ASSERT_EQ(1, g_handler1_called);
    ASSERT_EQ(1, g_handler2_called);

    event_bus_free(bus);
    TEST_PASS();
}

TEST_CASE(event_bus_multiple_publishes) {
    reset_test_state();
    EventBus *bus = event_bus_new();

    event_bus_subscribe(bus, EVENT_FILE_PROCESSED, test_handler1, NULL);

    event_bus_publish(bus, EVENT_FILE_PROCESSED, "file1");
    event_bus_publish(bus, EVENT_FILE_PROCESSED, "file2");
    event_bus_publish(bus, EVENT_FILE_PROCESSED, "file3");

    ASSERT_EQ(3, g_handler1_called);

    event_bus_free(bus);
    TEST_PASS();
}

/* ── Unsubscribe Tests ────────────────────────────────────────────────────── */

TEST_CASE(event_bus_unsubscribe) {
    reset_test_state();
    EventBus *bus = event_bus_new();

    event_bus_subscribe(bus, EVENT_BOT_STARTED, test_handler1, NULL);
    event_bus_subscribe(bus, EVENT_BOT_STARTED, test_handler2, NULL);

    /* Unsubscribe handler1 */
    int rc = event_bus_unsubscribe(bus, EVENT_BOT_STARTED, test_handler1);
    ASSERT_EQ(0, rc);

    event_bus_publish(bus, EVENT_BOT_STARTED, NULL);

    ASSERT_EQ(0, g_handler1_called);
    ASSERT_EQ(1, g_handler2_called);

    event_bus_free(bus);
    TEST_PASS();
}

TEST_CASE(event_bus_unsubscribe_not_found) {
    EventBus *bus = event_bus_new();

    int rc = event_bus_unsubscribe(bus, EVENT_BOT_STARTED, test_handler1);
    ASSERT_EQ(-1, rc);

    event_bus_free(bus);
    TEST_PASS();
}

/* ── Slot Reuse Tests ─────────────────────────────────────────────────────── */

static void dummy_handler(EventType type, const char *data, void *userdata) {
    (void)type;
    (void)data;
    (void)userdata;
}

TEST_CASE(event_bus_subscribe_unsubscribe_reuse) {
    reset_test_state();
    EventBus *bus = event_bus_new();

    /* Subscribe max subscribers */
    for (int i = 0; i < EVENT_BUS_MAX_SUBSCRIBERS; i++) {
        ASSERT_EQ(0, event_bus_subscribe(bus, EVENT_BOT_STARTED, dummy_handler, NULL));
    }

    /* Unsubscribe one */
    ASSERT_EQ(0, event_bus_unsubscribe(bus, EVENT_BOT_STARTED, dummy_handler));

    /* Should be able to subscribe again (slot reused) */
    ASSERT_EQ(0, event_bus_subscribe(bus, EVENT_BOT_STARTED, test_handler1, NULL));

    event_bus_publish(bus, EVENT_BOT_STARTED, NULL);
    ASSERT_EQ(1, g_handler1_called);

    event_bus_free(bus);
    TEST_PASS();
}

/* ── Error Handling Tests ─────────────────────────────────────────────────── */

TEST_CASE(event_bus_subscribe_null_args) {
    EventBus *bus = event_bus_new();

    ASSERT_EQ(-1, event_bus_subscribe(NULL, EVENT_BOT_STARTED, test_handler1, NULL));
    ASSERT_EQ(-1, event_bus_subscribe(bus, EVENT_BOT_STARTED, NULL, NULL));

    event_bus_free(bus);
    TEST_PASS();
}

TEST_CASE(event_bus_publish_null) {
    /* Should not crash */
    event_bus_publish(NULL, EVENT_BOT_STARTED, NULL);
    TEST_PASS();
}

TEST_CASE(event_bus_unsubscribe_null) {
    ASSERT_EQ(-1, event_bus_unsubscribe(NULL, EVENT_BOT_STARTED, test_handler1));
    TEST_PASS();
}
