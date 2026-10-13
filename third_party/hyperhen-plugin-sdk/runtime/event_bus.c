#include "onion/event_bus.h"

#include <pthread.h>
#include <stdlib.h>

typedef struct event_subscription {
    onion_event_subscription token;
    onion_event_id event;
    onion_event_callback callback;
    void *user_data;
    struct event_subscription *next;
} event_subscription;

struct onion_event_bus {
    pthread_mutex_t mutex;
    event_subscription *subscriptions;
    onion_event_subscription next_token;
};

onion_status onion_event_bus_create(onion_event_bus **out_bus) {
    if (!out_bus) return ONION_E_INVALID_ARGUMENT;
    onion_event_bus *bus = (onion_event_bus *)calloc(1, sizeof(*bus));
    if (!bus) return ONION_E_NO_MEMORY;
    if (pthread_mutex_init(&bus->mutex, NULL) != 0) {
        free(bus);
        return ONION_E_IO;
    }
    bus->next_token = 1;
    *out_bus = bus;
    return ONION_OK;
}

void onion_event_bus_destroy(onion_event_bus *bus) {
    if (!bus) return;
    pthread_mutex_lock(&bus->mutex);
    event_subscription *current = bus->subscriptions;
    bus->subscriptions = NULL;
    pthread_mutex_unlock(&bus->mutex);
    while (current) {
        event_subscription *next = current->next;
        free(current);
        current = next;
    }
    pthread_mutex_destroy(&bus->mutex);
    free(bus);
}

onion_status onion_event_subscribe(onion_event_bus *bus, onion_event_id event,
                                   onion_event_callback callback, void *user_data,
                                   onion_event_subscription *out_subscription) {
    if (!bus || !callback || !out_subscription || event == 0) return ONION_E_INVALID_ARGUMENT;
    event_subscription *subscription = (event_subscription *)calloc(1, sizeof(*subscription));
    if (!subscription) return ONION_E_NO_MEMORY;
    subscription->event = event;
    subscription->callback = callback;
    subscription->user_data = user_data;
    pthread_mutex_lock(&bus->mutex);
    subscription->token = bus->next_token++;
    subscription->next = bus->subscriptions;
    bus->subscriptions = subscription;
    pthread_mutex_unlock(&bus->mutex);
    *out_subscription = subscription->token;
    return ONION_OK;
}

onion_status onion_event_unsubscribe(onion_event_bus *bus,
                                     onion_event_subscription token) {
    if (!bus || token == 0) return ONION_E_INVALID_ARGUMENT;
    pthread_mutex_lock(&bus->mutex);
    event_subscription **cursor = &bus->subscriptions;
    while (*cursor && (*cursor)->token != token) cursor = &(*cursor)->next;
    if (!*cursor) {
        pthread_mutex_unlock(&bus->mutex);
        return ONION_E_NOT_FOUND;
    }
    event_subscription *removed = *cursor;
    *cursor = removed->next;
    pthread_mutex_unlock(&bus->mutex);
    free(removed);
    return ONION_OK;
}

onion_status onion_event_publish(onion_event_bus *bus, onion_event_id event,
                                 const void *data, size_t data_size) {
    if (!bus || event == 0 || (data_size != 0 && !data)) return ONION_E_INVALID_ARGUMENT;
    size_t count = 0;
    pthread_mutex_lock(&bus->mutex);
    for (event_subscription *s = bus->subscriptions; s; s = s->next)
        if (s->event == event) count++;
    event_subscription *snapshot = count ? calloc(count, sizeof(*snapshot)) : NULL;
    if (count && !snapshot) {
        pthread_mutex_unlock(&bus->mutex);
        return ONION_E_NO_MEMORY;
    }
    size_t index = 0;
    for (event_subscription *s = bus->subscriptions; s; s = s->next) {
        if (s->event == event) snapshot[index++] = *s;
    }
    pthread_mutex_unlock(&bus->mutex);
    for (index = 0; index < count; index++)
        snapshot[index].callback(event, data, data_size, snapshot[index].user_data);
    free(snapshot);
    return ONION_OK;
}

