#include "camera_service.h"
#include "camera.h"
#include "esp_log.h"

static const char* TAG = "Camera";

CameraService::CameraService(EventBus& bus) : Service(bus, "Camera") {}

esp_err_t CameraService::init() {
    ESP_LOGI(TAG, "Init camera...");
    esp_err_t e = camera_init();
    if (e == ESP_OK) {
        ready_ = true;
        bus_.post(EV_CAMERA_READY);
        ESP_LOGI(TAG, "Camera ready");
    } else {
        ESP_LOGW(TAG, "Camera init failed, continuing");
    }
    return ESP_OK;
}

void CameraService::deinit() {
    camera_deinit();
    ready_ = false;
}

esp_err_t CameraService::start_stream() {
    return camera_start_stream();
}

void CameraService::stop_stream() {
    camera_stop_stream();
}

const camera_frame_t* CameraService::get_frame() {
    return camera_get_frame();
}
