/**
 * @file demo.h
 *
 * demo 装配层：只调用 port 接口搭出示例界面，不含框架机构。
 * 把"具体有哪些图标/通知/桌面页/App"这类内容集中在这里，
 * 与 port 接口层隔离。
 */
#ifndef DEMO_H
#define DEMO_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 从 SD 卡加载的中文字体，NULL 表示加载失败 */
extern lv_font_t *cn_font_16;
extern lv_font_t *cn_font_20;
void demo_fonts_init(void);

/**
 * @brief 一键装配全部示例内容（在 luf_init() 之后调用）
 */
void demo_init(void);

/**
 * @brief 用 port 接口填出示例状态栏内容（时钟 + WiFi + 电池）
 *
 * 须在 luf_statusbar_init() 之后调用。
 */
void demo_statusbar(void);

/**
 * @brief 用 port 接口推入示例通知卡片
 *
 * 须在 luf_notify_init() 之后调用。
 */
void demo_notify(void);

/**
 * @brief 用 port 接口注册示例 App 到注册表
 *
 * 须在 demo_desktop()（图标页据此排图标）之前调用。
 */
void demo_apps(void);

/**
 * @brief 用 port 接口注册示例桌面页（时钟 / 系统信息 / App 图标）
 *
 * 须在 luf_desktop_init() 之后调用。
 */
void demo_desktop(void);

/**
 * @brief 翻页时钟 App 的界面构建（供 demo_apps 注册为一个 App）
 * @param content App 内容容器
 */
void demo_flipclock(lv_obj_t *content);

#ifdef __cplusplus
}
#endif

#endif /* DEMO_H */
