/**
 * @file luf.h
 *
 * Light UI Framework（luf）伞形头：使用者只需 include 本文件即可拿到全部接口。
 *
 * 库是纯 LVGL，不得 include 任何 esp_*.h、不得调用 ESP-IDF API。
 * PC（SDL2）与 ESP32-S3（esp_lvgl_port）共用同一份源码，只有底层
 * display flush / touch read 与 luf_osal 移植实现不同。
 */
#ifndef LUF_H
#define LUF_H

#include "lvgl.h"

#include "luf_conf.h"
#include "luf_osal.h"
#include "luf_statusbar.h"
#include "luf_notify.h"
#include "luf_desktop.h"
#include "luf_app.h"
#include "luf_app_mgr.h"
#include "luf_gesture.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 框架总入口：建桌面、状态栏、通知面板，启动手势状态机
 *
 * 必须在 lv_init() + HAL 初始化之后、在 LVGL/UI 任务上下文里调用。
 * 只装配库本身，不含任何示例内容（示例见 demo 层）。
 */
void luf_init(void);

/** @brief 当前屏幕横向分辨率（从默认 display 取，保持硬件无关） */
int32_t luf_hor_res(void);

/** @brief 当前屏幕纵向分辨率 */
int32_t luf_ver_res(void);

#ifdef __cplusplus
}
#endif

#endif /* LUF_H */
