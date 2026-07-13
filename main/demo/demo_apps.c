/**
 * @file demo_apps.c
 *
 * 示例 App：用 luf_app_register() 注册到注册表，桌面图标页据此排图标、
 * 点击经 luf_app_mgr 打开。各 App 的界面构建函数也在此。
 */
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "demo.h"
#include "demo_wifi.h"
#include "camera.h"
#include "luf_app.h"
#include "luf_app_mgr.h"
#include "luf_gesture.h"
#include "services/yolo_c_api.h"
#include "services/audio_c_api.h"

/* COCO 17-keypoint skeleton connections (pairs of keypoint indices) */
static const int SKELETON[][2] = {
    {0,1}, {0,2}, {1,3}, {2,4},       /* face */
    {5,6},                             /* shoulders */
    {5,7}, {7,9},                      /* left arm */
    {6,8}, {8,10},                     /* right arm */
    {11,12},                           /* hips */
    {5,11}, {6,12},                    /* torso */
    {11,13}, {13,15},                  /* left leg */
    {12,14}, {14,16},                  /* right leg */
};
#define SKELETON_NUM (sizeof(SKELETON) / sizeof(SKELETON[0]))

#define KPT_CONF_THR 0.5f

/* Detection results for drawing on freeze buffer */
static int   skel_kpts[34];
static int   skel_box_coords[4];  /* [x1,y1,x2,y2] */
static float skel_score = 0;
static bool  skel_valid = false;

/* Draw Bresenham line on RGB565 buffer */
static void skel_line(uint16_t *fb, int w, int x0, int y0, int x1, int y1, uint16_t color)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    while (1) {
        if (x0 >= 0 && x0 < w && y0 >= 0 && y0 < w)
            fb[y0 * w + x0] = color;
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

/* Draw bounding box */
static void skel_box(uint16_t *fb, int w, int x1, int y1, int x2, int y2, uint16_t color)
{
    int t = y1 < y2 ? y1 : y2, b = y1 > y2 ? y1 : y2;
    int l = x1 < x2 ? x1 : x2, r = x1 > x2 ? x1 : x2;
    for (int x = l; x <= r; x++) {
        if (t >= 0 && t < w && x >= 0 && x < w) fb[t * w + x] = color;
        if (b >= 0 && b < w && x >= 0 && x < w) fb[b * w + x] = color;
    }
    for (int y = t; y <= b; y++) {
        if (y >= 0 && y < w && l >= 0 && l < w) fb[y * w + l] = color;
        if (y >= 0 && y < w && r >= 0 && r < w) fb[y * w + r] = color;
    }
}

static void draw_skeleton_on_fb(uint16_t *fb, int w)
{
    int h = w, has_kpts = 0;

    /* Lines (no Y flip needed — model now receives right-side-up data) */
    for (int i = 0; i < SKELETON_NUM; i++) {
        int a = SKELETON[i][0], b = SKELETON[i][1];
        int ax = skel_kpts[a * 2], ay = skel_kpts[a * 2 + 1];
        int bx = skel_kpts[b * 2], by = skel_kpts[b * 2 + 1];
        if ((skel_kpts[a*2]==0 && skel_kpts[a*2+1]==0) ||
            (skel_kpts[b*2]==0 && skel_kpts[b*2+1]==0)) continue;
        has_kpts = 1;
        skel_line(fb, w, ax, ay, bx, by, 0x07E0);
    }
    /* Keypoint crosses */
    for (int i = 0; i < 17; i++) {
        int kx = skel_kpts[i * 2], ky = skel_kpts[i * 2 + 1];
        if (skel_kpts[i*2] == 0 && skel_kpts[i*2+1] == 0) continue;
        has_kpts = 1;
        skel_line(fb, w, kx-3, ky, kx+3, ky, 0xF800);
        skel_line(fb, w, kx, ky-3, kx, ky+3, 0xF800);
    }

    /* Fallback: draw bounding box if no keypoints detected */
    if (!has_kpts) {
        skel_box(fb, w,
                 skel_box_coords[0], skel_box_coords[1],
                 skel_box_coords[2], skel_box_coords[3],
                 0x07E0);
    }
}

/* 通用 App 内容：一个标题 + 一长串可纵向滚动的列表项。
 * 列表故意做长，用来验证页内纵向滚动与底部上滑回桌面的仲裁、
 * 以及左/右边缘横划返回不会被列表吃掉。 */
/* 点标题切换全屏：隐藏状态栏 + 内容铺满整屏，再点恢复 */
static void header_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    luf_app_mgr_set_fullscreen(!luf_app_mgr_is_fullscreen());
}

static void build_generic(lv_obj_t *content, const char *title, uint32_t color)
{
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(content, 16, 0);
    lv_obj_set_style_pad_row(content, 10, 0);

    lv_obj_t *header = lv_label_create(content);
    lv_obj_set_style_text_color(header, lv_color_hex(color), 0);
    lv_obj_set_style_text_font(header, &lv_font_montserrat_28, 0);
    lv_label_set_text(header, title);
    lv_obj_add_flag(header, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(header, header_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t *hint = lv_label_create(content);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x6b7480), 0);
    lv_label_set_text(hint, "Tap title: toggle fullscreen");

    for (int i = 1; i <= 12; i++) {
        lv_obj_t *row = lv_obj_create(content);
        lv_obj_remove_style_all(row);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_height(row, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(row, lv_color_hex(0x222831), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(row, 8, 0);
        lv_obj_set_style_pad_all(row, 14, 0);

        lv_obj_t *l = lv_label_create(row);
        lv_obj_set_style_text_color(l, lv_color_hex(0xc8cfd6), 0);
        lv_label_set_text_fmt(l, "%s item %d", title, i);
    }
}

/* ---- 设置 App：列表项点击进二级页，演示「返回回上一页 vs 上滑回桌面」 ---- */

struct setting_entry {
    const char *name;
    const char *desc;
    bool fullscreen; /* 二级页是否全屏显示 */
};

static const struct setting_entry settings_items[] = {
    {"Wi-Fi", "Manage Wi-Fi networks and connection.", false},
    {"Display", "Brightness, timeout and theme options.", false},
    {"Sound", "Ringtone, media volume and vibration.", false},
    {"About", "Device model, firmware and storage info.", true},
};

/* 二级页：显示所选设置项的标题 + 说明 */
static void build_setting_detail(lv_obj_t *content, void *arg)
{
    const struct setting_entry *it = arg;

    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(content, 16, 0);
    lv_obj_set_style_pad_row(content, 12, 0);

    lv_obj_t *title = lv_label_create(content);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_label_set_text(title, it->name);

    lv_obj_t *desc = lv_label_create(content);
    lv_obj_set_style_text_color(desc, lv_color_hex(0x9aa3ab), 0);
    lv_obj_set_width(desc, LV_PCT(100));
    lv_label_set_long_mode(desc, LV_LABEL_LONG_WRAP);
    lv_label_set_text(desc, it->desc);

    lv_obj_t *hint = lv_label_create(content);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x6b7480), 0);
    lv_label_set_text(hint, "Edge swipe: back to Settings\nSwipe up: go home");
}

static void setting_row_clicked(lv_event_t *e)
{
    const struct setting_entry *it = lv_event_get_user_data(e);

    /* Wi-Fi entry opens the real scanner instead of generic detail */
    if (strcmp(it->name, "Wi-Fi") == 0) {
        /* Build a real Wi-Fi scanner page */
        luf_app_mgr_push(demo_wifi_build, NULL, false);
        return;
    }

    luf_app_mgr_push(build_setting_detail, (void *)it, it->fullscreen);
}

/* 一级页：设置项列表，每行可点 → 压入二级页 */
static void build_settings(lv_obj_t *content)
{
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(content, 16, 0);
    lv_obj_set_style_pad_row(content, 10, 0);

    lv_obj_t *header = lv_label_create(content);
    lv_obj_set_style_text_color(header, lv_color_hex(0x4caf50), 0);
    lv_obj_set_style_text_font(header, &lv_font_montserrat_28, 0);
    lv_label_set_text(header, "Settings");

    int n = sizeof(settings_items) / sizeof(settings_items[0]);
    for (int i = 0; i < n; i++) {
        lv_obj_t *row = lv_obj_create(content);
        lv_obj_remove_style_all(row);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_height(row, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(row, lv_color_hex(0x222831), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(row, 8, 0);
        lv_obj_set_style_pad_all(row, 14, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row, setting_row_clicked, LV_EVENT_CLICKED,
                            (void *)&settings_items[i]);

        lv_obj_t *name = lv_label_create(row);
        lv_obj_set_style_text_color(name, lv_color_hex(0xc8cfd6), 0);
        lv_label_set_text(name, settings_items[i].name);

        lv_obj_t *chev = lv_label_create(row);
        lv_obj_set_style_text_color(chev, lv_color_hex(0x6b7480), 0);
        lv_label_set_text(chev, LV_SYMBOL_RIGHT);
    }
}

/* Clock App 自绘图标：teal 表盘 + 指向 3:00 的时针/分针 + 中心点 */
static void clock_icon(lv_obj_t *icon)
{
    lv_obj_set_style_radius(icon, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(icon, lv_color_hex(0x00bcd4), 0);
    lv_obj_set_style_bg_opa(icon, LV_OPA_COVER, 0);

    /* 分针：中心向上 */
    lv_obj_t *mh = lv_obj_create(icon);
    lv_obj_remove_style_all(mh);
    lv_obj_set_size(mh, 3, 20);
    lv_obj_set_style_radius(mh, 2, 0);
    lv_obj_set_style_bg_color(mh, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(mh, LV_OPA_COVER, 0);
    lv_obj_align(mh, LV_ALIGN_CENTER, 0, -10);

    /* 时针：中心向右 */
    lv_obj_t *hh = lv_obj_create(icon);
    lv_obj_remove_style_all(hh);
    lv_obj_set_size(hh, 15, 3);
    lv_obj_set_style_radius(hh, 2, 0);
    lv_obj_set_style_bg_color(hh, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(hh, LV_OPA_COVER, 0);
    lv_obj_align(hh, LV_ALIGN_CENTER, 7, 0);

    /* 中心点 */
    lv_obj_t *pin = lv_obj_create(icon);
    lv_obj_remove_style_all(pin);
    lv_obj_set_size(pin, 6, 6);
    lv_obj_set_style_radius(pin, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(pin, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(pin, LV_OPA_COVER, 0);
    lv_obj_center(pin);
}

static void build_music(lv_obj_t *c) { build_generic(c, "Music", 0xe91e63); }
static void build_photos(lv_obj_t *c) { build_generic(c, "Photos", 0xff9800); }
static void build_notes(lv_obj_t *c) { build_generic(c, "Notes", 0xffc107); }
static void build_phone(lv_obj_t *c) { build_generic(c, "Phone", 0x2196f3); }
static void build_messages(lv_obj_t *c)
{
    build_generic(c, "Messages", 0x00bcd4);
}

/* 摄像头 App 图标：镜头样式 */
static void camera_icon(lv_obj_t *icon)
{
    /* 镜头外圈 */
    lv_obj_set_style_radius(icon, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(icon, lv_color_hex(0x555555), 0);
    lv_obj_set_style_bg_opa(icon, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(icon, lv_color_hex(0x888888), 0);
    lv_obj_set_style_border_width(icon, 3, 0);

    /* 镜头内圈 */
    lv_obj_t *inner = lv_obj_create(icon);
    lv_obj_remove_style_all(inner);
    lv_obj_set_size(inner, 18, 18);
    lv_obj_set_style_radius(inner, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(inner, lv_color_hex(0x1a1a2e), 0);
    lv_obj_set_style_bg_opa(inner, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(inner, lv_color_hex(0x444444), 0);
    lv_obj_set_style_border_width(inner, 2, 0);
    lv_obj_center(inner);

    /* 镜头反光点 */
    lv_obj_t *dot = lv_obj_create(inner);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, 4, 4);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(0x88ccff), 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_align(dot, LV_ALIGN_TOP_LEFT, 4, 4);
}

static lv_obj_t   *camera_img     = NULL;
static lv_timer_t *camera_timer   = NULL;
static lv_obj_t   *camera_hint    = NULL;
static bool        camera_active  = false;
static volatile bool camera_detect_pending = false;
static bool        camera_frozen  = false;          /* show frozen frame with skeleton */
static uint16_t    *camera_freeze_buf = NULL;       /* saved frame for frozen display */
static lv_image_dsc_t camera_img_dsc = {
    .header = {
        .magic   = LV_IMAGE_HEADER_MAGIC,
        .cf      = LV_COLOR_FORMAT_RGB565,
        .w       = 800,
        .h       = 800,
        .stride  = 800 * 2,
        .flags   = 0,
    },
    .data_size = 800 * 800 * 2,
    .data      = NULL,
};

/* ---- Camera refresh ---- */

static void camera_refresh(lv_timer_t *tm)
{
    LV_UNUSED(tm);
    if (!camera_active || !camera_img) return;
    if (!lv_obj_is_valid(camera_img)) { camera_img = NULL; return; }

    if (camera_frozen) {
        /* When detection completes, draw skeleton on the frozen frame */
        if (camera_detect_pending && yolo_has_result()) {
            camera_detect_pending = false;

            int count = yolo_get_count();
            skel_valid = false;
            for (int i = 0; i < count && i < 1; i++) {
                float score;
                int box[4];
                if (yolo_get_result(i, &score, box, skel_kpts) == 0 && score >= KPT_CONF_THR) {
                    skel_valid = true;
                    skel_score = score;
                    skel_box_coords[0] = box[0];
                    skel_box_coords[1] = box[1];
                    skel_box_coords[2] = box[2];
                    skel_box_coords[3] = box[3];
                    draw_skeleton_on_fb(camera_freeze_buf, 800);
                    ESP_LOGI("camera", "Person: score=%.3f box=[%d,%d,%d,%d]",
                             score, box[0], box[1], box[2], box[3]);
                    break;
                }
            }
            if (camera_hint) {
                lv_label_set_text(camera_hint, "Tap shutter to continue");
                lv_obj_align(camera_hint, LV_ALIGN_BOTTOM_MID, 0, -20);
                lv_obj_clear_flag(camera_hint, LV_OBJ_FLAG_HIDDEN);
            }
            lv_obj_invalidate(camera_img);
        }

        /* Show frozen frame (stream still running but not displayed) */
        if (camera_freeze_buf) {
            camera_img_dsc.data = (const uint8_t *)camera_freeze_buf;
            lv_image_set_src(camera_img, &camera_img_dsc);
            lv_obj_invalidate(camera_img);
        }
        return;
    }

    const camera_frame_t *f = camera_get_frame();
    if (!f || !f->fb) return;

    camera_img_dsc.data = (const uint8_t *)f->fb;
    lv_image_set_src(camera_img, &camera_img_dsc);
    lv_obj_invalidate(camera_img);
}

static void camera_capture_clicked(lv_event_t *e)
{
    LV_UNUSED(e);

    /* Unfreeze: switch back to live preview (stream already running) */
    if (camera_frozen && !camera_detect_pending) {
        camera_frozen = false;
        skel_valid = false;
        if (camera_hint) {
            lv_label_set_text(camera_hint, "");
            lv_obj_add_flag(camera_hint, LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_invalidate(camera_img);
        return;
    }

    if (camera_detect_pending) return;

    /* Grab current frame, keep stream running */
    const camera_frame_t *f = camera_get_frame();
    if (!f || !f->fb) return;

    if (!camera_freeze_buf) {
        camera_freeze_buf = (uint16_t *)heap_caps_aligned_alloc(16, 800 * 800 * 2, MALLOC_CAP_SPIRAM);
    }
    if (!camera_freeze_buf) return;

    memcpy(camera_freeze_buf, f->fb, 800 * 800 * 2);

    /* Freeze display (stream keeps running underneath) */
    camera_frozen = true;
    camera_detect_pending = true;
    camera_img_dsc.data = (const uint8_t *)camera_freeze_buf;
    lv_image_set_src(camera_img, &camera_img_dsc);
    lv_obj_invalidate(camera_img);

    if (camera_hint) {
        lv_label_set_text(camera_hint, "AI detecting...");
        lv_obj_clear_flag(camera_hint, LV_OBJ_FLAG_HIDDEN);
    }

    yolo_trigger();
    ESP_LOGI("camera", "Shutter: freeze + trigger AI");
}

static void camera_page_close(lv_event_t *e)
{
    LV_UNUSED(e);
    camera_active = false;
    camera_detect_pending = false;
    camera_frozen = false;
    skel_valid = false;
    camera_img = NULL;
    camera_hint = NULL;
    if (camera_freeze_buf) {
        heap_caps_free(camera_freeze_buf);
        camera_freeze_buf = NULL;
    }
    if (camera_timer) {
        lv_timer_delete(camera_timer);
        camera_timer = NULL;
    }
    /* 恢复底部上滑手势 */
    luf_gesture_set_enabled(LUF_GESTURE_BOTTOM_HOME, true);
    camera_stop_stream();
}

static void build_camera(lv_obj_t *content)
{
    /* 清理上一轮残留的 timer 和流 */
    if (camera_timer) {
        lv_timer_delete(camera_timer);
        camera_timer = NULL;
    }
    camera_stop_stream();

    camera_active = true;
    /* 禁用底部上滑手势，避免抢走快门按钮的触摸事件 */
    luf_gesture_set_enabled(LUF_GESTURE_BOTTOM_HOME, false);
    lv_obj_set_style_bg_color(content, lv_color_hex(0x111111), 0);
    lv_obj_set_style_bg_opa(content, LV_OPA_COVER, 0);

    /* 立即启动摄像头流 */
    camera_start_stream();

    /* 取景器图像 */
    camera_img = lv_image_create(content);
    lv_obj_set_size(camera_img, 800, 800);
    lv_obj_align(camera_img, LV_ALIGN_TOP_MID, 0, 0);
    lv_image_set_scale(camera_img, 256);  /* 256 = 1:1 no scaling */

    /* "AI检测中..." 提示标签 */
    camera_hint = lv_label_create(content);
    lv_obj_set_style_text_color(camera_hint, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_color(camera_hint, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(camera_hint, LV_OPA_50, 0);
    lv_label_set_text(camera_hint, "");
    lv_obj_add_flag(camera_hint, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(camera_hint, LV_ALIGN_CENTER, 0, -40);

    /* 底部工具栏 */
    lv_obj_t *toolbar = lv_obj_create(content);
    lv_obj_remove_style_all(toolbar);
    lv_obj_set_size(toolbar, lv_pct(100), lv_pct(15));
    lv_obj_align(toolbar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(toolbar, lv_color_hex(0x222831), 0);
    lv_obj_set_style_bg_opa(toolbar, LV_OPA_COVER, 0);
    lv_obj_set_flex_flow(toolbar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(toolbar, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(toolbar, 16, 0);

    /* 拍照按钮 */
    lv_obj_t *btn = lv_btn_create(toolbar);
    lv_obj_set_size(btn, 64, 64);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(0xcccccc), 0);
    lv_obj_set_style_border_width(btn, 3, 0);
    lv_obj_add_event_cb(btn, camera_capture_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t *inner = lv_obj_create(btn);
    lv_obj_remove_style_all(inner);
    lv_obj_set_size(inner, 48, 48);
    lv_obj_set_style_radius(inner, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(inner, lv_color_hex(0xdddddd), 0);
    lv_obj_set_style_bg_opa(inner, LV_OPA_COVER, 0);
    lv_obj_center(inner);

    /* 页面销毁时停止流 */
    lv_obj_add_event_cb(content, camera_page_close, LV_EVENT_DELETE, NULL);

    /* 定时刷新预览 (33ms ≈ 30 FPS) */
    camera_timer = lv_timer_create(camera_refresh, 66, NULL);  /* 15 FPS — reduces cache sync overhead */
}

/************************** Audio Stream App **************************/

static void audio_stream_icon(lv_obj_t *icon)
{
    /* 紫色圆形背景 */
    lv_obj_set_style_radius(icon, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(icon, lv_color_hex(0x7B1FA2), 0);
    lv_obj_set_style_bg_opa(icon, LV_OPA_COVER, 0);

    /* 从 SD 卡加载图标图片 */
    lv_obj_t *img = lv_image_create(icon);
    lv_image_set_src(img, "S:/icons/audio_stream.jpg");
    lv_obj_center(img);
}

static void build_audio_stream(lv_obj_t *content)
{
    /* 点击图标时启动音频服务 */
    audio_start();

    lv_obj_t *label = lv_label_create(content);
    lv_label_set_text(label, LV_SYMBOL_VOLUME_MAX " Audio Stream\n\n"
                       "Status: Listening on port 5000\n\n"
                       "Send PCM audio from your PC\n"
                       "to start playing."
                       "\n\nVolume: " LV_SYMBOL_VOLUME_MID " 50%");
    lv_obj_center(label);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label, lv_obj_get_width(content) - 20);
}

void demo_apps(void)
{
    /* 字段顺序：name, symbol, icon_build, color, build, fullscreen */
    static const struct luf_app_desc apps[] = {
        {"Clock", NULL, clock_icon, 0x00bcd4, demo_flipclock, true},
        {"Settings", LV_SYMBOL_SETTINGS, NULL, 0x4caf50, build_settings, false},
        {"Music", LV_SYMBOL_AUDIO, NULL, 0xe91e63, build_music, false},
        {"Photos", LV_SYMBOL_IMAGE, NULL, 0xff9800, build_photos, false},
        {"Notes", LV_SYMBOL_EDIT, NULL, 0xffc107, build_notes, false},
        {"Phone", LV_SYMBOL_CALL, NULL, 0x2196f3, build_phone, false},
        {"Messages", LV_SYMBOL_ENVELOPE, NULL, 0x00bcd4, build_messages, false},
        {"Camera", NULL, camera_icon, 0x555555, build_camera, true},
        {"Audio Stream", NULL, audio_stream_icon, 0x7B1FA2, build_audio_stream, false},
    };
    for (unsigned i = 0; i < sizeof(apps) / sizeof(apps[0]); i++)
        luf_app_register(&apps[i]);
}
