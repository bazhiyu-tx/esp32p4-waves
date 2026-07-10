/**
 * @file luf.c
 *
 * 框架总装：桌面 + 状态栏 + 通知面板 + App 层 + 手势状态机。
 * 只装配库本身，示例内容由 demo 层通过 luf 接口填充。
 */
#include "luf.h"

int32_t luf_hor_res(void)
{
    return lv_display_get_horizontal_resolution(lv_display_get_default());
}

int32_t luf_ver_res(void)
{
    return lv_display_get_vertical_resolution(lv_display_get_default());
}

void luf_init(void)
{
    /* 适配层先就绪，后续 luf_post() 才有队列可投递 */
    luf_osal_init();

    /* 桌面 screen 先建好并设为当前屏，作为 App 层的 home */
    lv_obj_t *home = luf_desktop_init();
    lv_screen_load(home);

    luf_app_mgr_init(home);

    /* 状态栏与通知面板建在 top layer，常驻、不随 screen 切换重建 */
    luf_statusbar_init();
    luf_notify_init();

    /* 手势状态机最后启动（依赖 tileview / app_mgr / notify 已就绪） */
    luf_gesture_init();
}
