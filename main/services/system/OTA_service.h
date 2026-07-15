#pragma once
#include "core/service.h"
#include "esp_ota_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

class OTAService : public Service {
    public:
        OTAService(EventBus& bus); 
        ~OTAService() = default;

        esp_err_t init() override;
        void deinit() override;

        /// 启动 OTA 更新（点击桌面图标后调用）
        void start_ota();
    private:
        char m_ota_version[32] = {0};
        char m_ota_url[256] = {0};
        TaskHandle_t m_ota_task = nullptr;
        int m_wifi_evt = -1;
        static void ota_task_func(void *arg);
};