/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdint.h>
#include "esp_err.h"

typedef struct keyboard_backlight *keyboard_backlight_handle_t;
typedef struct { int gpio_num; } keyboard_backlight_config_t;
esp_err_t keyboard_backlight_create(const keyboard_backlight_config_t *config,
                                    keyboard_backlight_handle_t *out);
void keyboard_backlight_delete(keyboard_backlight_handle_t handle);
esp_err_t keyboard_backlight_poll(keyboard_backlight_handle_t handle, uint16_t pressed);
