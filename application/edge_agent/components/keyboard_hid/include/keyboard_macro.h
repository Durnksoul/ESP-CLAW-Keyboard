/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#define KEYBOARD_MACRO_KEYS 9
#define KEYBOARD_MACRO_STEPS 16
#define KEYBOARD_MACRO_SLOTS 6
#define KEYBOARD_MACRO_MAX_DELAY_MS 10000

typedef struct keyboard_macro *keyboard_macro_handle_t;
typedef struct {
    uint8_t modifiers;
    uint8_t keys[KEYBOARD_MACRO_SLOTS];
} keyboard_macro_report_t;
typedef struct {
    keyboard_macro_report_t report;
    uint32_t delay_ms; /* Wait after releasing this step. */
} keyboard_macro_step_t;
typedef struct {
    uint8_t version;
    uint8_t is_sequence;
    uint8_t count;
    keyboard_macro_step_t steps[KEYBOARD_MACRO_STEPS];
} keyboard_macro_config_t;

esp_err_t keyboard_macro_create(keyboard_macro_handle_t *out);
void keyboard_macro_delete(keyboard_macro_handle_t handle);
esp_err_t keyboard_macro_set(keyboard_macro_handle_t handle, unsigned key_id,
                             const keyboard_macro_config_t *config);
esp_err_t keyboard_macro_get(keyboard_macro_handle_t handle, unsigned key_id,
                             keyboard_macro_config_t *out);
esp_err_t keyboard_macro_reset(keyboard_macro_handle_t handle, unsigned key_id);
/* poll/sent are owned exclusively by the keyboard task. Config APIs are locked.
 * poll returns true when a report must be acknowledged even if unchanged. */
bool keyboard_macro_poll(keyboard_macro_handle_t handle, uint16_t pressed,
                         uint32_t now_ms, bool connected, keyboard_macro_report_t *out);
void keyboard_macro_sent(keyboard_macro_handle_t handle, uint32_t now_ms);
esp_err_t keyboard_macro_register_ai(keyboard_macro_handle_t handle);
