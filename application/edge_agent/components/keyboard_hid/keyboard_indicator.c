/* SPDX-License-Identifier: Apache-2.0 */
#include "keyboard_indicator.h"

#include <stdbool.h>
#include <stdlib.h>
#include "led_strip.h"
#include "led_strip_rmt.h"

#define INDICATOR_BRIGHTNESS_PERCENT 20
#define INDICATOR_GREEN (255 * INDICATOR_BRIGHTNESS_PERCENT / 100)
#define INDICATOR_RMT_RESOLUTION_HZ 10000000

struct keyboard_indicator {
    led_strip_handle_t strip;
    bool lit;
};

esp_err_t keyboard_indicator_create(const keyboard_indicator_config_t *config,
                                    keyboard_indicator_handle_t *out)
{
    if (!config || !out) { return ESP_ERR_INVALID_ARG; }
    *out = NULL;
    keyboard_indicator_handle_t h = calloc(1, sizeof(*h));
    if (!h) { return ESP_ERR_NO_MEM; }
    const led_strip_config_t strip_config = {
        .strip_gpio_num = config->gpio_num,
        .max_leds = 1,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
    };
    const led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = INDICATOR_RMT_RESOLUTION_HZ,
    };
    esp_err_t err = led_strip_new_rmt_device(&strip_config, &rmt_config, &h->strip);
    if (err != ESP_OK) { free(h); return err; }
    err = led_strip_clear(h->strip);
    if (err != ESP_OK) { keyboard_indicator_delete(h); return err; }
    *out = h;
    return ESP_OK;
}

void keyboard_indicator_delete(keyboard_indicator_handle_t h)
{
    if (!h) { return; }
    led_strip_del(h->strip);
    free(h);
}

esp_err_t keyboard_indicator_poll(keyboard_indicator_handle_t h, uint16_t pressed)
{
    if (!h) { return ESP_OK; } /* Observation must not disable keyboard input. */
    const bool should_light = pressed != 0;
    if (h->lit == should_light) { return ESP_OK; }
    esp_err_t err;
    if (should_light) {
        err = led_strip_set_pixel(h->strip, 0, 0, INDICATOR_GREEN, 0);
        if (err == ESP_OK) { err = led_strip_refresh(h->strip); }
    } else {
        err = led_strip_clear(h->strip);
    }
    if (err == ESP_OK) { h->lit = should_light; }
    return err;
}
