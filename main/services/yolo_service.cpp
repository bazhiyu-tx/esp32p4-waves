#include "yolo_service.h"
#include "core/event_bus.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_cache.h"
#include "camera.h"

#include "dl_image_process.hpp"
#include "dl_image_ppa.hpp"

/* Global instance pointer for C wrapper functions */
static YoloService *g_yolo_instance = nullptr;

const char *YoloService::TAG = "YoloService";

// YOLO11n-pose anchor point stages (strides: 8, 16, 32)
static const std::vector<dl::detect::anchor_point_stage_t> YOLO11_POSE_STAGES = {
    { 8,  8,  0, 0},
    {16, 16,  0, 0},
    {32, 32,  0, 0},
};

YoloService::YoloService(EventBus &bus)
    : Service(bus, "yolo")
    , m_sd_sub_handle(-1)
    , m_model(nullptr)
    , m_preprocessor(nullptr)
    , m_postprocessor(nullptr)
    , m_ppa_handle(nullptr)
    , m_ppa_outbuf(nullptr)
    , m_ppa_dst_img{}
    , m_score_thr(0.25f)
    , m_nms_thr(0.65f)
    , m_top_k(100)
    , m_task(nullptr)
    , m_trigger_sem(nullptr)
    , m_result_mutex(nullptr)
    , m_new_result_flag(false)
{
}

YoloService::~YoloService()
{
    deinit();
}

esp_err_t YoloService::init()
{
    // Subscribe to SD card mounted event — load model when SD is ready
    m_sd_sub_handle = bus().on(EV_SDCARD_MOUNTED, [this](void *) {
        ESP_LOGI(TAG, "SD card mounted, loading model...");
        load_model();
    });

    // Also try loading immediately in case SD is already mounted
    load_model();

    // Create synchronization primitives
    m_trigger_sem = xSemaphoreCreateBinary();
    m_result_mutex = xSemaphoreCreateMutex();
    if (!m_trigger_sem || !m_result_mutex) {
        ESP_LOGE(TAG, "Failed to create semaphores");
        return ESP_FAIL;
    }

    // Create inference task on core 1, low priority. Stack in PSRAM.
    m_task = nullptr;
    ESP_LOGI(TAG, "Creating inference task (stack: 8KB)...");
    BaseType_t ret = xTaskCreatePinnedToCore(
        inference_task_func, "yolo_infer", 8 * 1024, this, 2, &m_task, 1);
    if (ret != pdPASS || !m_task) {
        // Try even smaller stack
        ESP_LOGW(TAG, "8KB failed, trying 4KB...");
        ret = xTaskCreatePinnedToCore(
            inference_task_func, "yolo_infer", 4 * 1024, this, 2, &m_task, 1);
    }
    if (ret != pdPASS || !m_task) {
        ESP_LOGE(TAG, "Failed to create inference task (%d)", ret);
        return ESP_FAIL;
    }
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create inference task");
        return ESP_FAIL;
    }

    // Register PPA SRM client + allocate output buffer (DMA-capable, no custom alignment)
    {
        ppa_client_config_t ppa_cfg = {PPA_OPERATION_SRM, 0, PPA_DATA_BURST_LENGTH_128};
        esp_err_t ppa_err = ppa_register_client(&ppa_cfg, &m_ppa_handle);
        if (ppa_err != ESP_OK) {
            ESP_LOGW(TAG, "PPA registration failed (%d), will use SW resize", ppa_err);
            m_ppa_handle = nullptr;
            m_ppa_outbuf = nullptr;
        } else {
            ESP_LOGI(TAG, "PPA SRM client registered");
            // Allocate DMA-capable PSRAM using plain calloc (avoid aligned_calloc which may corrupt heap)
            size_t outbuf_size = 640 * 640 * 3;
            m_ppa_outbuf = heap_caps_calloc(1, outbuf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
            if (m_ppa_outbuf) {
                m_ppa_dst_img.data = m_ppa_outbuf;
                m_ppa_dst_img.width = 640;
                m_ppa_dst_img.height = 640;
                m_ppa_dst_img.pix_type = dl::image::DL_IMAGE_PIX_TYPE_BGR888;
                ESP_LOGI(TAG, "PPA buffer allocated (%.1fMB DMA PSRAM)", outbuf_size / 1048576.0);
            } else {
                ESP_LOGE(TAG, "PPA buffer allocation failed!");
            }
        }
    }

    // Register global instance for C wrapper calls
    g_yolo_instance = this;

    ESP_LOGI(TAG, "YOLO service initialized (inference task created)");
    return ESP_OK;
}

esp_err_t YoloService::load_model()
{
    // Don't re-load if already loaded
    if (m_model) {
        return ESP_OK;
    }

    const char *model_path = "/sdcard/models/coco_pose_yolo11n_pose_s8_v1.espdl";

    // Check if SD is mounted and file exists
    FILE *f = fopen(model_path, "rb");
    if (!f) {
        ESP_LOGW(TAG, "Model file not found: %s (SD may not be ready yet)", model_path);
        return ESP_FAIL;
    }
    fclose(f);

    ESP_LOGI(TAG, "Loading model from %s ...", model_path);

    // Create model from SD card
    m_model = new dl::Model(
        model_path,
        fbs::MODEL_LOCATION_IN_SDCARD,
        0,                          // max_internal_size = 0 (internal SRAM too fragmented, use PSRAM)
        dl::MEMORY_MANAGER_GREEDY,
        nullptr,                    // key (no encryption)
        true                        // param_copy
    );
    // Note: minimize() will be called below after model is fully loaded.

    ESP_LOGI(TAG, "Model loaded successfully");

    // Print model input/output names for debugging
    {
        auto &inps = m_model->get_inputs();
        std::string names;
        for (auto &kv : inps) names += kv.first + " ";
        ESP_LOGI(TAG, "  inputs: %s", names.c_str());
    }
    {
        auto &outs = m_model->get_outputs();
        std::string names;
        for (auto &kv : outs) names += kv.first + " ";
        ESP_LOGI(TAG, "  outputs: %s", names.c_str());
    }

    // Create image preprocessor
    // rgb_swap=true because PPA will output BGR888 (the only RGB888-compatible
    // format it supports from RGB565LE input), and model expects RGB888.
    m_preprocessor = new dl::image::ImagePreprocessor(
        m_model,
        {0.0f, 0.0f, 0.0f},     // mean [0,255]
        {255.0f, 255.0f, 255.0f}, // std
        true                       // rgb_swap = true (PPA outputs BGR888, model needs RGB888)
    );
    m_preprocessor->enable_letterbox({0, 0, 0});

    // Create pose postprocessor
    m_postprocessor = new dl::detect::yolo11posePostProcessor(
        m_model,
        m_preprocessor,
        m_score_thr,
        m_nms_thr,
        m_top_k,
        YOLO11_POSE_STAGES
    );

    // Minimize model: free flatbuffer parsing metadata (after pre/post processor creation)
    m_model->minimize();

    ESP_LOGI(TAG, "YOLO11n-pose model loaded and ready");
    return ESP_OK;
}

void YoloService::deinit()
{
    if (m_sd_sub_handle >= 0) {
        bus().off(m_sd_sub_handle);
        m_sd_sub_handle = -1;
    }
    // Stop inference task
    if (m_task) {
        vTaskDelete(m_task);
        m_task = nullptr;
    }
    if (m_trigger_sem) {
        vSemaphoreDelete(m_trigger_sem);
        m_trigger_sem = nullptr;
    }
    if (m_result_mutex) {
        vSemaphoreDelete(m_result_mutex);
        m_result_mutex = nullptr;
    }
    // Free PPA resources
    if (m_ppa_handle) {
        ppa_unregister_client(m_ppa_handle);
        m_ppa_handle = nullptr;
    }
    if (m_ppa_outbuf) {
        heap_caps_free(m_ppa_outbuf);
        m_ppa_outbuf = nullptr;
    }

    delete m_postprocessor;
    m_postprocessor = nullptr;
    delete m_preprocessor;
    m_preprocessor = nullptr;
    delete m_model;
    m_model = nullptr;
    m_results.clear();
    ESP_LOGI(TAG, "YOLO service deinitialized");
}

esp_err_t YoloService::detect(const camera_frame_t *frame)
{
    if (!m_model || !m_preprocessor || !m_postprocessor) {
        ESP_LOGW(TAG, "Model not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    if (!frame || !frame->fb) {
        return ESP_ERR_INVALID_ARG;
    }

    // ── Step 1: PPA hardware resize 800x800 RGB565 → 640x640 RGB888 ──
    bool ppa_used = false;
    if (m_ppa_handle && m_ppa_outbuf) {
        dl::image::img_t src_img;
        src_img.data = frame->fb;
        src_img.width = frame->width;
        src_img.height = frame->height;
        src_img.pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB565LE;

        esp_err_t ret = dl::image::resize_ppa(src_img, m_ppa_dst_img, m_ppa_handle);
        if (ret == ESP_OK) {
            ppa_used = true;
            // Cache sync: ensure PPA-written data is visible to CPU
            esp_cache_msync(m_ppa_outbuf, 640 * 640 * 3, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
        } else {
            ESP_LOGW(TAG, "PPA resize failed (%d), falling back to SW resize", ret);
        }
    }

    // ── Step 2: Preprocess (normalize only if PPA did resize, else full SW) ──
    if (ppa_used) {
        // Image already at 640x640 RGB888 — preprocessor resize is 1:1 (fast)
        m_preprocessor->preprocess(m_ppa_dst_img);
    } else {
        // Fallback: convert frame to RGB888 in software, then full preprocess
        dl::image::img_t sw_img;
        // Allocate temp RGB888 buffer for fallback
        size_t sw_size = frame->width * frame->height * 3;
        uint8_t *sw_buf = (uint8_t *)heap_caps_malloc(sw_size, MALLOC_CAP_SPIRAM);
        if (!sw_buf) {
            ESP_LOGE(TAG, "Fallback: failed to alloc SW buffer");
            return ESP_ERR_NO_MEM;
        }
        // Simple RGB565 → BGR888 conversion (preprocessor has rgb_swap=true)
        uint16_t *src = (uint16_t *)frame->fb;
        uint8_t *dst = sw_buf;
        for (int i = 0; i < frame->width * frame->height; i++) {
            uint16_t p = src[i];
            uint8_t r = (p >> 8) & 0xF8;
            uint8_t g = (p >> 3) & 0xFC;
            uint8_t b = (p << 3) & 0xF8;
            *dst++ = b;  // B
            *dst++ = g;  // G
            *dst++ = r;  // R
        }
        sw_img.data = sw_buf;
        sw_img.width = frame->width;
        sw_img.height = frame->height;
        sw_img.pix_type = dl::image::DL_IMAGE_PIX_TYPE_BGR888;
        m_preprocessor->preprocess(sw_img);
        heap_caps_free(sw_buf);
    }

    // ── Step 3: Model inference ──
    m_model->run(m_preprocessor->get_model_input());

    // ── Step 4: Post-process ──
    m_postprocessor->postprocess();

    // Get results and map to original camera frame dimensions
    if (ppa_used) {
        // PPA path: postprocessor scale=1:1 (640→640), results in 640x640 space
        if (m_result_mutex) xSemaphoreTake(m_result_mutex, portMAX_DELAY);
        m_results = m_postprocessor->get_result(640, 640);
        if (m_result_mutex) xSemaphoreGive(m_result_mutex);
        // Scale up from 640x640 to original camera resolution
        float sx = (float)frame->width / 640.0f;
        float sy = (float)frame->height / 640.0f;
        for (auto &res : m_results) {
            for (int i = 0; i < 4; i++)
                res.box[i] = (int)(res.box[i] * sx + 0.5f);
            for (size_t i = 0; i < res.keypoint.size(); i += 2) {
                res.keypoint[i]     = (int)(res.keypoint[i]     * sx + 0.5f);
                res.keypoint[i + 1] = (int)(res.keypoint[i + 1] * sy + 0.5f);
            }
        }
    } else {
        // Software path: postprocessor already mapped to original dimensions
        if (m_result_mutex) xSemaphoreTake(m_result_mutex, portMAX_DELAY);
        m_results = m_postprocessor->get_result(frame->width, frame->height);
        if (m_result_mutex) xSemaphoreGive(m_result_mutex);
    }

    m_postprocessor->clear_result();

    return ESP_OK;
}

void YoloService::trigger()
{
    if (m_trigger_sem) {
        xSemaphoreGive(m_trigger_sem);
    }
}

std::list<dl::detect::result_t> YoloService::get_results_snapshot() const
{
    std::list<dl::detect::result_t> snap;
    YoloService *self = const_cast<YoloService *>(this);
    if (self->m_result_mutex) xSemaphoreTake(self->m_result_mutex, portMAX_DELAY);
    snap = self->m_results;
    if (self->m_result_mutex) xSemaphoreGive(self->m_result_mutex);
    return snap;
}

bool YoloService::consume_new_result_flag()
{
    bool flag = m_new_result_flag;
    m_new_result_flag = false;
    return flag;
}

void YoloService::inference_task_func(void *arg)
{
    YoloService *self = static_cast<YoloService *>(arg);
    ESP_LOGI(TAG, "Inference task started");

    // Inference can run for seconds without yielding; remove from watchdog supervision
    // (we rely on the main watchdog timeout and vTaskDelay for safety)
    // esp_task_wdt_add(NULL) is intentionally NOT called.

    while (1) {
        // Wait for trigger from LVGL timer
        if (xSemaphoreTake(self->m_trigger_sem, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        if (!self->is_model_loaded()) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        // Get the latest camera frame (RGB565 from V4L2)
        const camera_frame_t *frame = camera_get_frame();
        if (!frame || !frame->fb) {
            continue;
        }

        ESP_LOGI(TAG, "Inference start...");
        uint64_t t0 = esp_timer_get_time();

        // Run inference with PPA hardware resize (blocking, in our task)
        esp_err_t e = self->detect(frame);

        uint64_t t1 = esp_timer_get_time();

        if (e == ESP_OK) {
            self->m_new_result_flag = true;
            int n = (int)self->m_results.size();
            ESP_LOGI(TAG, "Inference done: %d objects in %llu ms",
                     n, (t1 - t0) / 1000);
            if (n > 0) {
                auto &r = self->m_results.front();
                ESP_LOGI(TAG, "  top: score=%.3f box=[%d,%d,%d,%d] kpt0=(%d,%d)",
                         r.score,
                         r.box[0], r.box[1], r.box[2], r.box[3],
                         r.keypoint.size() >= 2 ? r.keypoint[0] : -1,
                         r.keypoint.size() >= 2 ? r.keypoint[1] : -1);
            }
        } else {
            ESP_LOGW(TAG, "Inference failed");
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/* ========== C wrapper implementations ========== */

extern "C" void yolo_trigger(void)
{
    if (g_yolo_instance) {
        g_yolo_instance->trigger();
    }
}

extern "C" int yolo_has_result(void)
{
    if (!g_yolo_instance) return 0;
    return g_yolo_instance->consume_new_result_flag() ? 1 : 0;
}

extern "C" int yolo_get_count(void)
{
    if (!g_yolo_instance) return 0;
    return (int)g_yolo_instance->get_results_snapshot().size();
}

extern "C" int yolo_get_result(int index, float *score, int *box, int *kpts)
{
    if (!g_yolo_instance) return -1;
    auto results = g_yolo_instance->get_results_snapshot();
    if (index < 0 || index >= (int)results.size()) return -1;

    auto it = results.begin();
    std::advance(it, index);
    const auto &r = *it;

    if (score) *score = r.score;
    if (box) {
        box[0] = r.box[0];
        box[1] = r.box[1];
        box[2] = r.box[2];
        box[3] = r.box[3];
    }
    if (kpts) {
        for (int i = 0; i < 17 && i * 2 < (int)r.keypoint.size(); i++) {
            kpts[i * 2]     = r.keypoint[i * 2];
            kpts[i * 2 + 1] = r.keypoint[i * 2 + 1];
        }
    }
    return 0;
}
