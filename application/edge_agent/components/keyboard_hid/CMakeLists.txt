idf_component_register(
    SRCS "keyboard_hid.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_gpio esp_tinyusb
)
