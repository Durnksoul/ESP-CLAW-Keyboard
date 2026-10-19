/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * 3x3 keyboard matrix and USB HID for the DIY ESP32-S3 keyboard.
 */
#include "keyboard_hid.h"
#include "keyboard_macro.h"
#include "keyboard_indicator.h"
#include "keyboard_backlight.h"
#include "keyboard_encoder.h"
#include "keyboard_board.h"
#include "esp_timer.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "class/hid/hid_device.h"

#define KEYBOARD_ROWS             3
#define KEYBOARD_COLS             3
#define KEYBOARD_KEYS             (KEYBOARD_ROWS * KEYBOARD_COLS)
#define KEYBOARD_INPUTS           (KEYBOARD_KEYS + 1)
#define ENCODER_BUTTON_MASK       (1U << KEYBOARD_KEYS)
#define MATRIX_KEY_MASK           ((1U << KEYBOARD_KEYS) - 1)
#define CONSUMER_HID_INSTANCE     1
#define CONSUMER_REPORT_LENGTH    2
#define SCAN_PERIOD_MS            5
#define DEBOUNCE_SAMPLES          4
#define HID_REPORT_LENGTH         8
#define HID_CONFIG_TOTAL_LENGTH   (TUD_CONFIG_DESC_LEN + 2 * TUD_HID_DESC_LEN)

_Static_assert(KEYBOARD_KEYS == KEYBOARD_MACRO_KEYS, "matrix and macro key counts must match");

static const char *TAG = "keyboard_hid";

/* Schematic: ROW1..3 = GPIO4..6; COL1..3 = GPIO11, GPIO10, GPIO9.
 * Switch diodes have their cathodes on the row side. Columns use
 * pull-ups; one row at a time is driven low, so a pressed key reads low.
 */static const gpio_num_t s_rows[KEYBOARD_ROWS] = {
    GPIO_NUM_4, GPIO_NUM_5, GPIO_NUM_6,
};
static const gpio_num_t s_cols[KEYBOARD_COLS] = {
    GPIO_NUM_11, GPIO_NUM_10, GPIO_NUM_9,
};

static keyboard_macro_handle_t s_macros;
static keyboard_indicator_handle_t s_indicator;
static keyboard_backlight_handle_t s_backlight;
static keyboard_encoder_handle_t s_encoder;
static uint16_t s_last_consumer_report;

static const uint8_t s_hid_report_descriptor[] = {
    TUD_HID_REPORT_DESC_KEYBOARD(),
};
static const uint8_t s_consumer_report_descriptor[] = {
    TUD_HID_REPORT_DESC_CONSUMER(),
};
static const uint8_t s_usb_config_descriptor[] = {
    TUD_CONFIG_DESCRIPTOR(1, 2, 0, HID_CONFIG_TOTAL_LENGTH, 0, 100),
    TUD_HID_DESCRIPTOR(0, 4, HID_ITF_PROTOCOL_KEYBOARD,
                       sizeof(s_hid_report_descriptor), 0x81, HID_REPORT_LENGTH, 10),
    TUD_HID_DESCRIPTOR(1, 5, HID_ITF_PROTOCOL_NONE,
                       sizeof(s_consumer_report_descriptor), 0x82, CONSUMER_REPORT_LENGTH, 5),
};
static const char s_language_id[] = {0x09, 0x04};
static const char *s_usb_strings[] = {
    s_language_id,
    "ESP-Claw",
    "ESP-Claw Keyboard",
    "000001",
    "Keyboard",
    "Volume Control",
};
static uint8_t s_last_report[HID_REPORT_LENGTH];

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    return instance == CONSUMER_HID_INSTANCE ? s_consumer_report_descriptor : s_hid_report_descriptor;
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                               hid_report_type_t report_type, uint8_t *buffer,
                               uint16_t reqlen)
{
    (void)report_id;
    if (report_type != HID_REPORT_TYPE_INPUT) { return 0; }
    const void *report = instance == CONSUMER_HID_INSTANCE ?
                         (const void *)&s_last_consumer_report : (const void *)s_last_report;
    const uint16_t size = instance == CONSUMER_HID_INSTANCE ?
                          sizeof(s_last_consumer_report) : sizeof(s_last_report);
    uint16_t length = reqlen < size ? reqlen : size;
    memcpy(buffer, report, length);
    return length;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                           hid_report_type_t report_type,
                           uint8_t const *buffer, uint16_t bufsize)
{
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)bufsize;
}

static uint16_t scan_matrix(void)
{
    uint16_t pressed = 0;
    for (unsigned row = 0; row < KEYBOARD_ROWS; ++row) {
        gpio_set_level(s_rows[row], 0);
        esp_rom_delay_us(20);
        for (unsigned col = 0; col < KEYBOARD_COLS; ++col) {
            if (gpio_get_level(s_cols[col]) == 0) {
                const unsigned key_id = row * KEYBOARD_COLS + col + 1;
                pressed |= (uint16_t)1U << (key_id - 1);
            }
        }
        if (row == 0 && gpio_get_level(KEYBOARD_BOARD_ENCODER_BUTTON_COL_GPIO) == 0) {
            pressed |= ENCODER_BUTTON_MASK;
        }
        gpio_set_level(s_rows[row], 1);
    }
    return pressed;
}

static bool send_keyboard_report(const keyboard_macro_report_t *report)
{
    if (tud_hid_keyboard_report(0, report->modifiers, report->keys)) {
        s_last_report[0] = report->modifiers;
        s_last_report[1] = 0;
        memcpy(&s_last_report[2], report->keys, sizeof(report->keys));
        return true;
    }
    return false;
}

static void keyboard_task(void *arg)
{
    (void)arg;
    uint8_t debounce[KEYBOARD_INPUTS] = {0};
    uint16_t stable = 0;
    keyboard_macro_report_t sent = {0};
    bool sent_valid = false;
    bool consumer_release_pending = false;

    while (true) {
        const uint16_t raw = scan_matrix();
        for (unsigned i = 0; i < KEYBOARD_INPUTS; ++i) {
            const uint16_t bit = (uint16_t)1U << i;
            if (raw & bit) {
                if (debounce[i] < DEBOUNCE_SAMPLES) {
                    ++debounce[i];
                }
                if (debounce[i] == DEBOUNCE_SAMPLES) {
                    stable |= bit;
                }
            } else {
                if (debounce[i] > 0) {
                    --debounce[i];
                }
                if (debounce[i] == 0) {
                    stable &= (uint16_t)~bit;
                }
            }
        }

        const uint32_t now = esp_timer_get_time() / 1000;
        /* Observe debounced physical key state independently of USB and macros. */
        esp_err_t indicator_err = keyboard_indicator_poll(s_indicator, stable);
        if (indicator_err != ESP_OK) {
            ESP_LOGW(TAG, "Key indicator update failed: %s", esp_err_to_name(indicator_err));
            keyboard_indicator_delete(s_indicator);
            s_indicator = NULL;
        }
        esp_err_t backlight_err = keyboard_backlight_poll(s_backlight, stable);
        if (backlight_err != ESP_OK) {
            ESP_LOGW(TAG, "Key backlight update failed: %s", esp_err_to_name(backlight_err));
            keyboard_backlight_delete(s_backlight);
            s_backlight = NULL;
        }
        keyboard_encoder_set_button(s_encoder, (stable & ENCODER_BUTTON_MASK) != 0);
        const bool mounted = tud_mounted();
        keyboard_macro_report_t report;
        const bool force = keyboard_macro_poll(s_macros, stable & MATRIX_KEY_MASK, now, mounted, &report);
        if (!mounted) {
            sent_valid = false;
            keyboard_encoder_clear(s_encoder);
            consumer_release_pending = false;
            s_last_consumer_report = 0;
        } else if (tud_hid_ready() && (force || !sent_valid || memcmp(&sent, &report, sizeof(report)))) {
            if (send_keyboard_report(&report)) {
                sent = report;
                sent_valid = true;
                keyboard_macro_sent(s_macros, now);
            }
        }
        if (mounted && tud_hid_n_ready(CONSUMER_HID_INSTANCE)) {
            uint16_t usage = consumer_release_pending ? 0 : keyboard_encoder_peek(s_encoder);
            if ((consumer_release_pending || usage) &&
                tud_hid_n_report(CONSUMER_HID_INSTANCE, 0, &usage, sizeof(usage))) {
                s_last_consumer_report = usage;
                if (consumer_release_pending) {
                    consumer_release_pending = false;
                } else {
                    keyboard_encoder_pop(s_encoder);
                    consumer_release_pending = true;
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(SCAN_PERIOD_MS));
    }
}

esp_err_t keyboard_hid_start(void)
{
    esp_err_t init_err = keyboard_macro_create(&s_macros);
    if (init_err != ESP_OK) { return init_err; }
    init_err = keyboard_macro_register_ai(s_macros);
    if (init_err != ESP_OK) { keyboard_macro_delete(s_macros); s_macros = NULL; return init_err; }
    for (unsigned i = 0; i < KEYBOARD_ROWS; ++i) {
        const gpio_config_t config = {
            .pin_bit_mask = 1ULL << s_rows[i],
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        esp_err_t err = gpio_config(&config);
        if (err != ESP_OK) {
            return err;
        }
        gpio_set_level(s_rows[i], 1);
    }

    for (unsigned i = 0; i < KEYBOARD_COLS; ++i) {
        const gpio_config_t config = {
            .pin_bit_mask = 1ULL << s_cols[i],
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        esp_err_t err = gpio_config(&config);
        if (err != ESP_OK) {
            return err;
        }
    }
    const gpio_config_t button_gpio = {
        .pin_bit_mask = 1ULL << KEYBOARD_BOARD_ENCODER_BUTTON_COL_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t button_err = gpio_config(&button_gpio);
    if (button_err != ESP_OK) { return button_err; }
    tinyusb_config_t config = TINYUSB_DEFAULT_CONFIG();
    config.descriptor.full_speed_config = s_usb_config_descriptor;
    config.descriptor.string = s_usb_strings;
    config.descriptor.string_count = sizeof(s_usb_strings) / sizeof(s_usb_strings[0]);
    esp_err_t err = tinyusb_driver_install(&config);
    if (err != ESP_OK) {
        return err;
    }

    const keyboard_indicator_config_t indicator_config = {
        .gpio_num = KEYBOARD_BOARD_RGB_GPIO,
    };
    esp_err_t indicator_err = keyboard_indicator_create(&indicator_config, &s_indicator);
    if (indicator_err != ESP_OK) {
        ESP_LOGW(TAG, "Key indicator unavailable: %s", esp_err_to_name(indicator_err));
    }
    const keyboard_backlight_config_t backlight_config = {
        .gpio_num = KEYBOARD_BOARD_KEY_RGB_GPIO,
    };
    esp_err_t backlight_err = keyboard_backlight_create(&backlight_config, &s_backlight);
    if (backlight_err != ESP_OK) {
        ESP_LOGW(TAG, "Key backlight unavailable: %s", esp_err_to_name(backlight_err));
    }
    const keyboard_encoder_config_t encoder_config = {
        .gpio_a = KEYBOARD_BOARD_ENCODER_A_GPIO,
        .gpio_b = KEYBOARD_BOARD_ENCODER_B_GPIO,
        .direction = KEYBOARD_BOARD_ENCODER_DIRECTION,
    };
    esp_err_t encoder_err = keyboard_encoder_create(&encoder_config, &s_encoder);
    if (encoder_err != ESP_OK) {
        ESP_LOGW(TAG, "Volume encoder unavailable: %s", esp_err_to_name(encoder_err));
    }
    if (xTaskCreate(keyboard_task, "keyboard_scan", 3072, NULL, 6, NULL) != pdPASS) {
        keyboard_indicator_delete(s_indicator);
        s_indicator = NULL;
        keyboard_backlight_delete(s_backlight);
        s_backlight = NULL;
        keyboard_encoder_delete(s_encoder);
        s_encoder = NULL;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "USB keyboard ready: GPIO4/5/6 rows, GPIO11/10/9 columns");
    return ESP_OK;
}
