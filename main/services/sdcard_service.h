#pragma once

#include "core/service.h"

/**
 * SD card mount + font loading service.
 *
 * After init(), SD card is mounted at BSP_SD_MOUNT_POINT.
 * Posts EV_SDCARD_MOUNTED on success.
 */
class SdCardService : public Service {
public:
    SdCardService(EventBus& bus) : Service(bus, "SdCard") {}

    esp_err_t init() override;
    void deinit() override;

    bool is_mounted() const { return mounted_; }

private:
    bool mounted_ = false;
};
