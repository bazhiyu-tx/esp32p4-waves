#include "display_service.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "luf.h"
#include "luf_osal.h"
#include "demo.h"

// Need LV_USE_PPA for conditional compilation
#include "lvgl.h"

#if LV_USE_PPA
extern "C" void lv_draw_ppa_init(void);
#endif

static const char* TAG = "Display";

esp_err_t DisplayService::init() {
    ESP_LOGI(TAG, "Init display...");

    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = {
            .task_priority = 4,
            .task_stack = 16384,      // Increased from 7168 for PPA draw stack depth
            .task_affinity = -1,
            .task_max_sleep_ms = 500,
            .task_stack_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_DEFAULT,
            .timer_period_ms = 5,
        },
        .buffer_size = BSP_LCD_DRAW_BUFF_SIZE,
        .double_buffer = BSP_LCD_DRAW_BUFF_DOUBLE,
        .flags = {
            .buff_dma = true,
            .buff_spiram = false,
            .sw_rotate = false,
        }
    };
    lv_display_t* disp = bsp_display_start_with_config(&cfg);
    (void)disp;
    // Initialize PPA hardware acceleration for LVGL drawing (ESP32-P4 only)
#if LV_USE_PPA
    lv_draw_ppa_init();
#endif
    bsp_display_backlight_on();

    bsp_display_lock(0);
    luf_osal_init();
    luf_init();

    /* 定期排空 luf_post 消息队列（每 10ms）——没有这个，时钟、通知不更新 */
    lv_timer_create([](lv_timer_t*) { luf_pump(); }, 10, NULL);

    bsp_display_unlock();

    bus_.post(EV_DISPLAY_READY);
    ESP_LOGI(TAG, "Display + LVGL ready");
    return ESP_OK;
}

void DisplayService::deinit() {
    // LVGL has no deinit in practice; BSP handles it.
}
