/**
 * @file demo_wifi.c
 *
 * WiFi 扫描与连接页面（替换 Settings 中的 Wi-Fi 占位符）。
 * 扫描附近网络 → 列表展示 → 点击弹出键盘输密码 → 连接。
 */
#include "demo.h"
#include "luf.h"
#include "luf_app_mgr.h"
#include "wifi.h"
#include "esp_log.h"

static const char *TAG = "demo_wifi";

static lv_obj_t *list_container;
static lv_obj_t *status_label;
static lv_obj_t *keyboard;
static lv_obj_t *password_ta;  /* textarea for password */
static lv_obj_t *current_content; /* current page content container */
static char selected_ssid[33];

static void do_connect(const char *ssid, const char *pass)
{
    ESP_LOGI(TAG, "Connecting to %s...", ssid);
    wifi_connect(ssid, pass);
    /* Go back to settings */
    luf_app_mgr_back();
}

static void keyboard_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    LV_UNUSED(e);
    if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        if (code == LV_EVENT_READY) {
            const char *pass = lv_textarea_get_text(password_ta);
            do_connect(selected_ssid, pass);
        } else {
            luf_app_mgr_back();
        }
    }
}

static void ap_clicked(lv_event_t *e)
{
    const char *ssid = lv_event_get_user_data(e);
    if (ssid == NULL || strlen(ssid) == 0) return;

    strncpy(selected_ssid, ssid, sizeof(selected_ssid) - 1);
    selected_ssid[sizeof(selected_ssid) - 1] = '\0';

    /* Create password dialog on the current content */
    lv_obj_t *content = current_content;
    if (content == NULL) return;

    /* Show keyboard and textarea */
    password_ta = lv_textarea_create(content);
    lv_obj_set_size(password_ta, lv_obj_get_width(content) - 32, 50);
    lv_obj_align(password_ta, LV_ALIGN_TOP_MID, 0, 60);
    lv_textarea_set_placeholder_text(password_ta, "Enter WiFi password");
    lv_textarea_set_password_mode(password_ta, true);
    lv_obj_set_style_bg_color(password_ta, lv_color_hex(0x222831), 0);
    lv_obj_set_style_bg_opa(password_ta, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(password_ta, lv_color_white(), 0);
    lv_obj_set_style_radius(password_ta, 6, 0);
    lv_obj_add_state(password_ta, LV_STATE_FOCUSED);

    keyboard = lv_keyboard_create(content);
    lv_keyboard_set_textarea(keyboard, password_ta);
    lv_obj_set_style_radius(keyboard, 8, 0);
    lv_obj_set_style_bg_color(keyboard, lv_color_hex(0x1b2028), 0);
    lv_obj_set_style_bg_opa(keyboard, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(keyboard, keyboard_event_cb, LV_EVENT_ALL, NULL);

    lv_obj_t *title = lv_label_create(content);
    lv_label_set_text_fmt(title, "Connect to: %s", ssid);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);
}



static void refresh_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    lv_label_set_text(status_label, "Scanning...");

    lv_obj_clean(list_container);
    wifi_ap_record_t aps[WIFI_MAX_AP];
    int count = wifi_scan(aps, WIFI_MAX_AP);

    if (count <= 0) {
        lv_obj_t *empty = lv_label_create(list_container);
        lv_label_set_text(empty, "No networks found");
        lv_obj_set_style_text_color(empty, lv_color_hex(0x6b7480), 0);
        lv_obj_center(empty);
        lv_label_set_text(status_label, "Tap Scan to try again");
        return;
    }

    for (int i = 0; i < count; i++) {
        if (strlen((const char *)aps[i].ssid) == 0) continue;
        lv_obj_t *row = lv_obj_create(list_container);
        lv_obj_remove_style_all(row);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_height(row, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(row, lv_color_hex(0x222831), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(row, 8, 0);
        lv_obj_set_style_pad_all(row, 12, 0);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        char *ssid_copy = strdup((const char *)aps[i].ssid);
        lv_obj_add_event_cb(row, ap_clicked, LV_EVENT_CLICKED, ssid_copy);
        lv_obj_t *name = lv_label_create(row);
        lv_obj_set_style_text_color(name, lv_color_hex(0xc8cfd6), 0);
        lv_label_set_text(name, (const char *)aps[i].ssid);
        if (cn_font_16) {
            lv_obj_set_style_text_font(name, cn_font_16, 0);
        }
        lv_obj_t *info = lv_label_create(row);
        lv_obj_align(info, LV_ALIGN_RIGHT_MID, -10, 0);
        lv_obj_set_style_text_color(info, lv_color_hex(0x6b7480), 0);
        char info_buf[16];
        int bars = (aps[i].rssi >= -50) ? 4 : (aps[i].rssi >= -65) ? 3 : (aps[i].rssi >= -80) ? 2 : 1;
        const char *lock = (aps[i].authmode > 0) ? "!" : " ";
        lv_snprintf(info_buf, sizeof(info_buf), "%s%d", lock, bars);
        lv_label_set_text(info, info_buf);
    }

    char msg[32];
    lv_snprintf(msg, sizeof(msg), "Found %d networks", count);
    lv_label_set_text(status_label, msg);
}

void demo_wifi_build(lv_obj_t *content, void *arg)
{
    LV_UNUSED(arg);
    current_content = content;

    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(content, 16, 0);
    lv_obj_set_style_pad_row(content, 10, 0);

    /* Header + refresh button */
    lv_obj_t *header_row = lv_obj_create(content);
    lv_obj_remove_style_all(header_row);
    lv_obj_set_width(header_row, LV_PCT(100));
    lv_obj_set_height(header_row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(header_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header_row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *title = lv_label_create(header_row);
    lv_obj_set_style_text_color(title, lv_color_hex(0x4caf50), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_label_set_text(title, "Wi-Fi");

    lv_obj_t *refresh_btn = lv_btn_create(header_row);
    lv_obj_set_size(refresh_btn, 80, 36);
    lv_obj_set_style_radius(refresh_btn, 8, 0);
    lv_obj_set_style_bg_color(refresh_btn, lv_color_hex(0x4caf50), 0);
    lv_obj_set_style_bg_opa(refresh_btn, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(refresh_btn, refresh_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t *refresh_lbl = lv_label_create(refresh_btn);
    lv_label_set_text(refresh_lbl, "Scan");
    lv_obj_set_style_text_color(refresh_lbl, lv_color_white(), 0);
    lv_obj_center(refresh_lbl);

    /* Status */
    status_label = lv_label_create(content);
    lv_obj_set_style_text_color(status_label, lv_color_hex(0x6b7480), 0);
    lv_label_set_text(status_label, "Tap Scan to find networks");

    /* Scrollable list of APs */
    list_container = lv_obj_create(content);
    lv_obj_remove_style_all(list_container);
    lv_obj_set_width(list_container, LV_PCT(100));
    lv_obj_set_flex_grow(list_container, 1);
    lv_obj_set_flex_flow(list_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list_container, 6, 0);
    lv_obj_set_scrollbar_mode(list_container, LV_SCROLLBAR_MODE_AUTO);
}
