/**
 * @file luf_statusbar.h
 *
 * 常驻顶部状态栏（全局单例）+ 状态栏项（局部项目，增删改查）。
 *
 * 状态栏本体挂在 lv_layer_top()，常驻、不随 lv_screen_load() 切换重建。
 * 状态栏项以 id 句柄对外暴露：句柄是一个不透明整数（非内存地址），
 * 框架内部用固定数组按 id 查表，并在 id 里编入代数，删除后旧 id 自动失效。
 * 同时存在的项数量上限由 LUF_STATUSBAR_ITEM_MAX 宏管控。
 */
#ifndef LUF_STATUSBAR_H
#define LUF_STATUSBAR_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 状态栏项句柄：不透明 id，LUF_STATUSBAR_ITEM_NONE 表示无效 */
typedef uint32_t luf_statusbar_item_t;
#define LUF_STATUSBAR_ITEM_NONE ((luf_statusbar_item_t)0)

/** 状态栏项所在的一侧 */
enum luf_statusbar_side {
    LUF_STATUSBAR_LEFT,
    LUF_STATUSBAR_RIGHT,
};

/** 通用状态栏项描述符：框架建好槽容器后回调 build 往里画任意内容 */
struct luf_statusbar_item_desc {
    enum luf_statusbar_side side;                 /**< 靠左簇或靠右簇 */
    void (*build)(lv_obj_t *parent, void *user); /**< 往槽里画 label/图形/自绘 */
    void *user;                                  /**< 透传给 build */
};

/**
 * @brief 建立常驻状态栏（全局单例，挂在 lv_layer_top()）
 *
 * 只建空壳：左簇 + 右簇两个 flex 容器，不含任何项内容。
 * 须在 LVGL/UI 任务上下文、lv_init() 之后调用一次。
 */
void luf_statusbar_init(void);

/**
 * @brief 新建一个通用状态栏项
 * @param desc 项描述符（含 build 回调）
 * @return 项句柄；无空位或参数非法时返回 LUF_STATUSBAR_ITEM_NONE
 */
luf_statusbar_item_t
luf_statusbar_item_add(const struct luf_statusbar_item_desc *desc);

/**
 * @brief 便捷封装：新建一个纯文本/符号项
 * @param side  靠左或靠右
 * @param text  文本（可含 LV_SYMBOL_*，按值拷入）
 * @param color 文字颜色 0xRRGGBB，传 0 用默认白
 * @return 项句柄；失败返回 LUF_STATUSBAR_ITEM_NONE
 */
luf_statusbar_item_t luf_statusbar_label_add(enum luf_statusbar_side side,
                                           const char *text, uint32_t color);

/**
 * @brief 修改文本项的文本（仅对 luf_statusbar_label_add 建的项有效）
 * @param item 项句柄
 * @param text 新文本（按值拷入）
 *
 * 须在 UI/LVGL 任务上下文调用；跨任务请用 luf_post() 投递到 UI 上下文。
 */
void luf_statusbar_label_set_text(luf_statusbar_item_t item, const char *text);

/**
 * @brief 显示/隐藏一个项（保留句柄与位置）
 * @param item   项句柄
 * @param hidden true 隐藏，false 显示
 */
void luf_statusbar_item_set_hidden(luf_statusbar_item_t item, bool hidden);

/**
 * @brief 释放一个项，回收其句柄
 * @param item 项句柄
 */
void luf_statusbar_item_remove(luf_statusbar_item_t item);

/**
 * @brief 当前在用项数量
 * @return 在用项个数
 */
int luf_statusbar_item_count(void);

/**
 * @brief 显示/隐藏整条状态栏（供全屏 App 使用）
 * @param hidden true 隐藏，false 显示
 */
void luf_statusbar_set_hidden(bool hidden);

#ifdef __cplusplus
}
#endif

#endif /* LUF_STATUSBAR_H */
