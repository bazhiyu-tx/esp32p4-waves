#pragma once

#include "core/service.h"

/**
 * WiFi via ESP32-C6 co-processor (esp_wifi_remote).
 *
 * Posts EV_WIFI_CONNECTED / EV_WIFI_DISCONNECTED.
 * Retries up to 3 times.
 */
class WifiService : public Service {
public:
    WifiService(EventBus& bus) : Service(bus, "WiFi") {}

    esp_err_t init() override;

    bool is_connected() const { return connected_; }
    void set_sd_mounted(bool v) { sdMounted_ = v; }

private:
    bool connected_ = false;
    bool sdMounted_ = false;
};
