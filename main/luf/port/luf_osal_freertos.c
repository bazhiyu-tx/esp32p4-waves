/**
 * @file luf_osal_freertos.c
 *
 * FreeRTOS 适配实现：用一个 FreeRTOS 队列承载工作，任务上下文投递、
 * UI 任务排空。中断上下文投递请用 luf_post_from_isr()。
 */
#include "lvgl.h"
#if LV_USE_OS == LV_OS_FREERTOS

#include "luf_osal.h"
#include "luf_conf.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

struct luf_work {
    luf_work_cb cb;
    void *arg;
};

static QueueHandle_t luf_queue;

void luf_osal_init(void)
{
    if (luf_queue == NULL)
        luf_queue = xQueueCreate(LUF_OSAL_QUEUE_LEN, sizeof(struct luf_work));
}

bool luf_post(luf_work_cb cb, void *arg)
{
    if (cb == NULL || luf_queue == NULL)
        return false;

    struct luf_work w = {cb, arg};
    return xQueueSend(luf_queue, &w, 0) == pdTRUE;
}

/**
 * @brief 中断上下文投递（FreeRTOS 专有，不在 luf_osal.h 抽象接口里）
 */
bool luf_post_from_isr(luf_work_cb cb, void *arg)
{
    if (cb == NULL || luf_queue == NULL)
        return false;

    struct luf_work w = {cb, arg};
    BaseType_t hp_task_woken = pdFALSE;
    BaseType_t ok = xQueueSendFromISR(luf_queue, &w, &hp_task_woken);
    portYIELD_FROM_ISR(hp_task_woken);
    return ok == pdTRUE;
}

void luf_pump(void)
{
    if (luf_queue == NULL)
        return;

    struct luf_work w;
    while (xQueueReceive(luf_queue, &w, 0) == pdTRUE)
        w.cb(w.arg);
}

#endif /* LV_USE_OS == LV_OS_FREERTOS */
