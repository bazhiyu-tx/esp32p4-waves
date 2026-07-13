/**
 * @file jpeg_decoder.h
 * @brief 硬件 JPEG 解码器（使用 ESP32-P4 内置 JPEG 硬件加速）
 *
 * 替代软件 TJPGD 解码，速度快、不卡 UI。
 */
#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 从 SD 卡加载 JPEG 文件，用硬件解码后创建 LVGL 图像
 * @param path  文件路径（VFS 路径，如 "/sdcard/icons/audio_stream.jpg"）
 * @return      lv_image_dsc_t 指针，失败返回 NULL
 *
 * @note 返回的 lv_image_dsc_t 由内部管理，不需要手动释放；
 *       重复调用 old 会被自动释放。
 */
lv_image_dsc_t *jpeg_hw_load(const char *path);

#ifdef __cplusplus
}
#endif
