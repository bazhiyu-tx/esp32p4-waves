/**
 * @file luf_desktop.h
 *
 * 桌面层：lv_tileview 一行多 tile（全局单例）+ 桌面页（局部项目，通用注册表）。
 *
 * 模型「空壳全建、内容现填」：每注册一页即追加一个空壳 tile，内容延迟到进入
 * 窗口（当前页 ±1）时才由 fill 回调填、离开窗口就 clear，框架按 VALUE_CHANGED
 * 自动维护这个滑动窗口。页以 luf_page_t 句柄对外，数量上限由 LUF_DESKTOP_PAGE_MAX
 * 管控。横划切页的吸附动画由 tileview 原生处理。
 */
#ifndef LUF_DESKTOP_H
#define LUF_DESKTOP_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 桌面页句柄：按追加顺序的不透明 id，LUF_PAGE_NONE 表示无效 */
typedef uint32_t luf_page_t;
#define LUF_PAGE_NONE ((luf_page_t)0)

/** 桌面页描述符 */
struct luf_page_desc {
    /** 框架建好 root（已留出状态栏顶部边距）后回调，往里填页面内容 */
    void (*fill)(lv_obj_t *root, void *user);
    /** 离开窗口时回调（清掉自己缓存的控件指针）；lv_obj_clean 由框架负责 */
    void (*clear)(void *user);
    void *user; /**< 透传给 fill / clear */
};

/**
 * @brief 建桌面 screen + 空壳 tileview（全局单例），返回 home screen
 *
 * 此时不含任何页；用 luf_desktop_page_add() 逐页注册。
 * 须在 LVGL/UI 任务上下文、lv_init() 之后调用一次。
 * @return 桌面 screen（作为 App 层的 home）
 */
lv_obj_t *luf_desktop_init(void);

/**
 * @brief 注册一个桌面页（追加到末尾一列）
 * @param desc 页描述符（fill / clear 回调）
 * @return 页句柄；满或参数非法时返回 LUF_PAGE_NONE
 */
luf_page_t luf_desktop_page_add(const struct luf_page_desc *desc);

/**
 * @brief 当前已注册页数量
 * @return 页个数
 */
int luf_desktop_page_count(void);

/**
 * @brief 切换到指定页
 * @param page 页句柄
 * @param anim 是否带吸附动画
 */
void luf_desktop_goto(luf_page_t page, bool anim);

/**
 * @brief 开关 tileview 横向滚动（供手势状态机在边缘返回这一笔里临时禁用）
 * @param enable true 允许横划切页，false 临时禁用
 */
void luf_desktop_scroll_enable(bool enable);

#ifdef __cplusplus
}
#endif

#endif /* LUF_DESKTOP_H */
