#include "wifi_service.h"
#include "wifi.h"

static const char* TAG = "WiFi";

esp_err_t WifiService::init() {
    ESP_LOGI(TAG, "Init WiFi...");
    wifi_init();
    if (sdMounted_) {
        wifi_load_saved_config();
    } else {
        ESP_LOGW(TAG, "SD not mounted, WiFi config deferred");
    }
    /* WiFi status is reported via callbacks from wifi.c */
    bus_.post(EV_WIFI_CONNECTED); /* simplified: assume connected */
    return ESP_OK;
}
