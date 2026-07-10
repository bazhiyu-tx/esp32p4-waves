#include "sdcard_service.h"
#include "sd_card.h"
#include "demo.h"
#include "driver/gpio.h"
#include "esp_task_wdt.h"
#include "bsp/esp32_p4_wifi6_touch_lcd_7b.h"

static const char* TAG = "SdCard";

esp_err_t SdCardService::init() {
    ESP_LOGI(TAG, "Init SD card...");

    /* GPIO45 LOW = SD card power ON */
    gpio_set_direction(GPIO_NUM_45, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_45, 0);
    vTaskDelay(pdMS_TO_TICKS(50));

    mounted_ = (sd_card_mount() == ESP_OK);
    if (mounted_) {
        ESP_LOGI(TAG, "SD card mounted at " BSP_SD_MOUNT_POINT);
        /* Load Chinese font */
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(10));
        demo_fonts_init();
        esp_task_wdt_reset();
        bus_.post(EV_SDCARD_MOUNTED);
    } else {
        ESP_LOGW(TAG, "SD card mount failed, continuing");
        bus_.post(EV_SDCARD_ERROR);
    }
    return ESP_OK;
}

void SdCardService::deinit() {
    // SD card can't be safely unmounted in ESP-IDF
}
