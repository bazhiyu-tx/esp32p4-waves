/**
 * @file luf_app_mgr.h
 *
 * App 导航层（全局单例）：每个 App = 独立 screen，点击现建、返回转场放完现销，
 * 维护一个很小的返回栈记录导航路径。打开哪个 App 用 luf_app 注册表的句柄指定。
 */
#ifndef LUF_APP_MGR_H
#define LUF_APP_MGR_H

#include "lvgl.h"
#include "luf_app.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化导航层
 * @param home_screen 桌面 screen，作为返回栈底
 */
void luf_app_mgr_init(lv_obj_t *home_screen);

/**
 * @brief 打开一个 App：现建 screen + 转场动画
 * @param app App 句柄（来自 luf_app 注册表）
 */
void luf_app_mgr_open(luf_app_t app);

/**
 * @brief 在当前 App 内压入一张子页 screen，复用同一个返回栈
 * @param build      往子页 content 里画内容
 * @param arg        透传给 build（用来区分是哪个子页）
 * @param fullscreen 该子页是否全屏（隐状态栏 + 内容铺满）
 *
 * 边缘返回会逐层弹回上一页，底部上滑回桌面则一次清空整栈。
 */
void luf_app_mgr_push(void (*build)(lv_obj_t *content, void *arg), void *arg,
                     bool fullscreen);

/**
 * @brief 返回上一层（App→上一个 App，或 App→桌面）
 * @return true 表示发生了返回；false 表示已在桌面、无可返回
 */
bool luf_app_mgr_back(void);

/** @brief 一路回到桌面（清空整个 App 栈） */
void luf_app_mgr_go_home(void);

/** @brief 当前 App 栈深度，0 表示正停在桌面 */
int luf_app_mgr_depth(void);

/**
 * @brief 开关栈顶 App 内容的滚动（供手势状态机在底部上滑这一笔里临时禁用）
 * @param enable true 允许页内滚动，false 临时禁用
 */
void luf_app_mgr_top_scroll_enable(bool enable);

/**
 * @brief 运行时切换栈顶这一层的全屏（隐状态栏 + 内容铺满整屏）
 *
 * 全屏是每一层各自的属性：只改当前栈顶层；返回上一层时按那层的属性
 * 自动恢复状态栏。栈空（停在桌面）时无效。
 * @param on true 进入全屏，false 退出
 */
void luf_app_mgr_set_fullscreen(bool on);

/** @brief 当前栈顶层是否全屏（栈空返回 false） */
bool luf_app_mgr_is_fullscreen(void);

#ifdef __cplusplus
}
#endif

#endif /* LUF_APP_MGR_H */
