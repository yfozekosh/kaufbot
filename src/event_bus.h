#ifndef EVENT_BUS_H
#define EVENT_BUS_H

#include <stddef.h>

/* ── Event Types ──────────────────────────────────────────────────────────── */

typedef enum {
    EVENT_BOT_STARTED = 0,
    EVENT_PROMPT_CHANGED,
    EVENT_FILE_PROCESSED,
    EVENT_OCR_FAILED,
    EVENT_FILE_DELETED,
    EVENT_COUNT /* Keep last - used for array sizing */
} EventType;

/* ── Event Handler Type ───────────────────────────────────────────────────── */

typedef void (*event_handler_t)(EventType type, const char *data, void *userdata);

/* ── Event Bus Interface ──────────────────────────────────────────────────── */

typedef struct EventBus EventBus;

typedef struct {
    /* Subscribe to an event type. Returns 0 on success. */
    int (*subscribe)(void *ctx, EventType type, event_handler_t handler, void *userdata);

    /* Unsubscribe from an event type. Returns 0 on success. */
    int (*unsubscribe)(void *ctx, EventType type, event_handler_t handler);

    /* Publish an event. Calls all registered handlers for the event type. */
    void (*publish)(void *ctx, EventType type, const char *data);
} EventBusOps;

struct EventBus {
    const EventBusOps *ops;
    void *internal;
};

/* ── Convenience Wrappers ─────────────────────────────────────────────────── */

static inline int event_bus_subscribe(EventBus *bus, EventType type, event_handler_t handler,
                                      void *userdata) {
    if (!bus || !bus->ops || !handler)
        return -1;
    return bus->ops->subscribe(bus->internal, type, handler, userdata);
}

static inline int event_bus_unsubscribe(EventBus *bus, EventType type, event_handler_t handler) {
    if (!bus || !bus->ops || !handler)
        return -1;
    return bus->ops->unsubscribe(bus->internal, type, handler);
}

static inline void event_bus_publish(EventBus *bus, EventType type, const char *data) {
    if (bus && bus->ops && bus->ops->publish) {
        bus->ops->publish(bus->internal, type, data);
    }
}

/* ── Default Implementation ───────────────────────────────────────────────── */

/* Maximum subscribers per event type */
#define EVENT_BUS_MAX_SUBSCRIBERS 8

/* Create a new event bus */
EventBus *event_bus_new(void);

/* Free the event bus */
void event_bus_free(EventBus *bus);

#endif /* EVENT_BUS_H */
