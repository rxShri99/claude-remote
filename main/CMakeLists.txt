idf_component_register(SRCS
                           main.cpp
                           display.cpp
                           status_ui.cpp
                           touch_spd2010.cpp
                           ble_hid.cpp
                           ble_audio.cpp
                           mic_stream.cpp
                       INCLUDE_DIRS "."
                       REQUIRES driver esp_lcd esp_timer nvs_flash bt esp_hid)
