/**
 * @file luf_gesture.h
 *
 * 全面屏手势 + 仲裁状态机（本项目工作量最大的部分）。
 *
 * 不把手势散落在各 callback 里：用一个 lv_timer 每帧轮询默认指针 indev
 * 的按下/坐标，驱动一个明确的状态机统一仲裁——
 *   - 左/右边缘横划 → 系统返回（跟手箭头 + 临时禁掉 tileview 滚动）
 *   - 底部上滑      → 回桌面
 *   - 顶部下拉      → 通知面板
 *   - 中间区域横划  → 交给 tileview 切页
 * 轮询只「观察」指针、不消费事件，所以 tileview / 页内滚动的正常手感不受影响。
 */
#ifndef LUF_GESTURE_H
#define LUF_GESTURE_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 可单独启停的手势类型 */
enum luf_gesture_type {
    LUF_GESTURE_EDGE_BACK,   /**< 左/右边缘横划返回 */
    LUF_GESTURE_BOTTOM_HOME, /**< 底部上滑回桌面 */
    LUF_GESTURE_NOTIFY,      /**< 顶部下拉打开通知面板 */
};

/**
 * @brief 启动手势状态机（轮询默认指针 indev）
 *
 * 依赖 tileview / app_mgr / notify 已就绪，须最后调用。
 */
void luf_gesture_init(void);

/**
 * @brief 启用/禁用某类手势
 *
 * 例如某 App 想临时关掉边缘返回或下拉通知。被禁用的手势在起手分流时
 * 不再独占该笔触摸，交还给底层（tileview / 页内滚动）。
 * @param type    手势类型
 * @param enabled true 启用，false 禁用
 */
void luf_gesture_set_enabled(enum luf_gesture_type type, bool enabled);

/**
 * @brief 查询某类手势是否启用
 * @param type 手势类型
 * @return 启用返回 true
 */
bool luf_gesture_is_enabled(enum luf_gesture_type type);

#ifdef __cplusplus
}
#endif

#endif /* LUF_GESTURE_H */
