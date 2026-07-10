/**
 * @file luf_app.h
 *
 * App 注册表（局部项目，增删改查）。
 *
 * 注册表只管「有哪些 App、各自的名字/图标/底色/界面构建函数」，是纯机制；
 * 具体注册哪些 App 由上层（demo）决定。桌面图标页据此排图标，点击时把句柄
 * 交给 luf_app_mgr 打开。App 以 luf_app_t 句柄对外，数量上限由 LUF_APP_MAX 管控。
 */
#ifndef LUF_APP_H
#define LUF_APP_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** App 句柄：不透明 id，LUF_APP_NONE 表示无效 */
typedef uint32_t luf_app_t;
#define LUF_APP_NONE ((luf_app_t)0)

/** App 描述符（调用方填写后注册，框架按值拷走；字符串须为常量或长期存活） */
struct luf_app_desc {
    const char *name;                   /**< App 名 */
    const char *symbol;                 /**< 便捷图标：LV_SYMBOL_* 或文本（icon_build 为 NULL 时用） */
    void (*icon_build)(lv_obj_t *icon); /**< 可选：自定义图标，往 icon 区画任意内容 */
    uint32_t color;                     /**< 便捷图标底色 0xRRGGBB */
    void (*build)(lv_obj_t *content);   /**< 往 App content 容器画界面 */
    bool fullscreen;                    /**< 打开时是否全屏（隐状态栏 + 内容铺满） */
};

/**
 * @brief 注册一个 App
 * @param desc App 描述符
 * @return App 句柄；满或参数非法时返回 LUF_APP_NONE
 */
luf_app_t luf_app_register(const struct luf_app_desc *desc);

/**
 * @brief 注销一个 App
 * @param app App 句柄
 */
void luf_app_unregister(luf_app_t app);

/**
 * @brief 已注册 App 数量
 * @return App 个数
 */
int luf_app_count(void);

/**
 * @brief 按序号取 App 句柄（用于遍历排图标）
 * @param index 0 .. count-1
 * @return App 句柄；越界返回 LUF_APP_NONE
 */
luf_app_t luf_app_at(int index);

/** @brief 取 App 名；无效句柄返回 NULL */
const char *luf_app_name(luf_app_t app);

/** @brief 取 App 图标符号；无效句柄返回 NULL */
const char *luf_app_symbol(luf_app_t app);

/** @brief 取 App 图标底色；无效句柄返回 0 */
uint32_t luf_app_color(luf_app_t app);

/** @brief 该 App 是否声明为全屏打开；无效句柄返回 false */
bool luf_app_fullscreen(luf_app_t app);

/**
 * @brief 若该 App 提供了自定义图标，往 icon 区画出来
 * @param app  App 句柄
 * @param icon 图标区容器
 * @return true 已画自定义图标；false 该 App 无 icon_build（调用方画默认图标）
 */
bool luf_app_build_icon(luf_app_t app, lv_obj_t *icon);

/**
 * @brief 触发 App 的界面构建（由 luf_app_mgr 打开时调用）
 * @param app     App 句柄
 * @param content App 的内容容器
 */
void luf_app_build(luf_app_t app, lv_obj_t *content);

#ifdef __cplusplus
}
#endif

#endif /* LUF_APP_H */
