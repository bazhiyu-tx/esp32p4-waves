/**
 * @file luf_osal.h
 *
 * UI 与底层 OS 之间的适配层（消息传递）。
 *
 * 抽象接口与 OS 无关，UI 逻辑层只依赖本头文件；具体实现按 LV_USE_OS
 * 在 luf_osal_noos.c / luf_osal_freertos.c 中二选一编译，从而适配不同工程。
 *
 * 数据源（别的任务/中断）不直接调 LVGL，而是用 luf_post() 把"要在 UI
 * 上下文做的事"投递进来；UI 任务在主循环里调 luf_pump() 取出并执行，
 * 于是所有 lv_* 调用都发生在 UI 单线程，天然安全。
 */
#ifndef LUF_OSAL_H
#define LUF_OSAL_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 投递到 UI 上下文执行的工作回调 */
typedef void (*luf_work_cb)(void *arg);

/**
 * @brief 初始化适配层（建队列等）
 *
 * 须在首次 luf_post()/luf_pump() 之前调用一次。
 */
void luf_osal_init(void);

/**
 * @brief 投递一个工作到 UI 上下文执行
 * @param cb  在 UI 上下文执行的回调
 * @param arg 透传给回调的参数（生命周期须存活到被执行）
 * @return 成功投递返回 true；队列满或未初始化返回 false
 */
bool luf_post(luf_work_cb cb, void *arg);

/**
 * @brief 排空并执行所有待办工作
 *
 * 必须在 UI/LVGL 任务上下文调用，通常放在主循环里、lv_timer_handler()
 * 之前每轮调用一次。
 */
void luf_pump(void);

#ifdef __cplusplus
}
#endif

#endif /* LUF_OSAL_H */
