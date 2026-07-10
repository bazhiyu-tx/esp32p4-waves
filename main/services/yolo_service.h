#pragma once

#include "core/service.h"
#include "core/event_bus.h"

#include "dl_model_base.hpp"
#include "dl_image_preprocessor.hpp"
#include "dl_pose_yolo11_postprocessor.hpp"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

class YoloService : public Service {
public:
    YoloService(EventBus &bus);
    ~YoloService();

    esp_err_t init() override;
    void deinit() override;

    /// Load model from SD card (called on EV_SDCARD_MOUNTED or manually)
    esp_err_t load_model();

    /// Non-blocking trigger: signal the inference task to run on the latest frame
    void trigger();

    /// Run detection on an RGB888 image buffer (blocking, from inference task)
    esp_err_t detect(uint8_t *rgb888_data, int width, int height);

    /// Get the latest detection results (thread-safe snapshot)
    std::list<dl::detect::result_t> get_results_snapshot() const;

    /// Check if model is loaded
    bool is_model_loaded() const { return m_model != nullptr; }

    /// Check if a new result is available (clears flag)
    bool consume_new_result_flag();

private:
    static void inference_task_func(void *arg);

    int m_sd_sub_handle;

    dl::Model *m_model;
    dl::image::ImagePreprocessor *m_preprocessor;
    dl::detect::yolo11posePostProcessor *m_postprocessor;

    std::list<dl::detect::result_t> m_results;

    float m_score_thr;
    float m_nms_thr;
    int m_top_k;

    TaskHandle_t m_task;
    SemaphoreHandle_t m_trigger_sem;
    SemaphoreHandle_t m_result_mutex;
    volatile bool m_new_result_flag;

    static const char *TAG;
};
