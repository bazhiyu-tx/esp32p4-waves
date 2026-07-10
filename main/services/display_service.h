#pragma once

#include "core/service.h"

/**
 * Display + LVGL initialization service.
 *
 * Must be the first service started (other services may depend on LVGL).
 */
class DisplayService : public Service {
public:
    DisplayService(EventBus& bus) : Service(bus, "Display") {}

    esp_err_t init() override;
    void deinit() override;
};
