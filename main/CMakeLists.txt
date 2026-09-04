idf_component_register(SRCS
                           main.cpp
                           display.cpp
                           status_ui.cpp
                           usb_kbd.cpp
                       INCLUDE_DIRS "."
                       REQUIRES driver esp_lcd esp_timer nvs_flash usb bt)
