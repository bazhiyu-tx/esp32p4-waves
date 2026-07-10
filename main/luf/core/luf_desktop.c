/**
 * @file luf_desktop.c
 */
#include "luf.h"
#include "luf_desktop.h"

#define LUF_DESKTOP_BG 0x0c0f12

struct page {
    lv_obj_t *tile;
    bool built;
    struct luf_page_desc desc;
};

static lv_obj_t *scr;
static lv_obj_t *tileview;
static struct page pages[LUF_DESKTOP_PAGE_MAX];
static int page_count;
static int active_idx;

/* 给页内容留出状态栏顶部边距的根容器 */
static lv_obj_t *page_root(lv_obj_t *tile)
{
    lv_obj_t *root = lv_obj_create(tile);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, luf_hor_res(), luf_ver_res() - LUF_STATUS_BAR_HEIGHT);
    lv_obj_set_pos(root, 0, LUF_STATUS_BAR_HEIGHT);
    lv_obj_set_style_pad_all(root, 16, 0);
    return root;
}

/* 已 built 直接返回；否则建 root 并回调 fill 填内容 */
static void build_page(int i)
{
    if (pages[i].built)
        return;
    lv_obj_t *root = page_root(pages[i].tile);
    if (pages[i].desc.fill)
        pages[i].desc.fill(root, pages[i].desc.user);
    pages[i].built = true;
}

/* 先回调 clear 让页清掉自己缓存的指针，再只删 tile 子对象、留壳 */
static void clear_page(int i)
{
    if (!pages[i].built)
        return;
    if (pages[i].desc.clear)
        pages[i].desc.clear(pages[i].desc.user);
    lv_obj_clean(pages[i].tile);
    pages[i].built = false;
}

/* 让 [idx-1, idx, idx+1] 为 built，窗口外的全部 clear（预加载 ±1 消除空窗） */
static void refresh_window(int idx)
{
    for (int i = 0; i < page_count; i++) {
        if (i >= idx - 1 && i <= idx + 1)
            build_page(i);
        else
            clear_page(i);
    }
}

static void tileview_changed(lv_event_t *e)
{
    LV_UNUSED(e);
    lv_obj_t *act = lv_tileview_get_tile_active(tileview);
    if (act == NULL)
        return;
    active_idx = (int)(intptr_t)lv_obj_get_user_data(act);
    refresh_window(active_idx);
}

lv_obj_t *luf_desktop_init(void)
{
    scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(LUF_DESKTOP_BG), 0);

    tileview = lv_tileview_create(scr);
    lv_obj_set_size(tileview, luf_hor_res(), luf_ver_res());
    lv_obj_set_style_bg_opa(tileview, LV_OPA_TRANSP, 0);
    lv_obj_set_scrollbar_mode(tileview, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(tileview, tileview_changed, LV_EVENT_VALUE_CHANGED,
                        NULL);

    page_count = 0;
    active_idx = 0;
    return scr;
}

luf_page_t luf_desktop_page_add(const struct luf_page_desc *desc)
{
    if (desc == NULL || page_count >= LUF_DESKTOP_PAGE_MAX)
        return LUF_PAGE_NONE;

    int i = page_count++;
    pages[i].tile = lv_tileview_add_tile(tileview, i, 0, LV_DIR_HOR);
    lv_obj_set_user_data(pages[i].tile, (void *)(intptr_t)i);
    pages[i].built = false;
    pages[i].desc = *desc;

    /* 新页落进当前窗口就立即填上（也保证开机第一屏非空） */
    refresh_window(active_idx);
    return (luf_page_t)(i + 1);
}

int luf_desktop_page_count(void)
{
    return page_count;
}

void luf_desktop_goto(luf_page_t page, bool anim)
{
    int idx = (int)page - 1;
    if (idx < 0 || idx >= page_count)
        return;
    lv_tileview_set_tile_by_index(tileview, idx, 0,
                                  anim ? LV_ANIM_ON : LV_ANIM_OFF);
}

void luf_desktop_scroll_enable(bool enable)
{
    if (tileview == NULL)
        return;
    if (enable)
        lv_obj_add_flag(tileview, LV_OBJ_FLAG_SCROLLABLE);
    else
        lv_obj_remove_flag(tileview, LV_OBJ_FLAG_SCROLLABLE);
}
