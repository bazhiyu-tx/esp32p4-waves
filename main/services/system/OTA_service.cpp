#include "OTA_service.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "esp_netif.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_crt_bundle.h"
#include "esp_cache.h"
#include "esp_heap_caps.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static const char *TAG = "OTA";
#define OTA_FW_URL "https://gitcode.com/tx209946/esp32p4-waves/releases/download/v0.2/hello_world.bin"

// 打印内部 RAM 和 PSRAM 剩余情况
static void log_heap_info(void) {
    ESP_LOGI(TAG, "--- Heap info ---");
    ESP_LOGI(TAG, "  INTERNAL: free=%zu  largest=%zu  min_free=%zu",
             heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
             heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
    ESP_LOGI(TAG, "  SPIRAM:   free=%zu  largest=%zu  min_free=%zu",
             heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
             heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM),
             heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM));
    ESP_LOGI(TAG, "  DMA:      free=%zu  largest=%zu",
             heap_caps_get_free_size(MALLOC_CAP_DMA),
             heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
    ESP_LOGI(TAG, "  INTERNAL|DMA|8BIT: largest=%zu",
             heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT));
}

// HTTP 事件处理：打印重定向和 CDN 响应
static esp_err_t http_event_handler(esp_http_client_event_t *evt) {
    switch (evt->event_id) {
    case HTTP_EVENT_REDIRECT: {
        char url[256];
        if (esp_http_client_get_url(evt->client, url, sizeof(url)) == ESP_OK) {
            ESP_LOGI(TAG, "Redirect to: %s", url);
        }
        break;
    }
    case HTTP_EVENT_ON_CONNECTED:
        ESP_LOGI(TAG, "Connected");
        break;
    case HTTP_EVENT_ON_HEADER:
        ESP_LOGI(TAG, "Header: %s = %s", evt->header_key, evt->header_value);
        break;
    case HTTP_EVENT_ON_HEADERS_COMPLETE: {
        int status = esp_http_client_get_status_code(evt->client);
        int content_len = esp_http_client_get_content_length(evt->client);
        ESP_LOGI(TAG, "HTTP %d, Content-Length: %d", status, content_len);
        break;
    }
    case HTTP_EVENT_ERROR:
        ESP_LOGE(TAG, "HTTP_EVENT_ERROR");
        break;
    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "Disconnected");
        break;
    default:
        break;
    }
    return ESP_OK;
}

// PSRAM 地址范围 (ESP32-P4)
#define PSRAM_ADDR_LOW  0x48000000
#define PSRAM_ADDR_HIGH 0x4C000000

OTAService::OTAService(EventBus& bus) : Service(bus, "OTA") {}

esp_err_t OTAService::init() {
    const esp_app_desc_t *app_desc = esp_app_get_description();
    if (app_desc) {
        snprintf(m_ota_version, sizeof(m_ota_version), "%s", app_desc->version);
        ESP_LOGI(TAG, "Current firmware version: %s", m_ota_version);
    }
    return ESP_OK;
}

void OTAService::deinit() {
    if (m_wifi_evt >= 0) { bus().off(m_wifi_evt); m_wifi_evt = -1; }
    if (m_ota_task) { vTaskDelete(m_ota_task); m_ota_task = nullptr; }
}

void OTAService::start_ota() {
    if (m_ota_task) return;

    m_wifi_evt = bus().on(EV_WIFI_CONNECTED, [this](void*) {
        xTaskCreatePinnedToCore(ota_task_func, "ota_task", 16384, this, 5, &m_ota_task, 1);
    });

    esp_netif_ip_info_t ip;
    esp_netif_t *netif = esp_netif_get_default_netif();
    if (netif && esp_netif_get_ip_info(netif, &ip) == ESP_OK && ip.ip.addr != 0)
        bus().post(EV_WIFI_CONNECTED);
}

/* 在 TLS 握手完成后 flush 整个 PSRAM data cache。
   TLS 握手会通过 psa_import_key() 把 AES 密钥写到 PSRAM，
   但数据只到了 CPU cache，还没写回 PSRAM。
   随后硬件 AES-GCM DMA 直接从 PSRAM 读密钥时读到的是旧数据。
   这个函数在握手后、第一次解密前执行，确保 DMA 读到正确数据。 */
static void psram_cache_flush(void) {
    size_t psram_size = PSRAM_ADDR_HIGH - PSRAM_ADDR_LOW;
    esp_err_t ret = esp_cache_msync(
        (void *)PSRAM_ADDR_LOW, psram_size,
        ESP_CACHE_MSYNC_FLAG_DIR_C2M |
        ESP_CACHE_MSYNC_FLAG_INVALIDATE |
        ESP_CACHE_MSYNC_FLAG_UNALIGNED);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "PSRAM cache flush: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGD(TAG, "PSRAM cache flushed (%zu MB)", psram_size / 1024 / 1024);
    }
}

void OTAService::ota_task_func(void *arg) {
    OTAService *svc = (OTAService *)arg;
    esp_err_t err;

    ESP_LOGI(TAG, "Starting OTA from %s ...", OTA_FW_URL);
    log_heap_info();

    // ============================================================
    // 阶段 1: 建立 HTTPS 连接 + TLS 握手
    // 此时 psa_import_key() 把 AES 密钥写入 PSRAM（只到 cache）
    // ============================================================
    // 启用 esp-tls 和 mbedtls 日志
    esp_log_level_set("esp-tls-mbedtls", ESP_LOG_VERBOSE);

    esp_http_client_config_t http_cfg = {};
    http_cfg.url = OTA_FW_URL;
    http_cfg.timeout_ms = 120000;
    http_cfg.crt_bundle_attach = esp_crt_bundle_attach;
    http_cfg.event_handler = http_event_handler;
    http_cfg.keep_alive_enable = true;

    esp_https_ota_config_t ota_cfg = {};
    ota_cfg.http_config = &http_cfg;

    esp_https_ota_handle_t ota_handle = NULL;
    err = esp_https_ota_begin(&ota_cfg, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_https_ota_begin failed: %s", esp_err_to_name(err));
        svc->m_ota_task = nullptr;
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "TLS handshake done, keys imported");

    // ============================================================
    // 阶段 2: flush PSRAM cache，把密钥从 cache 刷到 PSRAM
    // 这样后续 DMA 才能读到正确的密钥
    // ============================================================
    ESP_LOGI(TAG, "Flushing PSRAM cache...");
    psram_cache_flush();

    // ============================================================
    // 阶段 3: 读取固件头部，验证版本
    // 此处的第一次解密会触发 psa_aead_decrypt → DMA，
    // 此时密钥已在 PSRAM 物理内存中，DMA 读到正确值
    // ============================================================
    esp_app_desc_t app_desc = {};
    err = esp_https_ota_get_img_desc(ota_handle, &app_desc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_https_ota_get_img_desc failed: %s", esp_err_to_name(err));
        esp_https_ota_abort(ota_handle);
        svc->m_ota_task = nullptr;
        vTaskDelete(NULL);
        return;
    }

    // 版本检查
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_app_desc_t running_app_info;
    if (esp_ota_get_partition_description(running, &running_app_info) == ESP_OK) {
        ESP_LOGI(TAG, "Running: %s, New: %s", running_app_info.version, app_desc.version);
    }
    if (memcmp(app_desc.version, running_app_info.version, sizeof(app_desc.version)) == 0) {
        ESP_LOGW(TAG, "Same version, skipping");
        esp_https_ota_abort(ota_handle);
        svc->m_ota_task = nullptr;
        vTaskDelete(NULL);
        return;
    }

    // ============================================================
    // 阶段 4: 下载固件
    // ============================================================
    while (1) {
        err = esp_https_ota_perform(ota_handle);
        if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
            break;
        }
        size_t len = esp_https_ota_get_image_len_read(ota_handle);
        if (len % (100 * 1024) == 0) {
            ESP_LOGI(TAG, "Downloaded %zu KB", len / 1024);
        }
    }

    if (err == ESP_OK && esp_https_ota_is_complete_data_received(ota_handle)) {
        err = esp_https_ota_finish(ota_handle);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "OTA done, rebooting...");
            vTaskDelay(pdMS_TO_TICKS(1000));
            esp_restart();
        } else {
            ESP_LOGE(TAG, "esp_https_ota_finish failed: %s", esp_err_to_name(err));
        }
    } else {
        ESP_LOGE(TAG, "OTA download incomplete: %s", esp_err_to_name(err));
        esp_https_ota_abort(ota_handle);
    }

    svc->m_ota_task = nullptr;
    vTaskDelete(NULL);
}

// C 兼容接口，供 demo_apps.c 调用
extern "C" void ota_check_update(void) {
    extern OTAService *g_ota_svc;
    if (g_ota_svc) g_ota_svc->start_ota();
}