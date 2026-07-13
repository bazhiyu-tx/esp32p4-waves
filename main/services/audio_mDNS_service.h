#pragma once

#include "core/service.h"
#include "core/event_bus.h"
#include "bsp/esp-bsp.h"
#include "esp_codec_dev.h"
#include "mdns.h"

class AudioMDnsService : public Service {
    public:
        AudioMDnsService(EventBus& bus);
        ~AudioMDnsService() = default;

        esp_err_t init() override;        // 轻量注册，不做实际操作
        void deinit() override;

        /// 启动音频服务（点击桌面图标后调用）
        esp_err_t start_audio();

        /// 停止音频服务（关闭 App 时调用）
        void stop_audio();

        /// 播放 PCM 数据
        esp_err_t play_pcm(const void *data, size_t len);

        /// 设置音量 0..100
        esp_err_t set_volume(uint8_t vol);
        
        /// 获取当前音量
        uint8_t get_volume() const { return m_volume; }

        /// 是否正在运行
        bool is_running() const { return m_running; }

    private:
        static const char *TAG;

        esp_codec_dev_handle_t m_spk_dev = nullptr;

        static void udp_task_func(void *arg);
        TaskHandle_t m_udp_task = nullptr;
        uint8_t m_volume = 50;
        int m_wifi_evt = -1;
        bool m_mdns_started = false;
        bool m_running = false;
};

