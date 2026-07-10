#include "time_service.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "lwip/netdb.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include <time.h>
#include <string.h>

static const char* TAG = "TimeService";

// NTP 时间基准：1900-01-01 到 1970-01-01 的秒数
#define NTP_EPOCH_OFFSET 2208988800UL

static bool ntp_request(const char* host, int timeout_ms) {
    // DNS 解析
    struct hostent *h = gethostbyname(host);
    if (!h) {
        ESP_LOGW(TAG, "DNS failed for %s", host);
        return false;
    }

    // 创建 UDP socket
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGW(TAG, "socket failed");
        return false;
    }

    // 设置接收超时
    struct timeval tv = { timeout_ms / 1000, (timeout_ms % 1000) * 1000 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    // 构建 NTP 请求包（NTP v4, client mode）
    uint8_t pkt[48] = {0};
    pkt[0] = 0x1B;  // LI=0, VN=4, Mode=3 (client)

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(123);
    memcpy(&addr.sin_addr, h->h_addr_list[0], sizeof(addr.sin_addr));

    // 发送请求
    sendto(sock, pkt, sizeof(pkt), 0, (struct sockaddr*)&addr, sizeof(addr));

    // 接收响应
    int r = recvfrom(sock, pkt, sizeof(pkt), 0, NULL, NULL);
    close(sock);

    if (r < 48) {
        ESP_LOGW(TAG, "NTP response too short: %d", r);
        return false;
    }

    // 解析时间戳（byte 40-43 = 秒，NTP epoch）
    uint32_t sec = ((uint32_t)pkt[40] << 24) | ((uint32_t)pkt[41] << 16) |
                   ((uint32_t)pkt[42] << 8)  | pkt[43];
    time_t t = sec - NTP_EPOCH_OFFSET;

    // 设置系统时间
    struct timeval now = { t, 0 };
    settimeofday(&now, NULL);
    return true;
}

TimeService::TimeService(EventBus& bus) : Service(bus, "TimeService") {}

esp_err_t TimeService::init() {
    ESP_LOGI(TAG, "Init, subscribing to WiFi events...");

    // 订阅 WiFi 连接事件，连上后自动对时
    evtHandle_ = bus().on(EV_WIFI_CONNECTED, [this](void*) {
        sync_time();
    });

    // 如果 WiFi 已经连上了（事件在我们订阅前已发出），立即对时
    esp_netif_t *netif = esp_netif_get_default_netif();
    if (netif) {
        esp_netif_ip_info_t ip;
        if (esp_netif_get_ip_info(netif, &ip) == ESP_OK && ip.ip.addr != 0) {
            ESP_LOGI(TAG, "WiFi already connected, syncing now...");
            sync_time();
        } else {
            ESP_LOGW(TAG, "Default netif found but no IP yet");
        }
    } else {
        ESP_LOGW(TAG, "No default netif, will wait for WiFi event");
    }

    return ESP_OK;
}

void TimeService::deinit() {
    if (evtHandle_) {
        bus().off(evtHandle_);
        evtHandle_ = 0;
    }
    timeSynced_ = false;
}

void TimeService::sync_time() {
    ESP_LOGI(TAG, "WiFi connected, starting NTP sync...");

    // 设置时区为东八区（中国标准时间）
    setenv("TZ", "CST-8", 1);
    tzset();

    // 尝试 NTP 服务器列表
    const char* servers[] = {
        "ntp.ntsc.ac.cn",
        "ntp.aliyun.com",
        "cn.pool.ntp.org",
    };

    bool ok = false;
    for (auto& s : servers) {
        ESP_LOGI(TAG, "Trying NTP server: %s", s);
        if (ntp_request(s, 3000)) {
            ok = true;
            break;
        }
    }

    if (ok) {
        time_t now = time(nullptr);
        struct tm ti;
        localtime_r(&now, &ti);
        ESP_LOGI(TAG, "Time synced: %04d-%02d-%02d %02d:%02d:%02d",
                 ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday,
                 ti.tm_hour, ti.tm_min, ti.tm_sec);
        timeSynced_ = true;
        bus_.post(EV_TIME_SYNC);
    } else {
        ESP_LOGW(TAG, "NTP sync failed (all servers)");
    }
}
