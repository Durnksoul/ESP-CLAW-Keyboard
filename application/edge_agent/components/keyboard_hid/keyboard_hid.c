/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * 3x3 keyboard matrix and USB HID for the DIY ESP32-S3 keyboard.
 */
#include "keyboard_hid.h"
#include "keyboard_macro.h"
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
#define SCAN_PERIOD_MS            5
#define DEBOUNCE_SAMPLES          4
#define HID_REPORT_LENGTH         8
#define HID_CONFIG_TOTAL_LENGTH   (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN)

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

static const uint8_t s_hid_report_descriptor[] = {
    TUD_HID_REPORT_DESC_KEYBOARD(),
};
static const uint8_t s_usb_config_descriptor[] = {
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, HID_CONFIG_TOTAL_LENGTH, 0, 100),
    TUD_HID_DESCRIPTOR(0, 4, HID_ITF_PROTOCOL_KEYBOARD,
                       sizeof(s_hid_report_descriptor), 0x81, HID_REPORT_LENGTH, 10),
};
static const char s_language_id[] = {0x09, 0x04};
static const char *s_usb_strings[] = {
    s_language_id,
    "ESP-Claw",
    "ESP-Claw Keyboard",
    "000001",
    "Keyboard",
};
static uint8_t s_last_report[HID_REPORT_LENGTH];

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    (void)instance;
    return s_hid_report_descriptor;
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                               hid_report_type_t report_type, uint8_t *buffer,
                               uint16_t reqlen)
{
    (void)instance;
    (void)report_id;
    if (report_type != HID_REPORT_TYPE_INPUT) {
        return 0;
    }
    uint16_t length = reqlen < sizeof(s_last_report) ? reqlen : sizeof(s_last_report);
    memcpy(buffer, s_last_report, length);
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
    uint8_t debounce[KEYBOARD_KEYS] = {0};
    uint16_t stable = 0;
    keyboard_macro_report_t sent = {0};
    bool sent_valid = false;

    while (true) {
        const uint16_t raw = scan_matrix();
        for (unsigned i = 0; i < KEYBOARD_KEYS; ++i) {
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
        const bool mounted = tud_mounted();
        keyboard_macro_report_t report;
        const bool force = keyboard_macro_poll(s_macros, stable, now, mounted, &report);
        if (!mounted) {
            sent_valid = false;
        } else if (tud_hid_ready() && (force || !sent_valid || memcmp(&sent, &report, sizeof(report)))) {
            if (send_keyboard_report(&report)) {
                sent = report;
                sent_valid = true;
                keyboard_macro_sent(s_macros, now);
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
    tinyusb_config_t config = TINYUSB_DEFAULT_CONFIG();
    config.descriptor.full_speed_config = s_usb_config_descriptor;
    config.descriptor.string = s_usb_strings;
    config.descriptor.string_count = sizeof(s_usb_strings) / sizeof(s_usb_strings[0]);
    esp_err_t err = tinyusb_driver_install(&config);
    if (err != ESP_OK) {
        return err;
    }

    if (xTaskCreate(keyboard_task, "keyboard_scan", 3072, NULL, 6, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "USB keyboard ready: GPIO4/5/6 rows, GPIO11/10/9 columns");
    return ESP_OK;
}
