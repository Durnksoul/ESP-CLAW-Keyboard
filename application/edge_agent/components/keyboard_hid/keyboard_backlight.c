/* SPDX-License-Identifier: Apache-2.0 */
#include "keyboard_backlight.h"
#include "keyboard_board.h"
#include <stdbool.h>
#include <stdlib.h>
#include "led_strip.h"
#include "led_strip_rmt.h"
#include "esp_log.h"

#define BACKLIGHT_KEYS 9
#define BACKLIGHT_KEY_MASK ((1U << BACKLIGHT_KEYS) - 1)
#define BACKLIGHT_PERCENT 10
#define BACKLIGHT_GREEN ((255 * BACKLIGHT_PERCENT + 50) / 100)
#define BACKLIGHT_RMT_DMA_SYMBOLS 512
/* IDF splits DMA memory in two. Keep the complete 9x24-bit frame, reset and
 * terminator in one half so Wi-Fi cannot delay a mid-frame refill interrupt.
 */
_Static_assert(BACKLIGHT_RMT_DMA_SYMBOLS / 2 >= BACKLIGHT_KEYS * 24 + 2,
               "DMA half-buffer must hold the full backlight frame");

_Static_assert(KEYBOARD_BOARD_KEY_RGB_T0H_NS + KEYBOARD_BOARD_KEY_RGB_T0L_NS == 1250 &&
               KEYBOARD_BOARD_KEY_RGB_T1H_NS + KEYBOARD_BOARD_KEY_RGB_T1L_NS == 1250,
               "Both symbols must use the 800 kbit/s bit period");

static const char *TAG = "keyboard_backlight";

/* Latest schematic: U7,U8,U9,U4,U5,U6,U1,U2,U3, corresponding to key 1..9. */
static const uint8_t s_led_to_key[BACKLIGHT_KEYS] = KEYBOARD_BOARD_LED_TO_KEY;
struct keyboard_backlight {
    led_strip_handle_t strip;
    uint16_t previous;
    bool valid;
};

esp_err_t keyboard_backlight_poll(keyboard_backlight_handle_t h, uint16_t pressed)
{
    if (!h) { return ESP_OK; }
    pressed &= BACKLIGHT_KEY_MASK;
    if (h->valid && pressed == h->previous) { return ESP_OK; }
    uint16_t off_leds = 0;
    for (unsigned led = 0; led < BACKLIGHT_KEYS; ++led) {
        const uint8_t green = (pressed & (1U << s_led_to_key[led])) ? 0 : BACKLIGHT_GREEN;
        if (green == 0) { off_leds |= (uint16_t)(1U << led); }
        esp_err_t err = led_strip_set_pixel(h->strip, led, 0, green, 0);
        if (err != ESP_OK) { return err; }
    }
    esp_err_t err = led_strip_refresh(h->strip);
    if (err == ESP_OK) {
        h->previous = pressed;
        h->valid = true;
        /* Log changes only: distinguish extra scanned keys from bad display. */
        ESP_LOGI(TAG, "pressed=0x%03x off_leds=0x%03x (bit0=key1/LED1), R=0 B=0 G=%d",
                 (unsigned)pressed, (unsigned)off_leds, BACKLIGHT_GREEN);
    }
    return err;
}

esp_err_t keyboard_backlight_create(const keyboard_backlight_config_t *config,
                                    keyboard_backlight_handle_t *out)
{
    if (!config || !out) { return ESP_ERR_INVALID_ARG; }
    *out = NULL;
    keyboard_backlight_handle_t h = calloc(1, sizeof(*h));
    if (!h) { return ESP_ERR_NO_MEM; }
    const led_strip_config_t strip = {
        .strip_gpio_num = config->gpio_num,
        .max_leds = BACKLIGHT_KEYS,
        .led_model = LED_MODEL_CUSTOM,
        .timings = {
            .t0h_ns = KEYBOARD_BOARD_KEY_RGB_T0H_NS,
            .t0l_ns = KEYBOARD_BOARD_KEY_RGB_T0L_NS,
            .t1h_ns = KEYBOARD_BOARD_KEY_RGB_T1H_NS,
            .t1l_ns = KEYBOARD_BOARD_KEY_RGB_T1L_NS,
            .reset_us = KEYBOARD_BOARD_KEY_RGB_RESET_US,
        },
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
    };
    const led_strip_rmt_config_t rmt = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = KEYBOARD_BOARD_KEY_RGB_RESOLUTION_HZ,
        .mem_block_symbols = BACKLIGHT_RMT_DMA_SYMBOLS,
        .flags.with_dma = true,
    };
    esp_err_t err = led_strip_new_rmt_device(&strip, &rmt, &h->strip);
    if (err != ESP_OK) { free(h); return err; }
    err = keyboard_backlight_poll(h, 0);
    if (err != ESP_OK) { keyboard_backlight_delete(h); return err; }
    *out = h;
    ESP_LOGI(TAG, "GPIO%d: %d LEDs, green=%d/255, RMT DMA=%d symbols",
             config->gpio_num, BACKLIGHT_KEYS, BACKLIGHT_GREEN, BACKLIGHT_RMT_DMA_SYMBOLS);
    ESP_LOGI(TAG, "3528-800k: T0=%d/%dns T1=%d/%dns reset=%dus",
             KEYBOARD_BOARD_KEY_RGB_T0H_NS, KEYBOARD_BOARD_KEY_RGB_T0L_NS,
             KEYBOARD_BOARD_KEY_RGB_T1H_NS, KEYBOARD_BOARD_KEY_RGB_T1L_NS,
             KEYBOARD_BOARD_KEY_RGB_RESET_US);
    return ESP_OK;
}

void keyboard_backlight_delete(keyboard_backlight_handle_t h)
{
    if (!h) { return; }
    led_strip_clear(h->strip);
    led_strip_del(h->strip);
    free(h);
}
