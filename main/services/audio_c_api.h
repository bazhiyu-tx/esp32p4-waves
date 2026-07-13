/**
 * @file audio_c_api.h
 * @brief C-compatible wrapper for Audio MDNS service (included from C files)
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start audio playback service.
 * Must be called after WiFi is connected.
 */
void audio_start(void);

/**
 * @brief Stop audio playback service.
 */
void audio_stop(void);

#ifdef __cplusplus
}
#endif
