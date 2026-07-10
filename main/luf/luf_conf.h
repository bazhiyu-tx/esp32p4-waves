/**
 * @file luf_conf.h
 *
 * Light UI Framework 配置集中：使用者在此调整可裁剪的参数（各类项目数量上限、
 * 状态栏高度等）。所有上限都是编译期固定、用于静态数组分配，无动态内存。
 */
#ifndef LUF_CONF_H
#define LUF_CONF_H

/* 状态栏高度（top layer 常驻），各页 / App 内容据此留出顶部边距 */
#define LUF_STATUS_BAR_HEIGHT 28

/* 同时存在的局部项目数量上限（静态数组容量） */
#define LUF_STATUSBAR_ITEM_MAX 16
#define LUF_NOTIFY_ITEM_MAX    32
#define LUF_DESKTOP_PAGE_MAX   8
#define LUF_APP_MAX            16

/* luf_osal 消息队列长度 */
#define LUF_OSAL_QUEUE_LEN     32

#endif /* LUF_CONF_H */
