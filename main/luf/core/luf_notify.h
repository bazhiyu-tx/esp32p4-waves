/**
 * @file luf_notify.h
 *
 * 下拉通知面板（全局单例）+ 通知项（局部项目，增删改查）。
 *
 * 面板本体建在 lv_layer_top()，初始藏在屏幕上方外（y = -h），由手势状态机
 * 驱动跟手下拉与回弹。通知项以 id 句柄对外暴露：句柄是不透明整数（非内存
 * 地址），框架内部用固定数组按 id 查表，并在 id 里编入代数，删除后旧 id
 * 自动失效——别的任务用 luf_post() 投递 id 来增删通知也安全。
 * 同时存在的通知项数量上限由 LUF_NOTIFY_ITEM_MAX 宏管控。
 */
#ifndef LUF_NOTIFY_H
#define LUF_NOTIFY_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 通知项句柄：不透明 id，LUF_NOTIFY_ITEM_NONE 表示无效 */
typedef uint32_t luf_notify_item_t;
#define LUF_NOTIFY_ITEM_NONE ((luf_notify_item_t)0)

/** 通用通知项描述符：框架建好卡片槽后回调 build 往里画任意内容 */
struct luf_notify_item_desc {
    void (*build)(lv_obj_t *parent, void *user); /**< 往槽里画任意内容 */
    void *user;                                  /**< 透传给 build */
};

/** 便捷通知卡片内容描述符 */
struct luf_notify_msg {
    const char *icon;  /**< 可选图标，含 LV_SYMBOL_*，NULL 不显示 */
    const char *title; /**< 可选标题，NULL 不显示 */
    const char *text;  /**< 正文，NULL 不显示 */
    uint32_t color;    /**< 标题/图标颜色 0xRRGGBB，传 0 用默认 */
};

/**
 * @brief 建立下拉通知面板（全局单例，挂在 lv_layer_top()）
 *
 * 只建空壳：面板 + 顶部标题，不含任何通知内容。初始藏在屏幕上方外。
 * 须在 LVGL/UI 任务上下文、lv_init() 之后调用一次。
 */
void luf_notify_init(void);

/**
 * @brief 新建一个通用通知项
 * @param desc 项描述符（含 build 回调）
 * @return 项句柄；无空位或参数非法时返回 LUF_NOTIFY_ITEM_NONE
 */
luf_notify_item_t luf_notify_item_add(const struct luf_notify_item_desc *desc);

/**
 * @brief 便捷封装：新建一张标准通知卡片（图标 + 标题 + 正文）
 * @param msg 卡片内容
 * @return 项句柄；失败返回 LUF_NOTIFY_ITEM_NONE
 */
luf_notify_item_t luf_notify_add(const struct luf_notify_msg *msg);

/**
 * @brief 修改一张通知卡片的内容（仅对 luf_notify_add 建的项有效）
 * @param item 项句柄
 * @param msg  新内容
 *
 * 须在 UI/LVGL 任务上下文调用；跨任务请用 luf_post() 投递到 UI 上下文。
 */
void luf_notify_update(luf_notify_item_t item, const struct luf_notify_msg *msg);

/**
 * @brief 释放一个通知项，回收其句柄
 * @param item 项句柄
 */
void luf_notify_item_remove(luf_notify_item_t item);

/**
 * @brief 清空所有通知项
 */
void luf_notify_clear_all(void);

/**
 * @brief 当前在用通知项数量
 * @return 在用项个数
 */
int luf_notify_count(void);

/* ---- 面板拖动控制（供手势状态机驱动跟手下拉与回弹） ---- */

/** @brief 面板总高度（像素） */
int luf_notify_height(void);

/** @brief 当前露出屏幕的像素数（0=收起，height=完全展开） */
int luf_notify_visible(void);

/** @brief 跟手拖动：设置当前露出屏幕的像素数（0=收起，height=完全展开） */
void luf_notify_set_visible(int px);

/** @brief 松手回弹：按目标态做回弹动画 */
void luf_notify_settle(bool open);

/** @brief 面板是否处于展开态 */
bool luf_notify_is_open(void);

#ifdef __cplusplus
}
#endif

#endif /* LUF_NOTIFY_H */
