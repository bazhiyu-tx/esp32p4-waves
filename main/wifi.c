/**
 * @file wifi.c
 *
 * WiFi station manager for ESP32-P4 (uses ESP32-C6 via esp_wifi_remote).
 * WiFi config stored on SD card (/sdcard/wifi.cfg).
 */
#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "wifi.h"
#include "bsp/esp32_p4_wifi6_touch_lcd_7b.h"

#define WIFI_CONNECTED_BIT BIT0
#define CONFIG_PATH BSP_SD_MOUNT_POINT "/wifi.cfg"

static const char *TAG = "wifi";
static EventGroupHandle_t s_wifi_event_group;
static bool s_connected = false;
static bool s_sd_mounted = false;
static int s_retry_count = 0;
#define WIFI_MAX_RETRY 3

static wifi_cb_t s_cb = NULL;
static void *s_cb_arg = NULL;

void wifi_register_callback(wifi_cb_t cb, void *arg)
{
    s_cb = cb;
    s_cb_arg = arg;
}

void wifi_set_sd_mounted(bool mounted)
{
    s_sd_mounted = mounted;
}

bool wifi_is_connected(void)
{
    return s_connected;
}

/* ---------- SD card config file ---------- */

static void config_save(const char *ssid, const char *pass)
{
    if (!s_sd_mounted) {
        ESP_LOGW(TAG, "SD not mounted, config NOT saved");
        return;
    }
    FILE *f = fopen(CONFIG_PATH, "w");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open %s for writing", CONFIG_PATH);
        return;
    }
    fprintf(f, "ssid=%s\npassword=%s\n", ssid, pass ? pass : "");
    fclose(f);
    ESP_LOGI(TAG, "WiFi config saved to %s", CONFIG_PATH);
}

static bool config_load(char *ssid, size_t ssid_len, char *pass, size_t pass_len)
{
    if (!s_sd_mounted) return false;
    FILE *f = fopen(CONFIG_PATH, "r");
    if (!f) return false;

    char fmt[32];
    snprintf(fmt, sizeof(fmt), "ssid=%%.%us\npassword=%%.%us\n",
             (unsigned)(ssid_len - 1), (unsigned)(pass_len - 1));
    bool ok = (fscanf(f, fmt, ssid, pass) == 2);
    fclose(f);
    return ok;
}

/* ---------- WiFi events ---------- */
static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        s_retry_count = 0;
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        s_retry_count++;
        if (s_retry_count <= WIFI_MAX_RETRY) {
            ESP_LOGI(TAG, "Disconnected, retry %d/%d...", s_retry_count, WIFI_MAX_RETRY);
            esp_wifi_connect();
        } else {
            ESP_LOGE(TAG, "Failed to connect after %d retries, giving up.", WIFI_MAX_RETRY);
            if (s_cb) s_cb(false, s_cb_arg);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_connected = true;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        if (s_cb) s_cb(true, s_cb_arg);
    }
}

int wifi_scan(wifi_ap_record_t *aps, int max_count)
{
    uint16_t count = max_count;
    esp_wifi_scan_start(NULL, true);
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&count, (wifi_ap_record_t *)aps));
    return count;
}

void wifi_connect(const char *ssid, const char *password)
{
    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    if (password && strlen(password) > 0) {
        strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
    }

    /* Save to SD card */
    config_save(ssid, password ? password : "");

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    esp_wifi_connect();
}

static void load_saved_config(void)
{
    char ssid[33] = {0}, pass[65] = {0};
    if (config_load(ssid, sizeof(ssid), pass, sizeof(pass))) {
        ESP_LOGI(TAG, "Found saved config: %s, connecting...", ssid);
        wifi_connect(ssid, pass);
    }
}

void wifi_init(void)
{
    /* NVS still needed by WiFi stack internally (MAC, calibration, etc.) */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* SD card already mounted by app_main, just check status */
    if (s_sd_mounted) {
        ESP_LOGI(TAG, "SD card ready at " BSP_SD_MOUNT_POINT);
    }

    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                    ESP_EVENT_ANY_ID, &event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                    IP_EVENT_STA_GOT_IP, &event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    /* NOTE: Don't load saved config yet — SD card not mounted.
     * wifi_load_saved_config() will be called after SD card mount. */

    ESP_LOGI(TAG, "WiFi init done");
}

void wifi_load_saved_config(void)
{
    if (!s_sd_mounted) {
        ESP_LOGW(TAG, "SD not mounted, cannot load saved config");
        return;
    }
    load_saved_config();
}
