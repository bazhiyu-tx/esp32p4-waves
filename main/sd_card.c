/**
 * @file sd_card.c
 * @brief Custom SD card init — bypass bsp_sdcard_mount to avoid re-calling sdmmc_host_init
 *
 * The SDMMC controller (only 1 on ESP32-P4) is claimed by esp_hosted for the C6 SDIO
 * on slot 1. This module adds slot 0 for the SD card on the same controller,
 * without calling sdmmc_host_init() again.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/sdmmc_host.h"
#include "driver/sdmmc_default_configs.h"
#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"
#include "diskio_sdmmc.h"
#include "diskio_impl.h"
#include "ff.h"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#include "bsp/esp32_p4_wifi6_touch_lcd_7b.h"

static const char *TAG = "sd_card";
static sdmmc_card_t *s_card = NULL;

/* ------------------------------------------------------------------ */
/*  Public                                                             */
/* ------------------------------------------------------------------ */

esp_err_t sd_card_mount(void)
{
    esp_err_t ret;

    /* ---- 1. Power on SD card (GPIO45 LOW = P-MOSFET conducts) ---- */
    gpio_set_direction(GPIO_NUM_45, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_45, 0);
    vTaskDelay(pdMS_TO_TICKS(20));

    /* ---- 2. LDO power control ---- */
    sd_pwr_ctrl_ldo_config_t ldo_config = {
        .ldo_chan_id = 4,
    };
    sd_pwr_ctrl_handle_t pwr_ctrl_handle = NULL;
    ret = sd_pwr_ctrl_new_on_chip_ldo(&ldo_config, &pwr_ctrl_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create LDO power control: 0x%x", ret);
        return ret;
    }

    /* ---- 3. Add SD card slot 0 to the existing controller ---- */
    /* NOTE: sdmmc_host_init() was already called by esp_hosted for C6 SDIO.
     *       We skip it and only add our slot. */
    sdmmc_slot_config_t slot_config = {
        .cd  = SDMMC_SLOT_NO_CD,
        .wp  = SDMMC_SLOT_NO_WP,
        .width = 4,
        .flags = 0,
    };

    ret = sdmmc_host_init_slot(SDMMC_HOST_SLOT_0, &slot_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "sdmmc_host_init_slot(0) failed: 0x%x", ret);
        return ret;
    }
    ESP_LOGI(TAG, "SDMMC slot 0 initialized");

    /* ---- 4. Configure host parameters for SD card ---- */
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot          = SDMMC_HOST_SLOT_0;
    host.max_freq_khz  = SDMMC_FREQ_HIGHSPEED;   /* 20 MHz */
    host.pwr_ctrl_handle = pwr_ctrl_handle;

    /* ---- 5. Probe and initialize the SD card ---- */
    sdmmc_card_t *card = (sdmmc_card_t *)malloc(sizeof(sdmmc_card_t));
    if (!card) {
        ESP_LOGE(TAG, "Failed to allocate sdmmc_card_t");
        return ESP_ERR_NO_MEM;
    }
    memset(card, 0, sizeof(*card));

    ret = sdmmc_card_init(&host, card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "sdmmc_card_init failed: 0x%x", ret);
        free(card);
        return ret;
    }

    sdmmc_card_print_info(stdout, card);

    /* ---- 6. Mount FATFS using public APIs (no internal headers) ---- */

    /* 6a. Get a free drive number */
    BYTE pdrv = FF_DRV_NOT_USED;
    ret = ff_diskio_get_drive(&pdrv);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ff_diskio_get_drive failed: 0x%x", ret);
        free(card);
        return ret;
    }

    /* 6b. Register SDMMC disk I/O driver for this drive */
    ff_diskio_register_sdmmc(pdrv, card);

    /* 6c. Register VFS */
    char drv[3] = { (char)('0' + pdrv), ':', 0 };
    esp_vfs_fat_conf_t vfs_conf = {
        .base_path = BSP_SD_MOUNT_POINT,
        .fat_drive = drv,
        .max_files = 5,
    };
    FATFS *fs;
    ret = esp_vfs_fat_register(&vfs_conf, &fs);
    if (ret == ESP_ERR_INVALID_STATE) {
        /* Already registered, that's OK */
        ESP_LOGW(TAG, "VFS already registered");
    } else if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_vfs_fat_register failed: 0x%x", ret);
        ff_diskio_unregister(pdrv);
        free(card);
        return ret;
    }

    /* 6d. Mount the filesystem */
    FRESULT fr = f_mount(fs, drv, 1);
    if (fr != FR_OK) {
        ESP_LOGE(TAG, "f_mount failed: %d", fr);
        esp_vfs_fat_unregister_path(BSP_SD_MOUNT_POINT);
        ff_diskio_unregister(pdrv);
        free(card);
        return ESP_FAIL;
    }

    /* ---- 7. Save global handle ---- */
    s_card = card;
    ESP_LOGI(TAG, "SD card mounted at %s", BSP_SD_MOUNT_POINT);
    return ESP_OK;
}

sdmmc_card_t *sd_card_get_card(void)
{
    return s_card;
}
