#pragma once

#include "esp_err.h"
#include "esp_log.h"
#include "event_bus.h"

/**
 * Base class for all services.
 *
 * Lifecycle:
 *   constructor → init() → (task loop) → deinit() → destructor
 *
 * Services can create their own FreeRTOS task during init().
 * Communication with other services goes through the EventBus.
 */
class Service {
public:
    Service(EventBus& bus, const char* name) : bus_(bus), name_(name) {}
    virtual ~Service() = default;

    /// Name for logging.
    const char* name() const { return name_; }

    /// Initialize hardware and start any tasks.
    virtual esp_err_t init() = 0;

    /// Stop tasks and release resources.
    virtual void deinit() {}

    /// Get the event bus reference (for subscribing/publishing).
    EventBus& bus() { return bus_; }

protected:
    EventBus& bus_;
    const char* name_;
};
