#pragma once

#include <functional>
#include <unordered_map>
#include <vector>
#include <memory>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/**
 * Lightweight event bus for inter-service communication.
 *
 * Services publish events; other services subscribe via callbacks.
 * Cross-task delivery uses FreeRTOS queues internally.
 */
class EventBus {
public:
    using EventId = uint32_t;
    using Callback = std::function<void(void*)>;

    EventBus();
    ~EventBus();

    /// Subscribe to an event. Returns a handle for unsubscription.
    int on(EventId evt, Callback cb);

    /// Unsubscribe by handle.
    void off(int handle);

    /// Publish synchronously (caller's context).
    void post(EventId evt, void* data = nullptr);

    /// Publish asynchronously via queue (ISR-safe).
    void postAsync(EventId evt, void* data = nullptr);

    /// Drain the async queue (call from the main loop).
    void dispatch();

private:
    struct Entry {
        int id;
        Callback cb;
    };
    std::unordered_map<EventId, std::vector<Entry>> subs_;
    int nextHandle_ = 1;
    QueueHandle_t asyncQueue_ = nullptr;
};

/// Built-in system events
enum {
    EV_SYS_STARTUP     = 0x0001,
    EV_SYS_IDLE        = 0x0002,

    EV_DISPLAY_READY   = 0x0100,

    EV_SDCARD_MOUNTED  = 0x0200,
    EV_SDCARD_ERROR    = 0x0201,

    EV_WIFI_CONNECTED  = 0x0300,
    EV_WIFI_DISCONNECTED = 0x0301,

    EV_CAMERA_READY    = 0x0400,
    EV_CAMERA_FRAME    = 0x0401,
    EV_CAMERA_ERROR    = 0x0402,

    EV_TIME_SYNC       = 0x0500,
};
