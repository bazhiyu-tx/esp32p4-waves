/**
 * @file camera.h
 * @brief OV5647 camera via esp_video + V4L2 API for ESP32-P4
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Camera frame info */
typedef struct {
    void    *fb;       /**< RGB565 frame buffer (width * height * 2 bytes) */
    size_t   size;     /**< Frame buffer size in bytes */
    uint32_t width;    /**< Frame width in pixels */
    uint32_t height;   /**< Frame height in pixels */
} camera_frame_t;

/**
 * @brief Initialize camera via esp_video + V4L2
 *
 * Must be called after display init (shares MIPI DSI LDO).
 *
 * @return ESP_OK on success
 */
esp_err_t camera_init(void);

/**
 * @brief Start continuous video streaming (VIDIOC_STREAMON)
 *
 * @return ESP_OK on success
 */
esp_err_t camera_start_stream(void);

/**
 * @brief Stop video streaming (VIDIOC_STREAMOFF)
 */
void camera_stop_stream(void);

/**
 * @brief Get the latest frame (DQBUF/QBUF)
 */
const camera_frame_t *camera_get_frame(void);

/**
 * @brief Get RGB888 frame buffer (BGR→RGB swapped) for AI inference
 *
 * @return pointer to RGB888 data (width * height * 3 bytes), or NULL if not ready
 */
const uint8_t *camera_get_frame_rgb888(void);

/**
 * @brief Deinitialize camera and release resources
 */
void camera_deinit(void);

#ifdef __cplusplus
}
#endif
