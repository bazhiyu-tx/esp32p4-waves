/*
 * SPDX-FileCopyrightText: 2025 ESP32-P4 LVGL Demo
 *
 * SPDX-License-Identifier: CC0-1.0
 *
 * Application entry point.
 *
 * Initialises all services in dependency order.
 * Each service encapsulates a subsystem (display, SD card, WiFi, camera…)
 * and communicates with others via the EventBus.
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "core/service_manager.h"
#include "services/display_service.h"
#include "services/sdcard_service.h"
#include "services/wifi_service.h"
#include "services/camera_service.h"
#include "services/system/time_service.h"
#include "services/yolo_service.h"
#include "services/audio_mDNS_service.h"
#include "services/system/OTA_service.h"
#include "bsp/esp-bsp.h"
#include "demo.h"

static const char* TAG = "app";

/* 全局音频服务指针（供 demo_apps.c 调用） */
AudioMDnsService *g_audio_svc = nullptr;
OTAService *g_ota_svc = nullptr;

/* FreeRTOS hook stubs (required) */
extern "C" {
    void vApplicationIdleHook(void) {}
    void vApplicationTickHook(void) {}
}

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "Starting phone-style UI...");

    ServiceManager mgr;

    /* 1. Display + LVGL (must be first) */
    mgr.create<DisplayService>();

    /* 2. WiFi via ESP32-C6 co-processor */
    auto& wifi = mgr.create<WifiService>();

    /* 3. SD card + font loading */
    auto& sd = mgr.create<SdCardService>();

    /* 4. Time sync (NTP, subscribes to WiFi events) */
    mgr.create<TimeService>();

    /* 5. Camera (OV5647, uses ISP) */
    mgr.create<CameraService>();

    /* 6. Audio stream + mDNS (click icon to start) */
    g_audio_svc = &mgr.create<AudioMDnsService>();

    /* 7. YOLO11n-pose AI detection (loads model from SD) */
    mgr.create<YoloService>();

    g_ota_svc = &mgr.create<OTAService>();

    /* Init all in order — services publish events on completion */
    esp_err_t ret = mgr.start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Service init failed (0x%x), continuing", ret);
    }

    /* 等 SD 卡挂载完再构建完整 UI 页面（此时图片文件可访问） */
    bsp_display_lock(0);
    demo_init();
    bsp_display_unlock();

    /* Notify WiFi that SD may have config */
    wifi.set_sd_mounted(sd.is_mounted());

    ESP_LOGI(TAG, "UI running! Swipe left/right for desktops, tap icons to open apps.");

    /* The LVGL timer task handles UI; main task just polls events */
    while (true) {
        mgr.dispatch();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
