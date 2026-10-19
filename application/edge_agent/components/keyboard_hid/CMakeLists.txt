idf_component_register(
    SRCS "keyboard_hid.c" "keyboard_macro.c" "keyboard_macro_ai.c" "keyboard_indicator.c" "keyboard_backlight.c" "keyboard_encoder.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES led_strip gen_bmgr_codes
    REQUIRES esp_driver_gpio esp_tinyusb esp_timer nvs_flash json claw_cap app_claw
)
