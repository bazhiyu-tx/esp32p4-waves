/**
 * @file luf_notify.c
 */
#include "luf.h"
#include "luf_notify.h"

#define LUF_NOTIFY_BG     0x1c2126
#define LUF_NOTIFY_CARD   0x2b323a
#define LUF_NOTIFY_TEXT   0xd0d6dd
#define LUF_NOTIFY_ANIM_MS 220

struct nt_item {
    uint16_t gen;  /* 代数，编入 id；删除后回绕递增使旧 id 失效 */
    bool used;     /* 槽位在用标志 */
    bool managed;  /* 便捷卡片（luf_notify_add）为 true，可被 luf_notify_update */
    lv_obj_t *slot;
};

static lv_obj_t *panel;
static int panel_h;
static bool is_open;
static struct nt_item items[LUF_NOTIFY_ITEM_MAX];

static luf_notify_item_t encode(int idx, uint16_t gen)
{
    return ((luf_notify_item_t)gen << 16) | (luf_notify_item_t)(idx + 1);
}

static struct nt_item *resolve(luf_notify_item_t id)
{
    if (id == LUF_NOTIFY_ITEM_NONE)
        return NULL;

    int idx = (int)(id & 0xFFFF) - 1;
    uint16_t gen = (uint16_t)(id >> 16);
    if (idx < 0 || idx >= LUF_NOTIFY_ITEM_MAX)
        return NULL;

    struct nt_item *it = &items[idx];
    if (!it->used || it->gen != gen)
        return NULL;
    return it;
}

static struct nt_item *alloc_slot(int *out_idx)
{
    for (int i = 0; i < LUF_NOTIFY_ITEM_MAX; i++) {
        if (items[i].used)
            continue;
        items[i].used = true;
        items[i].gen++;
        if (items[i].gen == 0)
            items[i].gen = 1; /* 0 留作"未分配过"，回绕跳过 */
        *out_idx = i;
        return &items[i];
    }
    return NULL;
}

static void set_y(lv_obj_t *obj, int32_t y)
{
    lv_obj_set_y(obj, y);
}

void luf_notify_init(void)
{
    panel_h = luf_ver_res(); /* 展开时全屏覆盖 */

    panel = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(panel);
    lv_obj_set_size(panel, luf_hor_res(), panel_h);
    lv_obj_set_style_bg_color(panel, lv_color_hex(LUF_NOTIFY_BG), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(panel, 0, 0);
    lv_obj_set_style_pad_all(panel, 12, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(panel, 8, 0);

    lv_obj_t *title = lv_label_create(panel);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
    lv_label_set_text(title, LV_SYMBOL_BELL "  Notifications");

    /* 初始藏在屏幕上方外 */
    set_y(panel, -panel_h);
    is_open = false;
}

/* 把便捷卡片内容画进槽：可选「图标+标题」头部行 + 可选正文 */
static void build_card(lv_obj_t *slot, const struct luf_notify_msg *msg)
{
    lv_obj_set_style_bg_color(slot, lv_color_hex(LUF_NOTIFY_CARD), 0);
    lv_obj_set_style_bg_opa(slot, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(slot, 8, 0);
    lv_obj_set_style_pad_all(slot, 10, 0);
    lv_obj_set_flex_flow(slot, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(slot, 4, 0);

    lv_color_t accent =
        msg->color ? lv_color_hex(msg->color) : lv_color_white();

    if (msg->icon || msg->title) {
        lv_obj_t *head = lv_obj_create(slot);
        lv_obj_remove_style_all(head);
        lv_obj_set_width(head, LV_PCT(100));
        lv_obj_set_height(head, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(head, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(head, 6, 0);

        if (msg->icon) {
            lv_obj_t *ic = lv_label_create(head);
            lv_obj_set_style_text_color(ic, accent, 0);
            lv_label_set_text(ic, msg->icon);
        }
        if (msg->title) {
            lv_obj_t *tt = lv_label_create(head);
            lv_obj_set_style_text_color(tt, accent, 0);
            lv_obj_set_style_text_font(tt, &lv_font_montserrat_16, 0);
            lv_label_set_text(tt, msg->title);
        }
    }

    if (msg->text) {
        lv_obj_t *body = lv_label_create(slot);
        lv_obj_set_style_text_color(body, lv_color_hex(LUF_NOTIFY_TEXT), 0);
        lv_obj_set_width(body, LV_PCT(100));
        lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
        lv_label_set_text(body, msg->text);
    }
}

/* 建一个挂在面板下的卡片槽容器 */
static lv_obj_t *make_slot(void)
{
    lv_obj_t *slot = lv_obj_create(panel);
    lv_obj_remove_style_all(slot);
    lv_obj_set_width(slot, LV_PCT(100));
    lv_obj_set_height(slot, LV_SIZE_CONTENT);
    lv_obj_remove_flag(slot, LV_OBJ_FLAG_SCROLLABLE);
    return slot;
}

luf_notify_item_t luf_notify_item_add(const struct luf_notify_item_desc *desc)
{
    if (desc == NULL)
        return LUF_NOTIFY_ITEM_NONE;

    int idx;
    struct nt_item *it = alloc_slot(&idx);
    if (it == NULL)
        return LUF_NOTIFY_ITEM_NONE;

    lv_obj_t *slot = make_slot();
    if (desc->build)
        desc->build(slot, desc->user);

    it->slot = slot;
    it->managed = false;
    return encode(idx, it->gen);
}

luf_notify_item_t luf_notify_add(const struct luf_notify_msg *msg)
{
    if (msg == NULL)
        return LUF_NOTIFY_ITEM_NONE;

    int idx;
    struct nt_item *it = alloc_slot(&idx);
    if (it == NULL)
        return LUF_NOTIFY_ITEM_NONE;

    lv_obj_t *slot = make_slot();
    build_card(slot, msg);

    it->slot = slot;
    it->managed = true;
    return encode(idx, it->gen);
}

void luf_notify_update(luf_notify_item_t item, const struct luf_notify_msg *msg)
{
    struct nt_item *it = resolve(item);
    if (it == NULL || !it->managed || msg == NULL)
        return;

    lv_obj_clean(it->slot);
    build_card(it->slot, msg);
}

void luf_notify_item_remove(luf_notify_item_t item)
{
    struct nt_item *it = resolve(item);
    if (it == NULL)
        return;

    lv_obj_delete(it->slot);
    it->slot = NULL;
    it->used = false; /* gen 留待下次同槽 alloc 时递增，使旧 id 失效 */
}

void luf_notify_clear_all(void)
{
    for (int i = 0; i < LUF_NOTIFY_ITEM_MAX; i++) {
        if (!items[i].used)
            continue;
        lv_obj_delete(items[i].slot);
        items[i].slot = NULL;
        items[i].used = false;
    }
}

int luf_notify_count(void)
{
    int n = 0;
    for (int i = 0; i < LUF_NOTIFY_ITEM_MAX; i++) {
        if (items[i].used)
            n++;
    }
    return n;
}

int luf_notify_height(void)
{
    return panel_h;
}

int luf_notify_visible(void)
{
    return lv_obj_get_y(panel) + panel_h;
}

void luf_notify_set_visible(int px)
{
    if (px < 0)
        px = 0;
    if (px > panel_h)
        px = panel_h;
    set_y(panel, px - panel_h);
}

static void anim_y_cb(void *obj, int32_t v)
{
    set_y((lv_obj_t *)obj, v);
}

void luf_notify_settle(bool open)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, panel);
    lv_anim_set_exec_cb(&a, anim_y_cb);
    lv_anim_set_values(&a, lv_obj_get_y(panel), open ? 0 : -panel_h);
    lv_anim_set_duration(&a, LUF_NOTIFY_ANIM_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
    is_open = open;
}

bool luf_notify_is_open(void)
{
    return is_open;
}
