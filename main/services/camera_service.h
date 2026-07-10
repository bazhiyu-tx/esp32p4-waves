#pragma once

#include "core/service.h"
#include "camera.h"

/**
 * Camera service using V4L2 + ISP + BGR→RGB565 conversion.
 *
 * Posts EV_CAMERA_READY on init, EV_CAMERA_FRAME on each new frame.
 * Call get_frame() from LVGL context to get the latest RGB565 buffer.
 */
class CameraService : public Service {
public:
    CameraService(EventBus& bus);

    esp_err_t init() override;
    void deinit() override;

    /// Start streaming (called when camera app opens).
    esp_err_t start_stream();

    /// Stop streaming (called when camera app closes).
    void stop_stream();

    /// Get the latest frame (call from LVGL timer).
    const camera_frame_t* get_frame();

    bool is_ready() const { return ready_; }

private:
    bool ready_ = false;
};
