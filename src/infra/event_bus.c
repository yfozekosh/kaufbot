#include "event_bus.h"

#include <stdlib.h>
#include <string.h>

/* ── Subscriber Entry ─────────────────────────────────────────────────────── */

typedef struct {
    event_handler_t handler;
    void *userdata;
    int active;
} Subscriber;

/* ── Internal Structure ───────────────────────────────────────────────────── */

typedef struct {
    Subscriber subscribers[EVENT_COUNT][EVENT_BUS_MAX_SUBSCRIBERS];
    int counts[EVENT_COUNT];
} SimpleEventBus;

/* ── Implementation ───────────────────────────────────────────────────────── */

static int bus_subscribe(void *ctx, EventType type, event_handler_t handler, void *userdata) {
    if (!ctx || !handler || type >= EVENT_COUNT)
        return -1;

    SimpleEventBus *bus = (SimpleEventBus *)ctx;
    if (bus->counts[type] >= EVENT_BUS_MAX_SUBSCRIBERS)
        return -1;

    Subscriber *sub = &bus->subscribers[type][bus->counts[type]];
    sub->handler = handler;
    sub->userdata = userdata;
    sub->active = 1;
    bus->counts[type]++;

    return 0;
}

static int bus_unsubscribe(void *ctx, EventType type, event_handler_t handler) {
    if (!ctx || !handler || type >= EVENT_COUNT)
        return -1;

    SimpleEventBus *bus = (SimpleEventBus *)ctx;

    for (int i = 0; i < bus->counts[type]; i++) {
        if (bus->subscribers[type][i].handler == handler) {
            /* Shift remaining subscribers down to reclaim slot */
            for (int j = i; j < bus->counts[type] - 1; j++) {
                bus->subscribers[type][j] = bus->subscribers[type][j + 1];
            }
            bus->counts[type]--;
            /* Clear the vacated slot */
            memset(&bus->subscribers[type][bus->counts[type]], 0, sizeof(Subscriber));
            return 0;
        }
    }
    return -1;
}

static void bus_publish(void *ctx, EventType type, const char *data) {
    if (!ctx || type >= EVENT_COUNT)
        return;

    SimpleEventBus *bus = (SimpleEventBus *)ctx;

    for (int i = 0; i < bus->counts[type]; i++) {
        Subscriber *sub = &bus->subscribers[type][i];
        if (sub->active && sub->handler) {
            sub->handler(type, data, sub->userdata);
        }
    }
}

/* ── VTable ───────────────────────────────────────────────────────────────── */

static const EventBusOps bus_ops = {
    .subscribe = bus_subscribe,
    .unsubscribe = bus_unsubscribe,
    .publish = bus_publish,
};

/* ── Public API ───────────────────────────────────────────────────────────── */

EventBus *event_bus_new(void) {
    SimpleEventBus *impl = calloc(1, sizeof(SimpleEventBus));
    if (!impl)
        return NULL;

    EventBus *bus = calloc(1, sizeof(EventBus));
    if (!bus) {
        free(impl);
        return NULL;
    }

    bus->ops = &bus_ops;
    bus->internal = impl;

    return bus;
}

void event_bus_free(EventBus *bus) {
    if (!bus)
        return;
    free(bus->internal);
    free(bus);
}
