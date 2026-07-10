#ifndef WIFI_H
#define WIFI_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_wifi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_MAX_AP 20

typedef void (*wifi_cb_t)(bool connected, void *arg);

void wifi_init(void);
void wifi_load_saved_config(void);
void wifi_set_sd_mounted(bool mounted);
bool wifi_is_connected(void);
void wifi_register_callback(wifi_cb_t cb, void *arg);

/* Scan */
int  wifi_scan(wifi_ap_record_t *aps, int max_count);
void wifi_connect(const char *ssid, const char *password);

#ifdef __cplusplus
}
#endif

#endif /* WIFI_H */
