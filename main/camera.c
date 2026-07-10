/**
 * @file camera.c
 * @brief OV5647 via app_video API + ISP brightness control
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_cache.h"
#include "bsp/esp32_p4_wifi6_touch_lcd_7b.h"
#include "esp_video_device.h"
#include "linux/videodev2.h"
#include "app_video.h"
#include "camera.h"

static const char *TAG = "camera";
#define W 800
#define H 800
#define FB_SZ (W * H * 3)  /* RGB888 */
#define RGB_SZ (W * H * 2) /* RGB565 */
#define BUF_NUM 2

static int s_fd = -1;
static uint8_t *s_bufs[BUF_NUM] = { NULL };
static uint16_t *s_rgb[2] = { NULL, NULL };  /* double buffer for tear-free display */
static volatile int s_rgb_idx = 0;             /* index LVGL is currently reading */
static volatile int s_raw_idx = 0;             /* which s_bufs[] was used for current display frame */
static volatile uint8_t *s_fb = NULL;
static bool s_stream = false;

/* RGB888 buffer for YOLO inference (BGR→RGB swapped, converted on demand) */
static uint8_t *s_rgb888 = NULL;
static volatile int s_rgb888_ready = 0;        /* frame counter when RGB888 was last updated */

/* BGR888 → RGB565, rows reversed (sensor upside-down) */
static void convert_bgr888_to_rgb565(const uint8_t *src, uint16_t *dst, int w, int h)
{
    for (int y = 0; y < h; y++) {
        const uint8_t *row = src + (h - 1 - y) * w * 3;
        for (int x = 0; x < w; x++) {
            uint8_t b = row[x * 3];
            uint8_t g = row[x * 3 + 1];
            uint8_t r = row[x * 3 + 2];
            dst[y * w + x] = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
        }
    }
}

/* Frame callback — runs in stream task */
static void frame_cb(uint8_t *buf, uint8_t idx, uint32_t w, uint32_t h, size_t len)
{
    static int dump = 0;
    static int frame_num = 0;
    /* Write to the buffer NOT currently being read by LVGL */
    int write_idx = 1 - s_rgb_idx;
    convert_bgr888_to_rgb565(buf, s_rgb[write_idx], (int)w, (int)h);
    esp_cache_msync(s_rgb[write_idx], w * h * 2, ESP_CACHE_MSYNC_FLAG_DIR_C2M);

    /* Atomic swap: writer done, switch LVGL to this buffer */
    s_rgb_idx = write_idx;
    s_raw_idx = idx;   /* remember which raw buffer was used */
    s_fb = (uint8_t *)s_rgb[write_idx];
    frame_num++;
    if (dump < 5) {
        ESP_LOGI(TAG, "fr[%d] RGB565: %04x %04x %04x %04x",
                 dump, s_rgb[write_idx][0], s_rgb[write_idx][1],
                 s_rgb[write_idx][2], s_rgb[write_idx][3]);
        dump++;
    }
}

esp_err_t camera_init(void)
{
    bsp_i2c_init();
    i2c_master_bus_handle_t i2c = bsp_i2c_get_handle();

    esp_err_t e = app_video_main(i2c);
    if (e) { ESP_LOGE(TAG, "app_video_main fail %x", e); return e; }

    s_fd = app_video_open((char *)ESP_VIDEO_MIPI_CSI_DEVICE_NAME, APP_VIDEO_FMT_RGB888);
    if (s_fd < 0) { ESP_LOGE(TAG, "app_video_open fail"); return ESP_FAIL; }

    /* Allocate PSRAM buffers (128-byte aligned) */
    for (int i = 0; i < BUF_NUM; i++) {
        s_bufs[i] = (uint8_t *)heap_caps_aligned_alloc(128, FB_SZ, MALLOC_CAP_SPIRAM);
        if (!s_bufs[i]) { ESP_LOGE(TAG, "alloc buf%d", i); return ESP_FAIL; }
    }
    /* Double RGB565 output buffers (tear-free display) */
    for (int i = 0; i < 2; i++) {
        s_rgb[i] = (uint16_t *)heap_caps_aligned_alloc(128, RGB_SZ, MALLOC_CAP_SPIRAM);
        if (!s_rgb[i]) { ESP_LOGE(TAG, "alloc rgb%d", i); return ESP_FAIL; }
    }
    /* RGB888 buffer for YOLO (BGR→RGB swapped) */
    s_rgb888 = (uint8_t *)heap_caps_aligned_alloc(128, FB_SZ, MALLOC_CAP_SPIRAM);
    if (!s_rgb888) { ESP_LOGE(TAG, "alloc rgb888"); return ESP_FAIL; }

    e = app_video_set_bufs(s_fd, BUF_NUM, (const void **)s_bufs);
    if (e) { ESP_LOGE(TAG, "set_bufs fail %x", e); return e; }

    app_video_register_frame_operation_cb(frame_cb);

    /* Moderate ISP tuning */
    int isp_fd = open(ESP_VIDEO_ISP1_DEVICE_NAME, O_RDWR);
    if (isp_fd >= 0) {
        struct v4l2_ext_controls ctrls = { .count = 0 };
        struct v4l2_ext_control ctrl[3];
        ctrls.controls = ctrl;

        ctrls.count = 1;
        ctrl[0].id = V4L2_CID_BRIGHTNESS;
        ctrl[0].value = 32;
        ioctl(isp_fd, VIDIOC_S_EXT_CTRLS, &ctrls);

        ctrls.count = 1;
        ctrl[0].id = V4L2_CID_CONTRAST;
        ctrl[0].value = 128;
        ioctl(isp_fd, VIDIOC_S_EXT_CTRLS, &ctrls);

        ctrls.count = 1;
        ctrl[0].id = V4L2_CID_GAIN;
        ctrl[0].value = 128;
        ioctl(isp_fd, VIDIOC_S_EXT_CTRLS, &ctrls);

        close(isp_fd);
        ESP_LOGI(TAG, "ISP tuning applied");
    }

    ESP_LOGI(TAG, "init OK %dx%d RGB888", W, H);
    return ESP_OK;
}

esp_err_t camera_start_stream(void)
{
    if (s_fd < 0) return ESP_ERR_INVALID_STATE;
    /* Re-queue buffers for fresh start (needed after stop/restart) */
    app_video_set_bufs(s_fd, BUF_NUM, (const void **)s_bufs);
    esp_err_t e = app_video_stream_task_start(s_fd, 0);
    if (e) return ESP_FAIL;
    s_stream = true;
    ESP_LOGI(TAG, "stream task started");
    return ESP_OK;
}

void camera_stop_stream(void)
{
    if (s_fd >= 0 && s_stream) {
        app_video_stream_task_stop(s_fd);
        app_video_stream_wait_stop();
        s_stream = false;
    }
}

const camera_frame_t *camera_get_frame(void)
{
    static camera_frame_t f = { .width = W, .height = H, .size = RGB_SZ };
    uint8_t *fb = (uint8_t *)s_rgb[s_rgb_idx];
    if (fb) {
        esp_cache_msync(fb, RGB_SZ, ESP_CACHE_MSYNC_FLAG_DIR_M2C | ESP_CACHE_MSYNC_FLAG_INVALIDATE);
    }
    f.fb = fb;
    return &f;
}

const uint8_t *camera_get_frame_rgb888(void)
{
    if (!s_rgb888) return NULL;

    /* Convert the matching raw BGR888 buffer to RGB888 on demand,
       with Y-flip to match display orientation (sensor is upside-down). */
    int raw_idx = s_raw_idx;
    const uint8_t *raw = s_bufs[raw_idx];
    if (raw) {
        esp_cache_msync((void *)raw, FB_SZ, ESP_CACHE_MSYNC_FLAG_DIR_M2C | ESP_CACHE_MSYNC_FLAG_INVALIDATE);
        for (int y = 0; y < H; y++) {
            int src_row = (H - 1 - y) * W * 3;  /* Y-flip: read from bottom up */
            for (int x = 0; x < W; x++) {
                int si = src_row + x * 3;
                int di = (y * W + x) * 3;
                s_rgb888[di]     = raw[si + 2];  /* R */
                s_rgb888[di + 1] = raw[si + 1];  /* G */
                s_rgb888[di + 2] = raw[si];      /* B */
            }
        }
        esp_cache_msync(s_rgb888, FB_SZ, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    }
    return s_rgb888;
}

void camera_deinit(void)
{
    camera_stop_stream();
    if (s_fd >= 0) { close(s_fd); s_fd = -1; }
    for (int i = 0; i < BUF_NUM; i++) {
        if (s_bufs[i]) { heap_caps_free(s_bufs[i]); s_bufs[i] = NULL; }
    }
    for (int i = 0; i < 2; i++) {
        if (s_rgb[i]) { heap_caps_free(s_rgb[i]); s_rgb[i] = NULL; }
    }
    if (s_rgb888) { heap_caps_free(s_rgb888); s_rgb888 = NULL; }
    s_fb = NULL;
}