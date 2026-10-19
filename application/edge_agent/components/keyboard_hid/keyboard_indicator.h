/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdint.h>
#include "esp_err.h"

typedef struct keyboard_indicator *keyboard_indicator_handle_t;
typedef struct {
    int gpio_num;
} keyboard_indicator_config_t;

/* Owned exclusively by the keyboard scan task after create. */
esp_err_t keyboard_indicator_create(const keyboard_indicator_config_t *config,
                                    keyboard_indicator_handle_t *out);
void keyboard_indicator_delete(keyboard_indicator_handle_t handle);
esp_err_t keyboard_indicator_poll(keyboard_indicator_handle_t handle,
                                  uint16_t pressed);
