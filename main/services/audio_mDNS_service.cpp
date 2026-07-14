#include "audio_mDNS_service.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"

const char *AudioMDnsService::TAG = "AudioMDns";

AudioMDnsService::AudioMDnsService(EventBus& bus) : Service(bus, "AudioMDns") {}

void AudioMDnsService::udp_task_func(void *arg) {
    auto *svc = (AudioMDnsService *)arg;
    
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "Failed to create UDP socket");
        return;
    }
    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(5000);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "UDP bind failed");
        close(sock);
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "UDP listening on port 5000");

    // UDP 超时 50ms → 无数据时写零（静音）而非关闭 codec
    struct timeval tv = { .tv_sec = 0, .tv_usec = 50000 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    static char buf[2048];
    static const uint8_t silence[1024] = {0};  // 静音帧
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);

    while(1){
        int len = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr *)&client_addr, &addr_len);
        if (len < 0) {
            // 超时无数据 → 喂静音，保持 DAC 输出零电平
            if (svc->m_spk_dev) {
                esp_codec_dev_write(svc->m_spk_dev, (void *)silence, sizeof(silence));
            }
            continue;
        }
        // 播放接收到的 PCM 数据
        if (svc->m_spk_dev) {
            esp_codec_dev_write(svc->m_spk_dev, buf, len);
        }
    }
}

esp_err_t AudioMDnsService::play_pcm(const void *data, size_t len)
{
    if (!m_spk_dev) return ESP_ERR_INVALID_STATE;
    return esp_codec_dev_write(m_spk_dev, const_cast<void*>(data), (int)len);
}

esp_err_t AudioMDnsService::set_volume(uint8_t vol)
{
    if (vol > 100) vol = 100;
    m_volume = vol;
    if (m_spk_dev) {
        return esp_codec_dev_set_out_vol(m_spk_dev, m_volume);
    }
    return ESP_OK;
}

esp_err_t AudioMDnsService::init()
{
    ESP_LOGI(TAG, "Service registered (idle, start by app)");
    return ESP_OK;
}

esp_err_t AudioMDnsService::start_audio()
{
    esp_log_level_set("mdns", ESP_LOG_WARN);

    if (m_running) {
        ESP_LOGW(TAG, "Audio already running");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Starting audio service...");

    // 初始化音频硬件
    bsp_i2c_init();
    bsp_audio_init(NULL);
    m_spk_dev = bsp_audio_codec_speaker_init();
    if (!m_spk_dev) {
        ESP_LOGE(TAG, "Speaker init failed");
        return ESP_FAIL;
    }
    set_volume(m_volume);
    ESP_LOGI(TAG, "Audio speaker ready");

    // 打开 codec 并配置格式（持续保持打开，通过写零而非关闭来降噪）
    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = 16,
        .channel = 1,
        .channel_mask = 0,
        .sample_rate = 44100,
        .mclk_multiple = 0,
    };
    esp_codec_dev_open(m_spk_dev, &fs);

    // 订阅 WiFi 事件，连上后注册 mDNS
    m_wifi_evt = bus().on(EV_WIFI_CONNECTED, [this](void*) {
        ESP_LOGI(TAG, "WiFi connected, starting mDNS...");
        if (!m_mdns_started) {
            mdns_init();
            m_mdns_started = true;
        }
        mdns_hostname_set("esp32p4-audio");
        mdns_instance_name_set("ESP32-P4 Audio Stream");
        mdns_service_add(NULL, "_audio-stream", "_udp", 5000, NULL, 0);
        mdns_service_txt_item_set("_audio-stream", "_udp", "sample_rate", "44100");
        mdns_service_txt_item_set("_audio-stream", "_udp", "channels", "1");
    });

    // 如果 WiFi 已有 IP，立即注册 mDNS
    esp_netif_t *netif = esp_netif_get_default_netif();
    if (netif) {
        esp_netif_ip_info_t ip;
        if (esp_netif_get_ip_info(netif, &ip) == ESP_OK && ip.ip.addr != 0) {
            bus().post(EV_WIFI_CONNECTED);
        }
    }

    // 启动 UDP 接收任务
    xTaskCreatePinnedToCore(AudioMDnsService::udp_task_func, "audio_udp", 4096, this, 2, &m_udp_task, 1);

    // 关闭冗长的 mDNS 调试日志，不再看到满屏的局域网广播包
    esp_log_level_set("mdns", ESP_LOG_WARN);

    m_running = true;
    ESP_LOGI(TAG, "Audio service started, listening on port 5000");
    return ESP_OK;
}

void AudioMDnsService::stop_audio()
{
    if (!m_running) return;

    ESP_LOGI(TAG, "Stopping audio service...");

    // 停止 UDP 任务
    if (m_udp_task) {
        vTaskDelete(m_udp_task);
        m_udp_task = nullptr;
    }

    // 取消 WiFi 事件订阅
    if (m_wifi_evt >= 0) {
        bus().off(m_wifi_evt);
        m_wifi_evt = -1;
    }

    // 关闭扬声器
    if (m_spk_dev) {
        esp_codec_dev_close(m_spk_dev);
        m_spk_dev = nullptr;
    }

    m_running = false;
    ESP_LOGI(TAG, "Audio service stopped");
}

void AudioMDnsService::deinit()
{
    stop_audio();
}