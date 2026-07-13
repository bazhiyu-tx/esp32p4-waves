/**
 * @file jpeg_decoder.c
 * @brief 硬件 JPEG 解码器实现
 *
 * 流程：
 *   1. fopen 读取 SD 卡上的 JPEG 文件
 *   2. esp_new_jpeg 硬件解码 → RGB888
 *   3. 构造 lv_image_dsc_t 供 LVGL 直接使用
 */
#include "jpeg_decoder.h"
#include "esp_log.h"
#include "esp_jpeg_dec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "JPEG_HW";

/* 缓存最后一次解码的图像，用于 LVGL 引用 */
static lv_image_dsc_t *s_cached_dsc = NULL;

lv_image_dsc_t *jpeg_hw_load(const char *path)
{
    /* ---- 1. 读取文件到内存 ---- */
    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open %s", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long file_len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (file_len <= 0) {
        ESP_LOGE(TAG, "Empty file: %s", path);
        fclose(f);
        return NULL;
    }

    uint8_t *jpeg_data = malloc(file_len);
    if (!jpeg_data) {
        ESP_LOGE(TAG, "OOM for JPEG data (%ld bytes)", file_len);
        fclose(f);
        return NULL;
    }
    if (fread(jpeg_data, 1, file_len, f) != (size_t)file_len) {
        ESP_LOGE(TAG, "Read error: %s", path);
        free(jpeg_data);
        fclose(f);
        return NULL;
    }
    fclose(f);

    /* ---- 2. 硬件解码 ---- */
    jpeg_dec_handle_t dec = NULL;
    jpeg_dec_config_t cfg = DEFAULT_JPEG_DEC_CONFIG();
    cfg.output_type = JPEG_PIXEL_FORMAT_RGB888;

    jpeg_error_t err = jpeg_dec_open(&cfg, &dec);
    if (err != JPEG_ERR_OK) {
        ESP_LOGE(TAG, "jpeg_dec_open failed: %d", err);
        free(jpeg_data);
        return NULL;
    }

    /* 解析头部获取尺寸 */
    jpeg_dec_io_t io = {
        .inbuf = jpeg_data,
        .inbuf_len = (int)file_len,
        .inbuf_remain = 0,
        .outbuf = NULL,
        .out_size = 0,
    };
    jpeg_dec_header_info_t info;
    err = jpeg_dec_parse_header(dec, &io, &info);
    if (err != JPEG_ERR_OK) {
        ESP_LOGE(TAG, "jpeg_dec_parse_header failed: %d", err);
        jpeg_dec_open(&cfg, &dec); // close
        free(jpeg_data);
        return NULL;
    }

    int w = info.width;
    int h = info.height;
    int outbuf_len;
    jpeg_dec_get_outbuf_len(dec, &outbuf_len);

    /* 分配 16 字节对齐的输出缓冲区 */
    uint8_t *rgb = jpeg_calloc_align(outbuf_len, 16);
    if (!rgb) {
        ESP_LOGE(TAG, "OOM for decoded buffer (%d bytes)", outbuf_len);
        free(jpeg_data);
        return NULL;
    }

    io.outbuf = rgb;
    err = jpeg_dec_process(dec, &io);
    if (err != JPEG_ERR_OK) {
        ESP_LOGE(TAG, "jpeg_dec_process failed: %d", err);
        free(rgb);
        free(jpeg_data);
        return NULL;
    }

    free(jpeg_data);

    /* ---- 3. 构造 LVGL 图像 ---- */
    if (s_cached_dsc) {
        /* 释放上一帧 */
        free((void*)s_cached_dsc->data);
        free(s_cached_dsc);
        s_cached_dsc = NULL;
    }

    lv_image_dsc_t *dsc = malloc(sizeof(lv_image_dsc_t));
    if (!dsc) {
        ESP_LOGE(TAG, "OOM for image descriptor");
        free(rgb);
        return NULL;
    }
    memset(dsc, 0, sizeof(*dsc));

    dsc->header.w = w;
    dsc->header.h = h;
    dsc->header.cf = LV_COLOR_FORMAT_RGB888;
    dsc->data = rgb;
    dsc->data_size = outbuf_len;

    s_cached_dsc = dsc;
    ESP_LOGI(TAG, "Decoded %s: %dx%d (%d bytes)", path, w, h, outbuf_len);
    return dsc;
}
