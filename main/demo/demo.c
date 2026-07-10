/**
 * @file demo.c
 */
#include "demo.h"
#include "luf_statusbar.h"
#include "luf_notify.h"
#include "luf_osal.h"
#include "wifi.h"
#include "esp_log.h"
#include "font/binfont_loader/lv_binfont_loader.h"
#include "libs/fsdrv/lv_fsdrv.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "bsp/esp32_p4_wifi6_touch_lcd_7b.h"

static const char *TAG = "demo";
lv_font_t *cn_font_16 = NULL;

/* 从 SD 卡加载中文字体文件（直接 fopen 读取，不经过 LVGL 文件系统） */
void demo_fonts_init(void)
{
    /* 初始化 MEMFS（内存文件系统），供 lv_binfont_create_from_buffer 使用 */
    lv_fs_memfs_init();

    char path[64];
    snprintf(path, sizeof(path), BSP_SD_MOUNT_POINT "/fonts/cn_16.bin");

    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGW(TAG, "Failed to open %s", path);
        return;
    }

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len <= 0) {
        ESP_LOGW(TAG, "Empty font file");
        fclose(f);
        return;
    }

    void *buf = malloc(len);
    if (!buf) {
        ESP_LOGW(TAG, "Out of memory for font");
        fclose(f);
        return;
    }

    if (fread(buf, 1, len, f) != (size_t)len) {
        ESP_LOGW(TAG, "Failed to read font file");
        free(buf);
        fclose(f);
        return;
    }
    fclose(f);

    cn_font_16 = lv_binfont_create_from_buffer(buf, len);
    if (cn_font_16) {
        ESP_LOGI(TAG, "Loaded cn_16.bin (%ld bytes) from SD card", len);
    } else {
        ESP_LOGW(TAG, "lv_binfont_create_from_buffer failed");
        free(buf);
    }
}

static luf_statusbar_item_t clock_item;
static luf_statusbar_item_t wifi_item;

static void wifi_callback(bool connected, void *arg)
{
    LV_UNUSED(arg);
    if (connected) {
        luf_statusbar_label_set_text(wifi_item, LV_SYMBOL_WIFI);
    } else {
        luf_statusbar_label_set_text(wifi_item, LV_SYMBOL_CLOSE);
    }
}

/* 在 UI 上下文执行：算出时间写进时钟项。由 luf_pump() 取出调用 */
static void apply_clock(void *arg)
{
    LV_UNUSED(arg);
    time_t now = time(NULL);
    char buf[8];
    if (now < 100000) {
        /* NTP 未同步，回退到 uptime */
        uint32_t s = lv_tick_get() / 1000;
        lv_snprintf(buf, sizeof(buf), "%02u:%02u", (s / 3600) % 24, (s / 60) % 60);
    } else {
        struct tm ti;
        localtime_r(&now, &ti);
        lv_snprintf(buf, sizeof(buf), "%02d:%02d", ti.tm_hour, ti.tm_min);
    }
    luf_statusbar_label_set_text(clock_item, buf);
}

/* 数据源：这里用 UI 定时器模拟，真实工程里会是 RTC 任务/中断。
 * 不直接改 UI，只把"更新时钟"投递进消息队列，由 UI 任务排空时执行。 */
static void clock_source_cb(lv_timer_t *t)
{
    LV_UNUSED(t);
    luf_post(apply_clock, NULL);
}

void demo_statusbar(void)
{
    clock_item = luf_statusbar_label_add(LUF_STATUSBAR_LEFT, "00:00", 0);
    wifi_item = luf_statusbar_label_add(LUF_STATUSBAR_RIGHT, LV_SYMBOL_WIFI, 0);
    luf_statusbar_label_add(LUF_STATUSBAR_RIGHT, LV_SYMBOL_BATTERY_FULL, 0);

    lv_timer_create(clock_source_cb, 1000, NULL);
    luf_post(apply_clock, NULL);

    /* Update WiFi icon based on connection status */
    if (wifi_is_connected()) {
        luf_statusbar_label_set_text(wifi_item, LV_SYMBOL_WIFI);
    } else {
        luf_statusbar_label_set_text(wifi_item, LV_SYMBOL_CLOSE);
    }
    wifi_register_callback(NULL, NULL);
    wifi_register_callback(wifi_callback, NULL);
}

void demo_notify(void)
{
    static const char *msgs[] = {
        "System: UI framework ready",
        "Tip: swipe middle to switch desktops",
        "Tip: swipe from left/right edge to go back",
        "Tip: swipe up from bottom for home",
    };
    for (unsigned i = 0; i < sizeof(msgs) / sizeof(msgs[0]); i++) {
        struct luf_notify_msg m = {NULL, NULL, msgs[i], 0};
        luf_notify_add(&m);
    }
}

void demo_init(void)
{
    /* demo_fonts_init() 移到 main.c 中 SD 卡挂载后调用 */
    demo_statusbar();
    demo_notify();
    demo_apps(); /* 先注册 App，桌面图标页据此排图标 */
    demo_desktop();
}
