/**
 * @file luf_app.c
 */
#include "luf_app.h"
#include "luf_conf.h"

struct app_slot {
    uint16_t gen; /* 代数，编入 id；注销后回绕递增使旧 id 失效 */
    bool used;
    struct luf_app_desc desc;
};

static struct app_slot slots[LUF_APP_MAX];

static luf_app_t encode(int idx, uint16_t gen)
{
    return ((luf_app_t)gen << 16) | (luf_app_t)(idx + 1);
}

static struct app_slot *resolve(luf_app_t id)
{
    if (id == LUF_APP_NONE)
        return NULL;

    int idx = (int)(id & 0xFFFF) - 1;
    uint16_t gen = (uint16_t)(id >> 16);
    if (idx < 0 || idx >= LUF_APP_MAX)
        return NULL;

    struct app_slot *s = &slots[idx];
    if (!s->used || s->gen != gen)
        return NULL;
    return s;
}

luf_app_t luf_app_register(const struct luf_app_desc *desc)
{
    if (desc == NULL)
        return LUF_APP_NONE;

    for (int i = 0; i < LUF_APP_MAX; i++) {
        if (slots[i].used)
            continue;
        slots[i].used = true;
        slots[i].gen++;
        if (slots[i].gen == 0)
            slots[i].gen = 1; /* 0 留作"未分配过"，回绕跳过 */
        slots[i].desc = *desc;
        return encode(i, slots[i].gen);
    }
    return LUF_APP_NONE;
}

void luf_app_unregister(luf_app_t app)
{
    struct app_slot *s = resolve(app);
    if (s == NULL)
        return;
    s->used = false; /* gen 留待下次同槽注册时递增，使旧 id 失效 */
}

int luf_app_count(void)
{
    int n = 0;
    for (int i = 0; i < LUF_APP_MAX; i++) {
        if (slots[i].used)
            n++;
    }
    return n;
}

luf_app_t luf_app_at(int index)
{
    int seen = 0;
    for (int i = 0; i < LUF_APP_MAX; i++) {
        if (!slots[i].used)
            continue;
        if (seen == index)
            return encode(i, slots[i].gen);
        seen++;
    }
    return LUF_APP_NONE;
}

const char *luf_app_name(luf_app_t app)
{
    struct app_slot *s = resolve(app);
    return s ? s->desc.name : NULL;
}

const char *luf_app_symbol(luf_app_t app)
{
    struct app_slot *s = resolve(app);
    return s ? s->desc.symbol : NULL;
}

uint32_t luf_app_color(luf_app_t app)
{
    struct app_slot *s = resolve(app);
    return s ? s->desc.color : 0;
}

bool luf_app_fullscreen(luf_app_t app)
{
    struct app_slot *s = resolve(app);
    return s ? s->desc.fullscreen : false;
}

bool luf_app_build_icon(luf_app_t app, lv_obj_t *icon)
{
    struct app_slot *s = resolve(app);
    if (s == NULL || s->desc.icon_build == NULL)
        return false;
    s->desc.icon_build(icon);
    return true;
}

void luf_app_build(luf_app_t app, lv_obj_t *content)
{
    struct app_slot *s = resolve(app);
    if (s == NULL || s->desc.build == NULL)
        return;
    s->desc.build(content);
}
