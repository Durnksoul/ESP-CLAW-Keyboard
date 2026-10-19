/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

typedef struct keyboard_encoder *keyboard_encoder_handle_t;
typedef struct { int gpio_a; int gpio_b; int direction; } keyboard_encoder_config_t;
esp_err_t keyboard_encoder_create(const keyboard_encoder_config_t *config,
                                  keyboard_encoder_handle_t *out);
void keyboard_encoder_delete(keyboard_encoder_handle_t handle);
void keyboard_encoder_set_button(keyboard_encoder_handle_t handle, bool pressed);
uint16_t keyboard_encoder_peek(keyboard_encoder_handle_t handle);
void keyboard_encoder_pop(keyboard_encoder_handle_t handle);
void keyboard_encoder_clear(keyboard_encoder_handle_t handle);
