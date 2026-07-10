#include "display_service.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "luf.h"
#include "luf_osal.h"
#include "demo.h"

static const char* TAG = "Display";

esp_err_t DisplayService::init() {
    ESP_LOGI(TAG, "Init display...");

    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
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
    bsp_display_backlight_on();

    bsp_display_lock(0);
    luf_osal_init();
    luf_init();
    demo_init();

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
