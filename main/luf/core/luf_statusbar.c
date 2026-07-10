/**
 * @file luf_statusbar.c
 */
#include "luf.h"
#include "luf_statusbar.h"

#define LUF_STATUSBAR_BG      0x101418
#define LUF_STATUSBAR_PAD_HOR 10
#define LUF_STATUSBAR_GAP     6

struct sb_item {
    uint16_t gen;    /* 代数，编入 id；删除后回绕递增使旧 id 失效 */
    bool used;       /* 槽位在用标志 */
    lv_obj_t *obj;   /* 槽对象：通用项为容器、便捷文本项为 label 本身 */
    lv_obj_t *label; /* 便捷文本项指向其 label；通用项为 NULL */
};

static lv_obj_t *bar;
static lv_obj_t *cluster[2]; /* [LUF_STATUSBAR_LEFT] / [LUF_STATUSBAR_RIGHT] */
static struct sb_item items[LUF_STATUSBAR_ITEM_MAX];

/* id 编码：低 16 位 = 槽下标+1，高 16 位 = 代数 */
static luf_statusbar_item_t encode(int idx, uint16_t gen)
{
    return ((luf_statusbar_item_t)gen << 16) | (luf_statusbar_item_t)(idx + 1);
}

static struct sb_item *resolve(luf_statusbar_item_t id)
{
    if (id == LUF_STATUSBAR_ITEM_NONE)
        return NULL;

    int idx = (int)(id & 0xFFFF) - 1;
    uint16_t gen = (uint16_t)(id >> 16);
    if (idx < 0 || idx >= LUF_STATUSBAR_ITEM_MAX)
        return NULL;

    struct sb_item *it = &items[idx];
    if (!it->used || it->gen != gen)
        return NULL;
    return it;
}

static struct sb_item *alloc_slot(int *out_idx)
{
    for (int i = 0; i < LUF_STATUSBAR_ITEM_MAX; i++) {
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

/* 建一个透明的项簇容器：横向 flex，按对齐方式贴左或贴右 */
static lv_obj_t *make_cluster(lv_flex_align_t main_align)
{
    lv_obj_t *c = lv_obj_create(bar);
    lv_obj_remove_style_all(c);
    lv_obj_set_size(c, LV_SIZE_CONTENT, LV_PCT(100));
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(c, main_align, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(c, LUF_STATUSBAR_GAP, 0);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_CLICKABLE);
    return c;
}

void luf_statusbar_init(void)
{
    bar = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, luf_hor_res(), LUF_STATUS_BAR_HEIGHT);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(LUF_STATUSBAR_BG), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(bar, LUF_STATUSBAR_PAD_HOR, 0);

    /* flex：左簇靠左、右簇靠右 */
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    /* 状态栏只是覆盖在内容之上，本身不吃触摸（下拉通知由手势状态机统一处理） */
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_CLICKABLE);

    cluster[LUF_STATUSBAR_LEFT] = make_cluster(LV_FLEX_ALIGN_START);
    cluster[LUF_STATUSBAR_RIGHT] = make_cluster(LV_FLEX_ALIGN_END);
}

luf_statusbar_item_t
luf_statusbar_item_add(const struct luf_statusbar_item_desc *desc)
{
    if (desc == NULL)
        return LUF_STATUSBAR_ITEM_NONE;
    if (desc->side != LUF_STATUSBAR_LEFT && desc->side != LUF_STATUSBAR_RIGHT)
        return LUF_STATUSBAR_ITEM_NONE;

    int idx;
    struct sb_item *it = alloc_slot(&idx);
    if (it == NULL)
        return LUF_STATUSBAR_ITEM_NONE;

    lv_obj_t *slot = lv_obj_create(cluster[desc->side]);
    lv_obj_remove_style_all(slot);
    lv_obj_set_size(slot, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_remove_flag(slot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(slot, LV_OBJ_FLAG_CLICKABLE);

    if (desc->build)
        desc->build(slot, desc->user);

    it->obj = slot;
    it->label = NULL;
    return encode(idx, it->gen);
}

luf_statusbar_item_t luf_statusbar_label_add(enum luf_statusbar_side side,
                                           const char *text, uint32_t color)
{
    if (side != LUF_STATUSBAR_LEFT && side != LUF_STATUSBAR_RIGHT)
        return LUF_STATUSBAR_ITEM_NONE;

    int idx;
    struct sb_item *it = alloc_slot(&idx);
    if (it == NULL)
        return LUF_STATUSBAR_ITEM_NONE;

    lv_obj_t *label = lv_label_create(cluster[side]);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(
        label, color ? lv_color_hex(color) : lv_color_white(), 0);
    lv_label_set_text(label, text ? text : "");

    it->obj = label;
    it->label = label;
    return encode(idx, it->gen);
}

void luf_statusbar_label_set_text(luf_statusbar_item_t item, const char *text)
{
    struct sb_item *it = resolve(item);
    if (it == NULL || it->label == NULL)
        return;
    lv_label_set_text(it->label, text ? text : "");
}

void luf_statusbar_item_set_hidden(luf_statusbar_item_t item, bool hidden)
{
    struct sb_item *it = resolve(item);
    if (it == NULL)
        return;
    if (hidden)
        lv_obj_add_flag(it->obj, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_remove_flag(it->obj, LV_OBJ_FLAG_HIDDEN);
}

void luf_statusbar_item_remove(luf_statusbar_item_t item)
{
    struct sb_item *it = resolve(item);
    if (it == NULL)
        return;
    lv_obj_delete(it->obj);
    it->obj = NULL;
    it->label = NULL;
    it->used = false; /* gen 留待下次同槽 alloc 时递增，使旧 id 失效 */
}

int luf_statusbar_item_count(void)
{
    int n = 0;
    for (int i = 0; i < LUF_STATUSBAR_ITEM_MAX; i++) {
        if (items[i].used)
            n++;
    }
    return n;
}

void luf_statusbar_set_hidden(bool hidden)
{
    if (bar == NULL)
        return;
    if (hidden)
        lv_obj_add_flag(bar, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_remove_flag(bar, LV_OBJ_FLAG_HIDDEN);
}
