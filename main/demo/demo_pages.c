/**
 * @file demo_pages.c
 *
 * 示例桌面页：时钟页、系统信息页、App 图标页，全部用 luf_desktop_page_add()
 * 注册到桌面。动态刷新页（时钟、系统信息）的控件指针缓存在本文件，clear
 * 回调里置 NULL，timer 据此判断「该不该刷新」，避免往已清空的壳里写。
 */
#include "luf.h"
#include <time.h>
#include "demo.h"
#include "luf_desktop.h"
#include "luf_app.h"
#include "luf_app_mgr.h"

static lv_obj_t *clock_big;
static lv_obj_t *clock_date;
static lv_obj_t *sysinfo_uptime;

/* ---- 时钟页 ---- */

static const char *wdays[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
static const char *months[] = {"Jan","Feb","Mar","Apr","May","Jun",
                               "Jul","Aug","Sep","Oct","Nov","Dec"};

static void clock_timer_cb(lv_timer_t *t);

static void fill_clock(lv_obj_t *root, void *user)
{
    LV_UNUSED(user);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(root, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    clock_big = lv_label_create(root);
    lv_obj_set_style_text_color(clock_big, lv_color_white(), 0);
    lv_obj_set_style_text_font(clock_big, &lv_font_montserrat_48, 0);

    clock_date = lv_label_create(root);
    lv_obj_set_style_text_color(clock_date, lv_color_hex(0x9aa3ab), 0);
    lv_obj_set_style_text_font(clock_date, &lv_font_montserrat_20, 0);

    /* 初次显示立即刷新 */
    clock_timer_cb(NULL);
}

static void clear_clock(void *user)
{
    LV_UNUSED(user);
    clock_big = NULL;
    clock_date = NULL;
}

static void clock_timer_cb(lv_timer_t *t)
{
    LV_UNUSED(t);
    if (clock_big == NULL) return;
    time_t now = time(NULL);
    if (now < 100000) {
        uint32_t s = lv_tick_get() / 1000;
        lv_label_set_text_fmt(clock_big, "%02lu:%02lu",
                              (unsigned long)((s / 3600) % 24),
                              (unsigned long)((s / 60) % 60));
        if (clock_date) lv_label_set_text(clock_date, "---");
    } else {
        struct tm ti;
        localtime_r(&now, &ti);
        lv_label_set_text_fmt(clock_big, "%02d:%02d", ti.tm_hour, ti.tm_min);
        if (clock_date) {
            lv_label_set_text_fmt(clock_date, "%s, %s %d %d",
                                  wdays[ti.tm_wday], months[ti.tm_mon],
                                  ti.tm_mday, ti.tm_year + 1900);
        }
    }
}

/* ---- 系统信息页 ---- */

static void fill_sysinfo(lv_obj_t *root, void *user)
{
    LV_UNUSED(user);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(root, 12, 0);

    lv_obj_t *title = lv_label_create(root);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_label_set_text(title, "System Info");

    sysinfo_uptime = lv_label_create(root);
    lv_obj_set_style_text_color(sysinfo_uptime, lv_color_hex(0xc8cfd6), 0);

    lv_obj_t *res = lv_label_create(root);
    lv_obj_set_style_text_color(res, lv_color_hex(0xc8cfd6), 0);
    lv_label_set_text_fmt(res, "Resolution: %d x %d", (int)luf_hor_res(),
                          (int)luf_ver_res());

    lv_obj_t *gfx = lv_label_create(root);
    lv_obj_set_style_text_color(gfx, lv_color_hex(0xc8cfd6), 0);
    lv_label_set_text(gfx, "Graphics: LVGL 9.5");
}

static void clear_sysinfo(void *user)
{
    LV_UNUSED(user);
    sysinfo_uptime = NULL;
}

static void sysinfo_timer_cb(lv_timer_t *t)
{
    LV_UNUSED(t);
    if (sysinfo_uptime == NULL)
        return;
    uint32_t s = lv_tick_get() / 1000;
    lv_label_set_text_fmt(sysinfo_uptime, "Uptime: %lu m %02lu s", (unsigned long)(s / 60), (unsigned long)(s % 60));
}

/* ---- App 图标页 ---- */

static void icon_clicked(lv_event_t *e)
{
    luf_app_t app = (luf_app_t)(uintptr_t)lv_event_get_user_data(e);
    luf_app_mgr_open(app);
}

static void fill_apps(lv_obj_t *root, void *user)
{
    LV_UNUSED(user);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(root, 16, 0);
    lv_obj_set_style_pad_column(root, 12, 0);

    int n = luf_app_count();
    for (int i = 0; i < n; i++) {
        luf_app_t app = luf_app_at(i);

        lv_obj_t *cell = lv_obj_create(root);
        lv_obj_remove_style_all(cell);
        lv_obj_set_size(cell, 80, 92);
        lv_obj_set_flex_flow(cell, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(cell, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(cell, 6, 0);
        lv_obj_add_flag(cell, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(cell, icon_clicked, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)app);

        lv_obj_t *badge = lv_obj_create(cell);
        lv_obj_remove_style_all(badge);
        lv_obj_set_size(badge, 56, 56);
        lv_obj_set_style_radius(badge, 14, 0);
        lv_obj_set_style_clip_corner(badge, true, 0);
        lv_obj_remove_flag(badge, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(badge, LV_OBJ_FLAG_CLICKABLE);

        /* App 自定义图标优先；否则用纯色底 + 居中符号的默认样式 */
        if (!luf_app_build_icon(app, badge)) {
            lv_obj_set_style_bg_color(badge, lv_color_hex(luf_app_color(app)), 0);
            lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);

            lv_obj_t *sym = lv_label_create(badge);
            lv_obj_center(sym);
            lv_obj_set_style_text_color(sym, lv_color_white(), 0);
            lv_obj_set_style_text_font(sym, &lv_font_montserrat_24, 0);
            lv_label_set_text(sym, luf_app_symbol(app));
        }

        lv_obj_t *name = lv_label_create(cell);
        lv_obj_set_style_text_color(name, lv_color_hex(0xd0d6dd), 0);
        lv_obj_set_style_text_font(name, &lv_font_montserrat_12, 0);
        lv_label_set_text(name, luf_app_name(app));
    }
}

void demo_desktop(void)
{
    static const struct luf_page_desc clock_pg = {fill_clock, clear_clock, NULL};
    static const struct luf_page_desc sysinfo_pg = {fill_sysinfo, clear_sysinfo,
                                                   NULL};
    static const struct luf_page_desc apps_pg = {fill_apps, NULL, NULL};

    luf_desktop_page_add(&clock_pg);
    luf_desktop_page_add(&sysinfo_pg);
    luf_desktop_page_add(&apps_pg);

    lv_timer_create(clock_timer_cb, 1000, NULL);
    lv_timer_create(sysinfo_timer_cb, 1000, NULL);
    clock_timer_cb(NULL);
    sysinfo_timer_cb(NULL);
}
