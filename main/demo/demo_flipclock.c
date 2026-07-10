/**
 * @file demo_flipclock.c
 *
 * 翻页时钟 App：HH:MM:SS 六位数字，每位是一张机械翻页卡片。
 *
 * 每张卡片四层：上半静止 / 下半静止 / 上翻片 / 下翻片。中缝把数字分成上下两半，
 * 各半用裁剪容器 + 偏移的整字 label 只露对应半。翻页分两段、只动半页：
 *   1) 上翻片显示旧数字上半，绕中缝下折（scale_y 1→0）；折没后露出已换好的新上半。
 *   2) 下翻片显示新数字下半，从中缝翻下（scale_y 0→1）；落定后换好静止下半。
 * 时间走 luf_post → luf_pump 的消息链路在 UI 上下文刷新；App 关闭时经 content 的
 * DELETE 事件停掉定时器。
 */
#include "demo.h"
#include "luf.h"
#include <time.h>

#define FC_DIGITS  6
#define FC_CARD_W  44
#define FC_CARD_H  76
#define FC_HALF    (FC_CARD_H / 2)
#define FC_FOLD_MS 200

#define FC_BG   0x0c0f12
#define FC_CARD 0x1b2028

struct digitv {
    lv_obj_t *top_lbl; /* 上半静止 */
    lv_obj_t *bot_lbl; /* 下半静止 */
    lv_obj_t *ftop;    /* 上翻片容器（绕中缝下折） */
    lv_obj_t *ftop_lbl;
    lv_obj_t *fbot; /* 下翻片容器（从中缝翻下） */
    lv_obj_t *fbot_lbl;
    int val;
    int pending;
};

static struct {
    struct digitv d[FC_DIGITS];
    lv_timer_t *timer;
    bool alive;
    int line_h; /* 字体行高，用于把整字 label 偏成上/下半 */
} fc;

static void split_time(int d[FC_DIGITS])
{
    time_t now = time(NULL);
    if (now < 100000) {
        /* NTP not yet synced — fall back to uptime */
        uint32_t t = lv_tick_get() / 1000;
        int s = t % 60, m = (t / 60) % 60, h = (t / 3600) % 24;
        d[0] = h / 10; d[1] = h % 10;
        d[2] = m / 10; d[3] = m % 10;
        d[4] = s / 10; d[5] = s % 10;
    } else {
        struct tm ti;
        localtime_r(&now, &ti);
        d[0] = ti.tm_hour / 10; d[1] = ti.tm_hour % 10;
        d[2] = ti.tm_min / 10;  d[3] = ti.tm_min % 10;
        d[4] = ti.tm_sec / 10;  d[5] = ti.tm_sec % 10;
    }
}

static void scale_exec(void *var, int32_t v)
{
    /* scale 为 0 会在变换绘制里整数除零（lv_draw_sw_transform 的 256*256/scale），
     * S3 上触发 CPU 异常卡死；兜到最小 1，视觉上仍等同于压平 */
    if (v < 1)
        v = 1;
    lv_obj_set_style_transform_scale_y((lv_obj_t *)var, v, 0);
}

/* 建一个裁剪半片容器 + 内部整字 label（只露数字的上半或下半） */
static lv_obj_t *make_half(lv_obj_t *card, bool top, lv_obj_t **out_lbl)
{
    lv_obj_t *cont = lv_obj_create(card);
    lv_obj_remove_style_all(cont);
    lv_obj_set_size(cont, FC_CARD_W, FC_HALF);
    lv_obj_set_pos(cont, 0, top ? 0 : FC_HALF);
    /* 不透明底色：翻片要完全遮住下面的静止层，否则两个数字叠着透出来 */
    lv_obj_set_style_bg_color(cont, lv_color_hex(FC_CARD), 0);
    lv_obj_set_style_bg_opa(cont, LV_OPA_COVER, 0);
    lv_obj_remove_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(cont, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *lbl = lv_label_create(cont);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_48, 0);
    /* 整字在整卡片里垂直居中；上半容器露其上半，下半容器露其下半 */
    int y = (FC_CARD_H - fc.line_h) / 2 - (top ? 0 : FC_HALF);
    lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, y);

    *out_lbl = lbl;
    return cont;
}

static void set_digit_text(lv_obj_t *lbl, int v)
{
    lv_label_set_text_fmt(lbl, "%d", v);
}

/* 第二段完成：换好静止下半，收起下翻片 */
static void fbot_done(lv_anim_t *a)
{
    int i = (int)(intptr_t)lv_obj_get_user_data(a->var);
    struct digitv *d = &fc.d[i];
    set_digit_text(d->bot_lbl, d->pending);
    lv_obj_add_flag(d->fbot, LV_OBJ_FLAG_HIDDEN);
    d->val = d->pending;
}

/* 第一段完成（上翻片折没）：起第二段，下翻片从中缝翻下 */
static void ftop_done(lv_anim_t *a)
{
    int i = (int)(intptr_t)lv_obj_get_user_data(a->var);
    struct digitv *d = &fc.d[i];
    lv_obj_add_flag(d->ftop, LV_OBJ_FLAG_HIDDEN);

    set_digit_text(d->fbot_lbl, d->pending);
    lv_obj_remove_flag(d->fbot, LV_OBJ_FLAG_HIDDEN);

    lv_anim_t u;
    lv_anim_init(&u);
    lv_anim_set_var(&u, d->fbot);
    lv_anim_set_user_data(&u, (void *)(intptr_t)i);
    lv_anim_set_exec_cb(&u, scale_exec);
    lv_anim_set_values(&u, 0, LV_SCALE_NONE);
    lv_anim_set_duration(&u, FC_FOLD_MS);
    lv_anim_set_path_cb(&u, lv_anim_path_ease_out);
    lv_anim_set_completed_cb(&u, fbot_done);
    lv_anim_start(&u);
}

static void flip_digit(int i, int newv)
{
    struct digitv *d = &fc.d[i];
    d->pending = newv;

    /* 上翻片显示旧上半、盖住已换成新值的静止上半 */
    set_digit_text(d->ftop_lbl, d->val);
    set_digit_text(d->top_lbl, newv);
    lv_obj_set_style_transform_scale_y(d->ftop, LV_SCALE_NONE, 0);
    lv_obj_remove_flag(d->ftop, LV_OBJ_FLAG_HIDDEN);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, d->ftop);
    lv_anim_set_user_data(&a, (void *)(intptr_t)i);
    lv_anim_set_exec_cb(&a, scale_exec);
    lv_anim_set_values(&a, LV_SCALE_NONE, 0);
    lv_anim_set_duration(&a, FC_FOLD_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
    lv_anim_set_completed_cb(&a, ftop_done);
    lv_anim_start(&a);
}

static void apply_time(void *arg)
{
    LV_UNUSED(arg);
    if (!fc.alive)
        return;
    int d[FC_DIGITS];
    split_time(d);
    for (int i = 0; i < FC_DIGITS; i++) {
        if (d[i] != fc.d[i].val)
            flip_digit(i, d[i]);
    }
}

static void timer_cb(lv_timer_t *t)
{
    LV_UNUSED(t);
    luf_post(apply_time, NULL);
}

static void on_deleted(lv_event_t *e)
{
    LV_UNUSED(e);
    fc.alive = false;
    if (fc.timer != NULL) {
        lv_timer_delete(fc.timer);
        fc.timer = NULL;
    }
}

static void make_card(lv_obj_t *parent, int i, int digit)
{
    struct digitv *d = &fc.d[i];

    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, FC_CARD_W, FC_CARD_H);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_clip_corner(card, true, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(FC_CARD), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_CLICKABLE);

    /* 静止两半 */
    make_half(card, true, &d->top_lbl);
    make_half(card, false, &d->bot_lbl);
    set_digit_text(d->top_lbl, digit);
    set_digit_text(d->bot_lbl, digit);

    /* 上翻片：几何同上半，绕底边（中缝）下折 */
    d->ftop = make_half(card, true, &d->ftop_lbl);
    lv_obj_set_style_transform_pivot_x(d->ftop, FC_CARD_W / 2, 0);
    lv_obj_set_style_transform_pivot_y(d->ftop, FC_HALF, 0);
    lv_obj_set_user_data(d->ftop, (void *)(intptr_t)i);
    lv_obj_add_flag(d->ftop, LV_OBJ_FLAG_HIDDEN);

    /* 下翻片：几何同下半，绕顶边（中缝）翻下 */
    d->fbot = make_half(card, false, &d->fbot_lbl);
    lv_obj_set_style_transform_pivot_x(d->fbot, FC_CARD_W / 2, 0);
    lv_obj_set_style_transform_pivot_y(d->fbot, 0, 0);
    lv_obj_set_user_data(d->fbot, (void *)(intptr_t)i);
    lv_obj_add_flag(d->fbot, LV_OBJ_FLAG_HIDDEN);

    /* 中缝：压在最上层的深色细线 */
    lv_obj_t *hinge = lv_obj_create(card);
    lv_obj_remove_style_all(hinge);
    lv_obj_set_size(hinge, FC_CARD_W, 2);
    lv_obj_set_pos(hinge, 0, FC_HALF - 1);
    lv_obj_set_style_bg_color(hinge, lv_color_hex(FC_BG), 0);
    lv_obj_set_style_bg_opa(hinge, LV_OPA_COVER, 0);

    d->val = digit;
}

static void make_colon(lv_obj_t *parent)
{
    lv_obj_t *c = lv_label_create(parent);
    lv_obj_set_style_text_color(c, lv_color_hex(0x6b7480), 0);
    lv_obj_set_style_text_font(c, &lv_font_montserrat_48, 0);
    lv_label_set_text(c, ":");
}

void demo_flipclock(lv_obj_t *content)
{
    fc.alive = true;
    fc.timer = NULL;
    fc.line_h = lv_font_get_line_height(&lv_font_montserrat_48);

    lv_obj_set_style_bg_color(content, lv_color_hex(FC_BG), 0);
    lv_obj_set_style_bg_opa(content, LV_OPA_COVER, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(content, 4, 0);

    int dig[FC_DIGITS];
    split_time(dig);

    int di = 0;
    for (int pos = 0; pos < 8; pos++) {
        if (pos == 2 || pos == 5) {
            make_colon(content);
        } else {
            make_card(content, di, dig[di]);
            di++;
        }
    }

    lv_obj_add_event_cb(content, on_deleted, LV_EVENT_DELETE, NULL);
    fc.timer = lv_timer_create(timer_cb, 1000, NULL);
}
