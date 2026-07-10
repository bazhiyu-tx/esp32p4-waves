/**
 * @file luf_gesture.c
 */
#include "luf.h"
#include "luf_gesture.h"
#include "luf_desktop.h"
#include "luf_app_mgr.h"
#include "luf_notify.h"

/* 边缘返回指示器：贴边的竖条，按拉出距离自绘一团贝塞尔填充凸起 + 朝左箭头 */
#define IND_W         28    /* 指示器对象宽度（仅裁剪框，需 ≥ IND_MAX_DEPTH） */
#define IND_MAX_DEPTH 25    /* 凸起往屏内鼓出的最大深度（水平 x 方向） */
#define IND_BLOB_H    200    /* 凸起沿边的高度跨度（竖直 y 方向，固定，不随拉动变化） */
#define IND_ROUND     0.35f  /* 峰顶圆润度：贝塞尔控制点沿边外移比例，越大峰越钝越圆 */
#define IND_BLOB_COLOR 0x2b2f36 /* 凸起填充颜色 */
#define IND_BLOB_OPA  245    /* 凸起填充不透明度（固定，不随拉出进度变化） */
#define IND_ARROW_OPA 255    /* 箭头不透明度（固定，不随拉出进度变化） */

/* 凸起曲线求值方式：1 = 增量 t 前向差分（每步纯加法、无数组）；0 = 预采折线 + cubicf。
 * 两种方式都靠逐行覆盖率 AA 消除台阶。 */
#define IND_BLOB_FWD_DIFF 1

#if IND_BLOB_FWD_DIFF
/* 上下两段各步进 BLOB_FD_STEPS 次，行循环在相邻步进点间插值边界 x */
#define BLOB_FD_STEPS 24
#else
/* 上下两条三次贝塞尔各取 BLOB_SAMPLES/2 个采样点，行循环在其间插值边界 x */
#define BLOB_SAMPLES 48
#endif

/* 以下两项按屏宽比例换算成像素，且彼此独立：
 *   IND_FULL_RATIO   —— 凸起拉满（prog 到 100%）所需的水平拉出距离 = 屏宽 × 此比例
 *   IND_COMMIT_RATIO —— 触发返回所需的水平拉出距离 = 屏宽 × 此比例 */
#define IND_FULL_RATIO    0.35f
#define IND_COMMIT_RATIO  0.2f

#define IND_FULL_PX     ((int)(IND_FULL_RATIO   * luf_hor_res()))
#define IND_COMMIT_PX   ((int)(IND_COMMIT_RATIO * luf_hor_res()))

/* 手势仲裁的边缘带（单位 px，最终阈值要在实机上调） */
#define LUF_EDGE_BAND    24   /* 左/右边缘带宽度：落在此处的横划判为系统返回 */
#define LUF_BOTTOM_BAND  28   /* 底部带高度：落在此处的上滑判为回桌面 */

/* 底部上滑回桌面：圆形虹膜遮罩（圆心屏幕正中，半径随上滑线性收缩） */
#define HOME_FULL_RATIO   0.5f      /* 上滑达到 屏高×此比例 时圆缩到 0（虹膜全闭） */
#define HOME_COMMIT_FRAC  45        /* 进度(%) ≥ 此值即可回桌面；松手后动态缩满再切 */
#define IRIS_DIM_COLOR    0x0c0f12  /* 遮罩色＝桌面底色，缩满时与桌面无缝衔接 */
#define IRIS_DIM_OPA      LV_OPA_70 /* 拖动时遮罩半透明度 */
#define IRIS_COMMIT_MS    500       /* 松手后「动态缩满回桌面」收尾动画时长 */
#define IRIS_CANCEL_MS    500       /* 没拉够时「圆弹回满+淡出」收尾动画时长 */

/* 底部 home 指示细长条 */
#define PILL_W      120  /* 长条宽度 */
#define PILL_H      6    /* 长条高度 */
#define PILL_MARGIN 8    /* 距屏幕底边 */

enum gesture_state {
    GS_IDLE, /* 没按下 */
    GS_OTHER, /* 起手落在中间：放手不管，交给 tileview / 页内滚动 */
    GS_EDGE_BACK, /* 边缘横划返回 */
    GS_BOTTOM_HOME, /* 底部上滑回桌面 */
    GS_NOTIFY, /* 顶部下拉通知 */
};

enum edge_side { SIDE_LEFT, SIDE_RIGHT };

static lv_indev_t *pointer;
static lv_obj_t *indicator; /* 跟手返回指示器，常驻 top layer，平时隐藏 */
static lv_obj_t *home_pill; /* 底部上滑回桌面的状态细长条，常驻 top layer，平时隐藏 */
static lv_obj_t *iris; /* 底部上滑回桌面的圆形虹膜遮罩，常驻 top layer，平时隐藏 */
static int iris_radius; /* 当前透明圆半径；>= iris_rmax 时全透明不画 */
static int iris_rmax; /* 圆完全盖住屏幕所需半径（≈半对角线，整数近似） */
static int iris_top; /* 遮罩覆盖区上边界：非全屏避开状态栏，全屏盖满整屏 */
static lv_opa_t iris_opa; /* 当前遮罩不透明度 */
static int iris_a_r0, iris_a_r1, iris_a_o0, iris_a_o1; /* 收尾动画插值端点 */

static enum gesture_state state;
static bool was_pressed;
static int start_x, start_y;
static int notify_start_visible; /* 通知拖动起始露出量，做跟手增量 */
static enum edge_side side;

/* 指示器当前几何，喂给绘制回调 */
static enum edge_side ind_side;
static int ind_cy; /* 凸起中心 y（跟手指） */
static int ind_progress; /* 拉出进度 0..100 */
static bool ind_can_back; /* 已拉够 IND_COMMIT_PX：松手即返回，且据此显示箭头 */
static bool edge_closes_notify; /* 本次边缘返回是「收起通知面板」而非「App 返回」 */

static lv_indev_t *find_pointer(void)
{
    lv_indev_t *indev = NULL;
    while ((indev = lv_indev_get_next(indev)) != NULL) {
        if (lv_indev_get_type(indev) == LV_INDEV_TYPE_POINTER)
            return indev;
    }
    return NULL;
}

/* 箭头：拉够 ind_can_back 后显示朝左的字符 "<" */
static void indicator_draw_arrow(lv_layer_t *layer, float ex, float dir,
                                 float depth, float cy)
{
    if (!ind_can_back)
        return;
    int ax = (int)(ex + dir * depth * 0.5f);
    const lv_font_t *font = &lv_font_montserrat_18;
    lv_draw_label_dsc_t lb;
    lv_draw_label_dsc_init(&lb);
    lb.text = "<";
    lb.font = font;
    lb.color = lv_color_white();
    lb.opa = IND_ARROW_OPA;
    lb.align = LV_TEXT_ALIGN_CENTER;
    int fh = lv_font_get_line_height(font);
    lv_area_t la = {ax - 12, (int)cy - fh / 2, ax + 12, (int)cy + fh / 2};
    lv_draw_label(layer, &lb, &la);
}

#if LV_USE_VECTOR_GRAPHIC

/* 矢量方案：用三次贝塞尔路径填充凸起（平滑、抗锯齿）。
 * 需在 lv_conf.h 开启 LV_USE_VECTOR_GRAPHIC + LV_USE_THORVG_INTERNAL。 */
static void indicator_draw_cb(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    float prog = ind_progress / 100.0f;
    float depth = prog * IND_MAX_DEPTH; /* 鼓出深度随拉出进度线性增长 */
    if (depth < 1.0f)
        return;

    float w = luf_hor_res();
    float cy = ind_cy;
    float hh = IND_BLOB_H / 2.0f; /* 贴边高度跨度固定，不随拉动变化 */
    float ex = (ind_side == SIDE_LEFT) ? 0.0f : w;
    float dir = (ind_side == SIDE_LEFT) ? 1.0f : -1.0f; /* 凸起伸出方向 */
    float px = ex + dir * depth; /* 峰的 x */
    float r = IND_ROUND;

    /* 凸起轮廓：上边缘点 → 三次贝塞尔到峰 → 三次贝塞尔到下边缘点 → 沿边封口 */
    lv_vector_path_t *blob = lv_vector_path_create(
        LV_VECTOR_PATH_QUALITY_MEDIUM);
    lv_fpoint_t pt = {ex, cy - hh};
    lv_vector_path_move_to(blob, &pt);
    lv_fpoint_t c1 = {ex, cy - hh * r};
    lv_fpoint_t c2 = {px, cy - hh * r};
    lv_fpoint_t pk = {px, cy};
    lv_vector_path_cubic_to(blob, &c1, &c2, &pk);
    lv_fpoint_t c3 = {px, cy + hh * r};
    lv_fpoint_t c4 = {ex, cy + hh * r};
    lv_fpoint_t pb = {ex, cy + hh};
    lv_vector_path_cubic_to(blob, &c3, &c4, &pb);
    lv_vector_path_close(blob);

    lv_draw_vector_dsc_t *dsc = lv_draw_vector_dsc_create(layer);
    lv_draw_vector_dsc_set_fill_color(dsc, lv_color_hex(IND_BLOB_COLOR));
    lv_draw_vector_dsc_set_fill_opa(dsc, IND_BLOB_OPA);
    lv_draw_vector_dsc_set_stroke_opa(dsc, 0);
    lv_draw_vector_dsc_add_path(dsc, blob);
    lv_draw_vector(dsc);
    lv_draw_vector_dsc_delete(dsc);
    lv_vector_path_delete(blob);

    indicator_draw_arrow(layer, ex, dir, depth, cy);
}

#else /* !LV_USE_VECTOR_GRAPHIC */

/* AA 填一行：内侧实心列满 IND_BLOB_OPA，边界列按覆盖率给部分 opa，消除台阶。
 * xc 恒非负，(int)xc 即 floor，不用 math 库。 */
static void blob_draw_row(lv_layer_t *layer, lv_draw_rect_dsc_t *rd, int y,
                          float xc, int w)
{
    int ixc = (int)xc;
    float frac = xc - (float)ixc;
    lv_area_t a;
    a.y1 = y;
    a.y2 = y;
    if (ind_side == SIDE_LEFT) {
        /* 凸起在左：实心列 [0, ixc-1]，边界列 ixc 覆盖率 = frac */
        if (ixc >= 1) {
            rd->bg_opa = IND_BLOB_OPA;
            a.x1 = 0;
            a.x2 = ixc - 1;
            lv_draw_rect(layer, rd, &a);
        }
        if (frac > 0.0f) {
            rd->bg_opa = (lv_opa_t)(IND_BLOB_OPA * frac);
            a.x1 = ixc;
            a.x2 = ixc;
            lv_draw_rect(layer, rd, &a);
        }
    } else {
        /* 凸起在右：边界列 ixc 覆盖率 = 1-frac，实心列 [ixc+1, w-1] */
        rd->bg_opa = (lv_opa_t)(IND_BLOB_OPA * (1.0f - frac));
        a.x1 = ixc;
        a.x2 = ixc;
        lv_draw_rect(layer, rd, &a);
        if (ixc + 1 <= w - 1) {
            rd->bg_opa = IND_BLOB_OPA;
            a.x1 = ixc + 1;
            a.x2 = w - 1;
            lv_draw_rect(layer, rd, &a);
        }
    }
}

#if IND_BLOB_FWD_DIFF

/* 前向差分推进一段三次贝塞尔（y 单调增），逐整数行 AA 填充，无需采样数组。
 * 由控制点求 power-basis 初始差分后，每步仅 3 次加法推进 (x,y)；行中心被跨过时
 * 在相邻步进点间线性插值出边界 x。next_row 跨上下两段累进，接缝处不重行/漏行。 */
static void blob_segment_fd(lv_layer_t *layer, lv_draw_rect_dsc_t *rd, int w,
                            float x0, float x1, float x2, float x3,
                            float y0, float y1, float y2, float y3,
                            int *next_row)
{
    float h = 1.0f / BLOB_FD_STEPS, h2 = h * h, h3 = h2 * h;
    float ax3 = -x0 + 3.0f * x1 - 3.0f * x2 + x3, ax2 =
        3.0f * x0 - 6.0f * x1 + 3.0f * x2, ax1 = 3.0f * (x1 - x0);
    float ay3 = -y0 + 3.0f * y1 - 3.0f * y2 + y3, ay2 =
        3.0f * y0 - 6.0f * y1 + 3.0f * y2, ay1 = 3.0f * (y1 - y0);
    float px = x0, dx1 = ax3 * h3 + ax2 * h2 + ax1 * h, dx2 =
        6.0f * ax3 * h3 + 2.0f * ax2 * h2, dx3 = 6.0f * ax3 * h3;
    float py = y0, dy1 = ay3 * h3 + ay2 * h2 + ay1 * h, dy2 =
        6.0f * ay3 * h3 + 2.0f * ay2 * h2, dy3 = 6.0f * ay3 * h3;
    float prev_x = px, prev_y = py;
    for (int i = 0; i < BLOB_FD_STEPS; i++) {
        px += dx1;
        dx1 += dx2;
        dx2 += dx3;
        py += dy1;
        dy1 += dy2;
        dy2 += dy3;
        while (py > prev_y && (float)(*next_row) + 0.5f <= py) {
            float yc = (float)(*next_row) + 0.5f;
            if (yc <= prev_y)
                break; /* 该行已在更早步进点填过 */
            float tt = (yc - prev_y) / (py - prev_y);
            float xc = prev_x + (px - prev_x) * tt;
            blob_draw_row(layer, rd, *next_row, xc, w);
            (*next_row)++;
        }
        prev_x = px;
        prev_y = py;
    }
}

/* 前向差分增量求每行边界 x，逐行按覆盖率 AA 填充 */
static void indicator_draw_cb(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    float prog = ind_progress / 100.0f;
    float depth = prog * IND_MAX_DEPTH; /* 鼓出深度随拉出进度线性增长 */
    if (depth < 1.0f)
        return;

    float w = luf_hor_res();
    float cy = ind_cy;
    float hh = IND_BLOB_H / 2.0f; /* 贴边高度跨度固定，不随拉动变化 */
    float ex = (ind_side == SIDE_LEFT) ? 0.0f : w;
    float dir = (ind_side == SIDE_LEFT) ? 1.0f : -1.0f; /* 凸起伸出方向 */
    float px = ex + dir * depth; /* 峰的 x */
    float r = IND_ROUND;

    lv_draw_rect_dsc_t rd;
    lv_draw_rect_dsc_init(&rd);
    rd.bg_color = lv_color_hex(IND_BLOB_COLOR);

    int next_row = (int)(cy - hh + 0.5f);
    /* 上半：A(ex,cy-hh) → 峰(px,cy) */
    blob_segment_fd(layer, &rd, (int)w,
                    ex, ex, px, px,
                    cy - hh, cy - hh * r, cy - hh * r, cy, &next_row);
    /* 下半：峰(px,cy) → B(ex,cy+hh) */
    blob_segment_fd(layer, &rd, (int)w,
                    px, px, ex, ex,
                    cy, cy + hh * r, cy + hh * r, cy + hh, &next_row);

    indicator_draw_arrow(layer, ex, dir, depth, cy);
}

#else /* !IND_BLOB_FWD_DIFF */

static float cubicf(float a, float b, float c, float d, float t)
{
    float u = 1.0f - t;
    return u * u * u * a + 3.0f * u * u * t * b + 3.0f * u * t * t * c + t * t *
           t * d;
}

/* 把贝塞尔轮廓采成折线（ys 整条单调递增），逐整数行双指针插值出边界 x，
 * 再逐行按覆盖率 AA 填充。 */
static void indicator_draw_cb(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    float prog = ind_progress / 100.0f;
    float depth = prog * IND_MAX_DEPTH; /* 鼓出深度随拉出进度线性增长 */
    if (depth < 1.0f)
        return;

    float w = luf_hor_res();
    float cy = ind_cy;
    float hh = IND_BLOB_H / 2.0f; /* 贴边高度跨度固定，不随拉动变化 */
    float ex = (ind_side == SIDE_LEFT) ? 0.0f : w;
    float dir = (ind_side == SIDE_LEFT) ? 1.0f : -1.0f; /* 凸起伸出方向 */
    float px = ex + dir * depth; /* 峰的 x */
    float r = IND_ROUND;

    /* 采样轮廓：上半 A(ex,cy-hh)→峰(px,cy)，下半 峰→B(ex,cy+hh)，ys 整条单调递增 */
    int half = BLOB_SAMPLES / 2;
    float xs[BLOB_SAMPLES + 1], ys[BLOB_SAMPLES + 1];
    for (int i = 0; i <= half; i++) {
        float t = (float)i / half;
        xs[i] = cubicf(ex, ex, px, px, t);
        ys[i] = cubicf(cy - hh, cy - hh * r, cy - hh * r, cy, t);
    }
    for (int i = 1; i <= half; i++) {
        float t = (float)i / half;
        xs[half + i] = cubicf(px, px, ex, ex, t);
        ys[half + i] = cubicf(cy, cy + hh * r, cy + hh * r, cy + hh, t);
    }

    lv_draw_rect_dsc_t rd;
    lv_draw_rect_dsc_init(&rd);
    rd.bg_color = lv_color_hex(IND_BLOB_COLOR);

    int y_top = (int)(cy - hh + 0.5f);
    int y_bot = (int)(cy + hh + 0.5f);
    int wi = (int)w;
    int seg = 0;
    for (int y = y_top; y < y_bot; y++) {
        float yc = y + 0.5f; /* 行中心 */
        while (seg < BLOB_SAMPLES && ys[seg + 1] < yc)
            seg++;
        if (seg >= BLOB_SAMPLES)
            break;
        float span = ys[seg + 1] - ys[seg];
        float tt = span > 0.001f ? (yc - ys[seg]) / span : 0.0f;
        if (tt < 0.0f)
            tt = 0.0f;
        if (tt > 1.0f)
            tt = 1.0f;
        float xc = xs[seg] + (xs[seg + 1] - xs[seg]) * tt; /* 该行边界 x（浮点） */
        blob_draw_row(layer, &rd, y, xc, wi);
    }

    indicator_draw_arrow(layer, ex, dir, depth, cy);
}

#endif /* IND_BLOB_FWD_DIFF */

#endif /* LV_USE_VECTOR_GRAPHIC */

static void indicator_create(void)
{
    indicator = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(indicator);
    lv_obj_set_size(indicator, IND_W, luf_ver_res());
    lv_obj_set_style_bg_opa(indicator, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(indicator, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(indicator, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(indicator, indicator_draw_cb, LV_EVENT_DRAW_MAIN, NULL);
}

static void indicator_show(enum edge_side s, int cy, int progress)
{
    ind_side = s;
    ind_cy = cy;
    ind_progress = progress;
    /* 把竖条移到对应边，限定重绘范围 */
    lv_obj_set_pos(indicator, s == SIDE_LEFT ? 0 : luf_hor_res() - IND_W, 0);

    /* 提到 top layer 最前，确保凸起画在所有界面与其它覆盖层之上 */
    lv_obj_move_to_index(indicator, -1);
    lv_obj_remove_flag(indicator, LV_OBJ_FLAG_HIDDEN);
    lv_obj_invalidate(indicator);
}

static void indicator_hide(void)
{
    lv_obj_add_flag(indicator, LV_OBJ_FLAG_HIDDEN);
}

static void set_tileview_scroll(bool en)
{
    luf_desktop_scroll_enable(en);
}

/* 底部上滑这一笔里临时禁掉栈顶 App 的页内滚动，避免和回桌面手势双重生效 */
static void set_app_scroll(bool en)
{
    luf_app_mgr_top_scroll_enable(en);
}

static void home_pill_create(void)
{
    home_pill = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(home_pill);
    lv_obj_set_size(home_pill, PILL_W, PILL_H);
    lv_obj_set_style_radius(home_pill, PILL_H / 2, 0);
    lv_obj_set_style_bg_color(home_pill, lv_color_hex(0x888f98), 0);
    lv_obj_set_style_bg_opa(home_pill, LV_OPA_COVER, 0);
    lv_obj_remove_flag(home_pill, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(home_pill, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(home_pill, LV_ALIGN_BOTTOM_MID, 0, -PILL_MARGIN);
}

static void home_pill_show(void)
{
    lv_obj_set_style_bg_color(home_pill, lv_color_hex(0x888f98), 0);
    lv_obj_remove_flag(home_pill, LV_OBJ_FLAG_HIDDEN);
}

/* 进度达到回桌面阈值就把长条点亮成白色，提示松手即回桌面 */
static void home_pill_update(int progress)
{
    bool ready = progress >= HOME_COMMIT_FRAC;
    lv_obj_set_style_bg_color(home_pill,
                              ready ? lv_color_white() : lv_color_hex(0x888f98),
                              0);
}

static void home_pill_hide(void)
{
    lv_obj_add_flag(home_pill, LV_OBJ_FLAG_HIDDEN);
}

/* 上滑进度 0..100：以 屏高×HOME_FULL_RATIO 为「圆缩到 0」的满程 */
static int home_progress(int up)
{
    int full = (int)(HOME_FULL_RATIO * luf_ver_res());
    if (full < 1)
        full = 1;
    int p = up * 100 / full;
    if (p < 0)
        p = 0;
    if (p > 100)
        p = 100;
    return p;
}

/* 虹膜遮罩绘制（不依赖 ThorVG）：用 lv_draw_arc 画一圈黑色"甜甜圈"——
 * 内半径＝当前透明圆、外半径＝rmax（盖住全屏）→ 圆内透出 App、圆外蒙桌面底色。
 * 圆缩没了就直接铺一块满色。iris 对象只盖状态栏下方，故状态栏不会被蒙。 */
static void iris_draw_cb(lv_event_t *e)
{
    if (iris_radius >= iris_rmax)
        return;

    lv_layer_t *layer = lv_event_get_layer(e);
    int w = luf_hor_res();
    int h = luf_ver_res();

    if (iris_radius <= 1) {
        lv_draw_rect_dsc_t rd;
        lv_draw_rect_dsc_init(&rd);
        rd.bg_color = lv_color_hex(IRIS_DIM_COLOR);
        rd.bg_opa = iris_opa;
        lv_area_t a = {0, iris_top, w - 1, h - 1};
        lv_draw_rect(layer, &rd, &a);
        return;
    }

    lv_draw_arc_dsc_t ad;
    lv_draw_arc_dsc_init(&ad);
    ad.color = lv_color_hex(IRIS_DIM_COLOR);
    ad.opa = iris_opa;
    ad.center.x = w / 2;
    ad.center.y = h / 2;
    ad.radius = iris_rmax; /* 外半径 */
    ad.width = iris_rmax - iris_radius; /* 厚度＝外−内，内半径即当前透明圆 */
    ad.start_angle = 0;
    ad.end_angle = 360;
    lv_draw_arc(layer, &ad);
}

static void iris_create(void)
{
    /* 圆完全盖住「状态栏下方矩形」所需半径 ≈ 半对角线，用整数近似免去 sqrt */
    int a = luf_hor_res() / 2;
    int b = luf_ver_res() / 2;
    int mx = a > b ? a : b;
    int mn = a < b ? a : b;
    iris_rmax = mx + (mn * 106) / 256; /* alpha-max-beta-min：≈ √(a²+b²) */
    iris_radius = iris_rmax;
    iris_opa = IRIS_DIM_OPA;

    iris = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(iris);
    /* 默认只盖状态栏下方：圆弧画到状态栏区域的部分被对象裁掉，状态栏不被蒙；
     * 全屏（状态栏隐藏）时 iris_show 会把覆盖区扩到整屏 */
    iris_top = LUF_STATUS_BAR_HEIGHT;
    lv_obj_set_size(iris, luf_hor_res(), luf_ver_res() - iris_top);
    lv_obj_set_pos(iris, 0, iris_top);
    lv_obj_set_style_bg_opa(iris, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(iris, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(iris, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(iris, iris_draw_cb, LV_EVENT_DRAW_MAIN, NULL);
}

/* 起手：遮罩起始为全透明（圆 = rmax），随上滑收缩 */
static void iris_show(void)
{
    lv_anim_delete(iris, NULL); /* 停掉可能在跑的收尾动画 */

    /* 全屏 App 状态栏已隐藏，遮罩需盖满整屏，否则顶部会露出一条 */
    iris_top = luf_app_mgr_is_fullscreen() ? 0 : LUF_STATUS_BAR_HEIGHT;
    lv_obj_set_pos(iris, 0, iris_top);
    lv_obj_set_size(iris, luf_hor_res(), luf_ver_res() - iris_top);

    iris_radius = iris_rmax;
    iris_opa = IRIS_DIM_OPA;
    lv_obj_remove_flag(iris, LV_OBJ_FLAG_HIDDEN);
    lv_obj_invalidate(iris);
}

static void iris_track(int progress)
{
    iris_radius = iris_rmax * (100 - progress) / 100;
    /* 不透明度随进度从 IRIS_DIM_OPA 线性加深到全不透明，与收尾动画衔接 */
    iris_opa = (lv_opa_t)(
        IRIS_DIM_OPA + (LV_OPA_COVER - IRIS_DIM_OPA) * progress / 100);
    lv_obj_invalidate(iris);
}

static void iris_anim_exec(void *var, int32_t t)
{
    LV_UNUSED(var);
    iris_radius = iris_a_r0 + (iris_a_r1 - iris_a_r0) * t / 256;
    iris_opa = (lv_opa_t)(iris_a_o0 + (iris_a_o1 - iris_a_o0) * t / 256);
    lv_obj_invalidate(iris);
}

static void iris_reset_hidden(void)
{
    lv_obj_add_flag(iris, LV_OBJ_FLAG_HIDDEN);
    iris_radius = iris_rmax;
    iris_opa = IRIS_DIM_OPA;
}

/* 拉够了：动态把圆缩到 0、遮罩压到全不透明（＝桌面底色），瞬切桌面后隐藏 */
static void iris_done_home(lv_anim_t *a)
{
    LV_UNUSED(a);
    luf_app_mgr_go_home();
    iris_reset_hidden();
}

/* 没拉够：圆弹回满、遮罩淡出后隐藏 */
static void iris_done_cancel(lv_anim_t *a)
{
    LV_UNUSED(a);
    iris_reset_hidden();
}

static void iris_anim_to(int r1, lv_opa_t o1, int ms, lv_anim_ready_cb_t done)
{
    iris_a_r0 = iris_radius;
    iris_a_r1 = r1;
    iris_a_o0 = iris_opa;
    iris_a_o1 = o1;

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, iris);
    lv_anim_set_exec_cb(&a, iris_anim_exec);
    lv_anim_set_values(&a, 0, 256);
    lv_anim_set_duration(&a, ms);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_completed_cb(&a, done);
    lv_anim_start(&a);
}

/* 判定本笔为系统手势后调用：作废当前按压，余下的移动/抬手不再传给下层控件，
 * 手势独占这一笔（poll 仍直接读 indev 原始坐标，不受影响）。 */
static void claim_gesture(void)
{
    if (pointer)
        lv_indev_wait_release(pointer);
}

/* 起手分流：按 touch-down 落点决定这一笔归谁 */
/* 各类手势的启停开关（下标 = enum luf_gesture_type），默认全开 */
static bool gesture_on[3] = {true, true, true};

static bool gesture_enabled(enum luf_gesture_type type)
{
    return gesture_on[(int)type];
}

static void on_press(int x, int y)
{
    start_x = x;
    start_y = y;

    bool at_edge = (x < LUF_EDGE_BAND || x > luf_hor_res() - LUF_EDGE_BAND);
    bool in_app = luf_app_mgr_depth() > 0; /* depth==0 即停在桌面 */

    if (luf_notify_is_open()) {
        /* 面板已开：边缘横划 → 收起面板；其余 → 拖动面板。任何界面都生效 */
        if (at_edge) {
            edge_closes_notify = true;
            side = (x < LUF_EDGE_BAND) ? SIDE_LEFT : SIDE_RIGHT;
            ind_can_back = false;
            indicator_show(side, y, 0);
            state = GS_EDGE_BACK;
        } else {
            notify_start_visible = luf_notify_visible();
            state = GS_NOTIFY;
        }
        claim_gesture();
    } else if (y < LUF_STATUS_BAR_HEIGHT && gesture_enabled(LUF_GESTURE_NOTIFY)) {
        /* 顶部下拉 → 通知面板，任何界面（含桌面）都生效 */
        notify_start_visible = luf_notify_visible();
        state = GS_NOTIFY;
        claim_gesture();
    } else if (!in_app) {
        /* 桌面：不启用侧边返回 / 底部回桌面，横划交给 tileview、其余交给页内滚动 */
        state = GS_OTHER;
    } else if (at_edge && gesture_enabled(LUF_GESTURE_EDGE_BACK)) {
        /* 边缘横划 → 系统返回，并在这一笔里临时禁掉 tileview 滚动 */
        edge_closes_notify = false;
        side = (x < LUF_EDGE_BAND) ? SIDE_LEFT : SIDE_RIGHT;
        set_tileview_scroll(false);
        ind_can_back = false;
        indicator_show(side, y, 0);
        state = GS_EDGE_BACK;
        claim_gesture();
    } else if (y > luf_ver_res() - LUF_BOTTOM_BAND &&
               gesture_enabled(LUF_GESTURE_BOTTOM_HOME)) {
        /* 底部上滑 → 回桌面（此分支只在 App 内可达） */
        set_app_scroll(false);
        home_pill_show();
        iris_show();
        state = GS_BOTTOM_HOME;
        claim_gesture();
    } else {
        /* 中间区域 → 交给 tileview / 页内滚动 */
        state = GS_OTHER;
    }
}

static void on_pressing(int x, int y)
{
    switch (state) {
    case GS_EDGE_BACK: {
        int pull = (side == SIDE_LEFT) ? (x - start_x) : (start_x - x);
        if (pull < 0)
            pull = 0;
        int prog = pull * 100 / IND_FULL_PX;
        if (prog > 100)
            prog = 100;
        ind_can_back = (pull >= IND_COMMIT_PX); /* 够距离即可返回，箭头随之出现 */
        /* cy 锁在起手位置 start_y，不随手指上下移动 */
        indicator_show(side, start_y, prog);
        break;
    }
    case GS_NOTIFY:
        /* 跟手增量：从起始露出量加上手指纵向位移，从任意位置上拖都能收起 */
        luf_notify_set_visible(notify_start_visible + (y - start_y));
        break;
    case GS_BOTTOM_HOME: {
        int prog = home_progress(start_y - y);
        home_pill_update(prog);
        if (luf_app_mgr_depth() > 0)
            iris_track(prog);
        break;
    }
    default:
        break;
    }
}

static void on_release(int x, int y)
{
    switch (state) {
    case GS_EDGE_BACK: {
        int pull = (side == SIDE_LEFT) ? (x - start_x) : (start_x - x);
        set_tileview_scroll(true); /* 松手恢复 tileview 滚动 */
        indicator_hide();
        if (pull >= IND_COMMIT_PX) {
            if (edge_closes_notify)
                luf_notify_settle(false); /* 收起通知面板 */
            else
                luf_app_mgr_back();
        }
        break;
    }
    case GS_BOTTOM_HOME: {
        int prog = home_progress(start_y - y);
        set_app_scroll(true); /* 恢复 App 页内滚动 */
        home_pill_hide();
        if (luf_app_mgr_depth() > 0) {
            if (prog >= HOME_COMMIT_FRAC)
                iris_anim_to(0, LV_OPA_COVER, IRIS_COMMIT_MS, iris_done_home);
                /* 动态缩满再回桌面 */
            else
                iris_anim_to(iris_rmax, LV_OPA_TRANSP, IRIS_CANCEL_MS,
                             iris_done_cancel); /* 弹回淡出 */
        }
        break;
    }
    case GS_NOTIFY:
        luf_notify_settle(luf_notify_visible() > luf_notify_height() * 2 / 5);
        break;
    default:
        break;
    }
    state = GS_IDLE;
}

static void poll_cb(lv_timer_t *t)
{
    LV_UNUSED(t);
    if (pointer == NULL)
        return;

    lv_point_t p;
    lv_indev_get_point(pointer, &p);
    bool pressed = (lv_indev_get_state(pointer) == LV_INDEV_STATE_PRESSED);

    if (pressed && !was_pressed)
        on_press(p.x, p.y);
    else if (pressed && was_pressed)
        on_pressing(p.x, p.y);
    else if (!pressed && was_pressed)
        on_release(p.x, p.y);

    was_pressed = pressed;
}

void luf_gesture_init(void)
{
    pointer = find_pointer();
    indicator_create();
    iris_create();
    home_pill_create(); /* 最后建：细长条压在虹膜遮罩之上，不被蒙住 */
    state = GS_IDLE;
    was_pressed = false;

    /* 轮询指针，周期与 LVGL 读触摸/刷新同拍（LV_DEF_REFR_PERIOD），不过采样 */
    lv_timer_create(poll_cb, LV_DEF_REFR_PERIOD, NULL);
}

void luf_gesture_set_enabled(enum luf_gesture_type type, bool enabled)
{
    if ((int)type < 0 || (int)type >= (int)(sizeof(gesture_on) /
                                            sizeof(gesture_on[0])))
        return;
    gesture_on[(int)type] = enabled;
}

bool luf_gesture_is_enabled(enum luf_gesture_type type)
{
    if ((int)type < 0 || (int)type >= (int)(sizeof(gesture_on) /
                                            sizeof(gesture_on[0])))
        return false;
    return gesture_on[(int)type];
}
