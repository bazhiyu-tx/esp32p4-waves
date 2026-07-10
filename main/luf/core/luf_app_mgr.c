/**
 * @file luf_app_mgr.c
 */
#include "luf.h"
#include "luf_app_mgr.h"
#include "luf_statusbar.h"

#define LUF_APP_STACK_MAX 8
#define LUF_APP_ANIM_MS   250

static lv_obj_t *home_scr;
static lv_obj_t *stack[LUF_APP_STACK_MAX];
static bool stack_fs[LUF_APP_STACK_MAX]; /* 各层是否全屏 */
static int depth;

void luf_app_mgr_init(lv_obj_t *home_screen)
{
    home_scr = home_screen;
    depth = 0;
}

int luf_app_mgr_depth(void)
{
    return depth;
}

/* 栈顶 App 的内容容器（content）；栈空（停在桌面）时返回 NULL */
static lv_obj_t *top_content(void)
{
    if (depth == 0)
        return NULL;
    return lv_obj_get_child(stack[depth - 1], 0);
}

void luf_app_mgr_top_scroll_enable(bool enable)
{
    lv_obj_t *c = top_content();
    if (c == NULL)
        return;
    if (enable)
        lv_obj_add_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    else
        lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
}

/* App content 顶部边距：全屏铺满（0），否则给状态栏让出 LUF_STATUS_BAR_HEIGHT */
static int content_top(bool fs)
{
    return fs ? 0 : LUF_STATUS_BAR_HEIGHT;
}

/* 同步状态栏可见性到某一层的全屏属性 */
static void apply_chrome(bool fs)
{
    luf_statusbar_set_hidden(fs);
}

/* 建一张 screen 壳：纯色背景 + content 容器（child 0），边距按是否全屏 */
static lv_obj_t *make_screen(bool fs)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x14181d), 0);

    lv_obj_t *content = lv_obj_create(scr);
    lv_obj_remove_style_all(content);
    int top = content_top(fs);
    lv_obj_set_size(content, luf_hor_res(), luf_ver_res() - top);
    lv_obj_set_pos(content, 0, top);
    lv_obj_set_style_pad_all(content, 0, 0);
    return scr;
}

/* 压一张新 screen 进返回栈并从右盖入；保留上一层（桌面或上一页） */
static void push_screen(lv_obj_t *scr, bool fs)
{
    stack[depth] = scr;
    stack_fs[depth] = fs;
    depth++;
    apply_chrome(fs);
    lv_screen_load_anim(scr, LV_SCR_LOAD_ANIM_OVER_LEFT, LUF_APP_ANIM_MS, 0,
                        false);
}

void luf_app_mgr_open(luf_app_t app)
{
    if (depth >= LUF_APP_STACK_MAX)
        return;

    bool fs = luf_app_fullscreen(app);
    lv_obj_t *scr = make_screen(fs);
    luf_app_build(app, lv_obj_get_child(scr, 0));
    push_screen(scr, fs);
}

void luf_app_mgr_push(void (*build)(lv_obj_t *content, void *arg), void *arg,
                     bool fullscreen)
{
    if (depth >= LUF_APP_STACK_MAX)
        return;

    lv_obj_t *scr = make_screen(fullscreen);
    if (build)
        build(lv_obj_get_child(scr, 0), arg);
    push_screen(scr, fullscreen);
}

bool luf_app_mgr_back(void)
{
    if (depth == 0)
        return false;

    lv_obj_t *prev = (depth >= 2) ? stack[depth - 2] : home_scr;
    depth--;

    /* 同步状态栏到返回后的那一层（回到桌面视为非全屏） */
    apply_chrome(depth >= 1 ? stack_fs[depth - 1] : false);

    /* 返回：上一层从右盖回，转场动画放完后自动删掉被替换的 top screen */
    lv_screen_load_anim(prev, LV_SCR_LOAD_ANIM_OVER_RIGHT, LUF_APP_ANIM_MS, 0,
                        true);
    return true;
}

void luf_app_mgr_go_home(void)
{
    if (depth == 0)
        return;

    apply_chrome(false); /* 回到桌面：恢复状态栏 */

    /* 瞬切桌面（转场由调用方的虹膜遮罩负责），切完再删掉所有 App screen */
    lv_screen_load(home_scr);
    for (int i = 0; i < depth; i++) {
        lv_obj_delete(stack[i]);
        stack[i] = NULL;
    }
    depth = 0;
}

void luf_app_mgr_set_fullscreen(bool on)
{
    if (depth == 0)
        return;

    stack_fs[depth - 1] = on;
    apply_chrome(on);

    /* 当前栈顶内容跟着调整边距：铺满 / 让出状态栏 */
    lv_obj_t *c = top_content();
    if (c != NULL) {
        int top = content_top(on);
        lv_obj_set_pos(c, 0, top);
        lv_obj_set_size(c, luf_hor_res(), luf_ver_res() - top);
    }
}

bool luf_app_mgr_is_fullscreen(void)
{
    return depth > 0 ? stack_fs[depth - 1] : false;
}
