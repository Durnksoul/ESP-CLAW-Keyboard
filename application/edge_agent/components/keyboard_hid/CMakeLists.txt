idf_component_register(
    SRCS "keyboard_hid.c" "keyboard_macro.c" "keyboard_macro_ai.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_gpio esp_tinyusb esp_timer nvs_flash json claw_cap app_claw
)
