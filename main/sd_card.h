/**
 * @file sd_card.h
 * @brief Custom SD card init (bypass bsp_sdcard_mount to avoid double sdmmc_host_init)
 */

#pragma once

#include "esp_err.h"
#include "sdmmc_cmd.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Mount SD card on slot 0, using the already-initialized SDMMC controller
 *        (which was claimed by esp_hosted for C6 SDIO on slot 1).
 *
 * Must be called AFTER wifi_init().
 *
 * @return ESP_OK on success
 */
esp_err_t sd_card_mount(void);

/**
 * @brief Get global SD card pointer (for unmount etc.)
 */
sdmmc_card_t *sd_card_get_card(void);

#ifdef __cplusplus
}
#endif
