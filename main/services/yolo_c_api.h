/**
 * @file yolo_c_api.h
 * @brief C-compatible wrapper for YOLO inference (included from C files)
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Trigger YOLO inference on the latest camera frame (non-blocking).
 * Call this from LVGL timer; returns immediately.
 * Inference runs in a separate FreeRTOS task.
 */
void yolo_trigger(void);

/**
 * @brief Check if a new inference result is available since last check.
 * @return 1 if new result available, 0 otherwise
 */
int yolo_has_result(void);

/**
 * @brief Get number of detected objects in the latest frame.
 */
int yolo_get_count(void);

/**
 * @brief Get the bounding box and keypoints of a detected object.
 * @param index  0-based index
 * @param score  [out] confidence score
 * @param box    [out] int[4] = {x1, y1, x2, y2} (can be NULL)
 * @param kpts   [out] int[34] = {x0,y0, x1,y1, ..., x16,y16} (can be NULL)
 * @return 0 on success, -1 if index out of range
 */
int yolo_get_result(int index, float *score, int *box, int *kpts);

#ifdef __cplusplus
}
#endif
