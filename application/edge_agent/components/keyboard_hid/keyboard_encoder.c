/* SPDX-License-Identifier: Apache-2.0 */
#include "keyboard_encoder.h"
#include "keyboard_encoder_decode.h"
#include <stdlib.h>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "class/hid/hid.h"

#define ENCODER_QUEUE_LENGTH 32
struct keyboard_encoder {
    int gpio_a, gpio_b, direction;
    portMUX_TYPE lock;
    keyboard_encoder_decoder_t decoder;
    uint16_t queue[ENCODER_QUEUE_LENGTH];
    unsigned head, count;
    bool button_pressed, isr_a, isr_b;
};

/* Call only while holding lock (task or ISR). Drop newest event if full. */
static void enqueue(keyboard_encoder_handle_t h, uint16_t usage)
{
    if (h->count < ENCODER_QUEUE_LENGTH) {
        h->queue[(h->head + h->count) % ENCODER_QUEUE_LENGTH] = usage;
        ++h->count;
    }
}

static void encoder_edge(void *arg)
{
    keyboard_encoder_handle_t h = arg;
    portENTER_CRITICAL_ISR(&h->lock);
    unsigned state = (gpio_get_level(h->gpio_a) << 1) | gpio_get_level(h->gpio_b);
    int motion = keyboard_encoder_decode(&h->decoder, state) * h->direction;
    if (motion) {
        enqueue(h, motion > 0 ? HID_USAGE_CONSUMER_VOLUME_INCREMENT : HID_USAGE_CONSUMER_VOLUME_DECREMENT);
    }
    portEXIT_CRITICAL_ISR(&h->lock);
}

esp_err_t keyboard_encoder_create(const keyboard_encoder_config_t *config,
                                  keyboard_encoder_handle_t *out)
{
    if (!config || !out || (config->direction != 1 && config->direction != -1)) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = NULL;
    keyboard_encoder_handle_t h = calloc(1, sizeof(*h));
    if (!h) { return ESP_ERR_NO_MEM; }
    h->gpio_a = config->gpio_a; h->gpio_b = config->gpio_b; h->direction = config->direction;
    h->lock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
    const gpio_config_t gpio = {
        .pin_bit_mask = (1ULL << h->gpio_a) | (1ULL << h->gpio_b),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&gpio);
    if (err != ESP_OK) { free(h); return err; }
    h->decoder.previous = (gpio_get_level(h->gpio_a) << 1) | gpio_get_level(h->gpio_b);
    err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) { free(h); return err; }
    err = gpio_isr_handler_add(h->gpio_a, encoder_edge, h);
    if (err == ESP_OK) { h->isr_a = true; err = gpio_isr_handler_add(h->gpio_b, encoder_edge, h); }
    if (err == ESP_OK) { h->isr_b = true; err = gpio_set_intr_type(h->gpio_a, GPIO_INTR_ANYEDGE); }
    if (err == ESP_OK) { err = gpio_set_intr_type(h->gpio_b, GPIO_INTR_ANYEDGE); }
    if (err != ESP_OK) { keyboard_encoder_delete(h); return err; }
    *out = h;
    return ESP_OK;
}

void keyboard_encoder_delete(keyboard_encoder_handle_t h)
{
    if (!h) { return; }
    gpio_intr_disable(h->gpio_a); gpio_intr_disable(h->gpio_b);
    if (h->isr_a) { gpio_isr_handler_remove(h->gpio_a); }
    if (h->isr_b) { gpio_isr_handler_remove(h->gpio_b); }
    /* The process-wide ISR service may belong to other drivers. */
    free(h);
}

void keyboard_encoder_set_button(keyboard_encoder_handle_t h, bool pressed)
{
    if (!h) { return; }
    portENTER_CRITICAL(&h->lock);
    if (pressed && !h->button_pressed) { enqueue(h, HID_USAGE_CONSUMER_MUTE); }
    h->button_pressed = pressed;
    portEXIT_CRITICAL(&h->lock);
}

uint16_t keyboard_encoder_peek(keyboard_encoder_handle_t h)
{
    if (!h) { return 0; }
    portENTER_CRITICAL(&h->lock);
    uint16_t usage = h->count ? h->queue[h->head] : 0;
    portEXIT_CRITICAL(&h->lock);
    return usage;
}

void keyboard_encoder_pop(keyboard_encoder_handle_t h)
{
    if (!h) { return; }
    portENTER_CRITICAL(&h->lock);
    if (h->count) { h->head = (h->head + 1) % ENCODER_QUEUE_LENGTH; --h->count; }
    portEXIT_CRITICAL(&h->lock);
}

void keyboard_encoder_clear(keyboard_encoder_handle_t h)
{
    if (!h) { return; }
    portENTER_CRITICAL(&h->lock);
    h->head = h->count = 0;
    h->decoder.quarter_steps = 0;
    portEXIT_CRITICAL(&h->lock);
}
