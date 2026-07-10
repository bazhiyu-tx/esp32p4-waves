idf_component_register(
    SRCS main.cpp
         wifi.c
         sd_card.c
         camera.c
         app_video.c
         core/event_bus.cpp
         services/display_service.cpp
         services/sdcard_service.cpp
         services/wifi_service.cpp
         services/camera_service.cpp
         services/yolo_service.cpp
         services/system/time_service.cpp
         luf/luf.c
         luf/core/luf_app.c
         luf/core/luf_app_mgr.c
         luf/core/luf_desktop.c
         luf/core/luf_gesture.c
         luf/core/luf_notify.c
         luf/core/luf_statusbar.c
         luf/port/luf_osal_freertos.c
         demo/demo.c
         demo/demo_apps.c
         demo/demo_pages.c
         demo/demo_flipclock.c
         demo/demo_wifi.c
    INCLUDE_DIRS . core services luf luf/core luf/port demo
    PRIV_REQUIRES sdmmc esp_driver_sdmmc esp_wifi nvs_flash spiffs fatfs
               esp_video esp-dl)

idf_component_get_property(LVGL_LIB lvgl__lvgl COMPONENT_LIB)
target_compile_options(
    ${LVGL_LIB} 
    PRIVATE 
        -DLV_LVGL_H_INCLUDE_SIMPLE
)

# Enable C++17 for the main component
target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
