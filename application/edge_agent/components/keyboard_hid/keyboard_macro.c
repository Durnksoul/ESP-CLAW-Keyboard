/* SPDX-License-Identifier: Apache-2.0 */
#include "keyboard_macro.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "class/hid/hid.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"

#define MACRO_VERSION 1
#define TAP_MS 30
#define START_GAP_MS 10
typedef enum { IDLE, PREPARE, START_GAP, PRESS, HOLD, RELEASE, WAIT } phase_t;
struct keyboard_macro {
    SemaphoreHandle_t lock;
    nvs_handle_t nvs;
    keyboard_macro_config_t keys[KEYBOARD_MACRO_KEYS + 1];
    /* The following fields are owned by the scan task, not config writers. */
    keyboard_macro_config_t playing;
    uint16_t previous;
    uint8_t queue[KEYBOARD_MACRO_KEYS];
    unsigned head, queued, step;
    phase_t phase;
    uint32_t due;
};
static const char *TAG = "keyboard_macro";

static void set_default(unsigned id, keyboard_macro_config_t *out)
{
    memset(out, 0, sizeof(*out));
    out->version = MACRO_VERSION;
    out->count = 1;
    out->steps[0].report.keys[0] = HID_KEY_1 + id - 1;
}

static bool valid(const keyboard_macro_config_t *config)
{
    if (config->version != MACRO_VERSION || config->is_sequence > 1 ||
        config->count == 0 || config->count > KEYBOARD_MACRO_STEPS ||
        (!config->is_sequence && config->count != 1)) {
        return false;
    }
    for (unsigned i = 0; i < config->count; ++i) {
        const keyboard_macro_step_t *step = &config->steps[i];
        bool nonempty = step->report.modifiers != 0;
        if ((step->report.modifiers & ~0x0f) || step->delay_ms > KEYBOARD_MACRO_MAX_DELAY_MS ||
            (!config->is_sequence && step->delay_ms != 0)) {
            return false;
        }
        for (unsigned j = 0; j < KEYBOARD_MACRO_SLOTS; ++j) {
            uint8_t key = step->report.keys[j];
            if (key && (key < HID_KEY_A || key > HID_KEY_ARROW_UP)) {
                return false;
            }
            nonempty |= key != 0;
            for (unsigned k = 0; key && k < j; ++k) {
                if (step->report.keys[k] == key) { return false; }
            }
        }
        if (!nonempty) { return false; }
    }
    return true;
}

esp_err_t keyboard_macro_create(keyboard_macro_handle_t *out)
{
    if (!out) { return ESP_ERR_INVALID_ARG; }
    *out = NULL;
    keyboard_macro_handle_t h = calloc(1, sizeof(*h));
    if (!h) { return ESP_ERR_NO_MEM; }
    h->lock = xSemaphoreCreateMutex();
    if (!h->lock) { free(h); return ESP_ERR_NO_MEM; }
    esp_err_t err = nvs_open("keyboard", NVS_READWRITE, &h->nvs);
    if (err != ESP_OK) { vSemaphoreDelete(h->lock); free(h); return err; }
    for (unsigned id = 1; id <= KEYBOARD_MACRO_KEYS; ++id) {
        char name[8];
        snprintf(name, sizeof(name), "key%u", id);
        set_default(id, &h->keys[id]);
        size_t size = sizeof(h->keys[id]);
        err = nvs_get_blob(h->nvs, name, &h->keys[id], &size);
        if (err == ESP_ERR_NVS_NOT_FOUND) { continue; }
        if (err != ESP_OK || size != sizeof(h->keys[id]) || !valid(&h->keys[id])) {
            ESP_LOGW(TAG, "key %u config unavailable (%s), using default", id, esp_err_to_name(err));
            set_default(id, &h->keys[id]);
        }
    }
    *out = h;
    return ESP_OK;
}

void keyboard_macro_delete(keyboard_macro_handle_t h)
{
    if (!h) { return; }
    nvs_close(h->nvs);
    vSemaphoreDelete(h->lock);
    free(h);
}

esp_err_t keyboard_macro_get(keyboard_macro_handle_t h, unsigned id, keyboard_macro_config_t *out)
{
    if (!h || !out || id < 1 || id > KEYBOARD_MACRO_KEYS) { return ESP_ERR_INVALID_ARG; }
    xSemaphoreTake(h->lock, portMAX_DELAY);
    *out = h->keys[id];
    xSemaphoreGive(h->lock);
    return ESP_OK;
}

esp_err_t keyboard_macro_set(keyboard_macro_handle_t h, unsigned id,
                             const keyboard_macro_config_t *config)
{
    if (!h || !config || id < 1 || id > KEYBOARD_MACRO_KEYS || !valid(config)) {
        return ESP_ERR_INVALID_ARG;
    }
    char name[8];
    snprintf(name, sizeof(name), "key%u", id);
    xSemaphoreTake(h->lock, portMAX_DELAY);
    esp_err_t err = nvs_set_blob(h->nvs, name, config, sizeof(*config));
    if (err == ESP_OK) { err = nvs_commit(h->nvs); }
    if (err == ESP_OK) { h->keys[id] = *config; }
    xSemaphoreGive(h->lock);
    return err;
}

esp_err_t keyboard_macro_reset(keyboard_macro_handle_t h, unsigned id)
{
    if (!h || id > KEYBOARD_MACRO_KEYS) { return ESP_ERR_INVALID_ARG; }
    xSemaphoreTake(h->lock, portMAX_DELAY);
    esp_err_t err;
    if (id) {
        char name[8];
        snprintf(name, sizeof(name), "key%u", id);
        err = nvs_erase_key(h->nvs, name);
        if (err == ESP_ERR_NVS_NOT_FOUND) { err = ESP_OK; }
    } else {
        err = nvs_erase_all(h->nvs); /* Only the dedicated keyboard namespace. */
    }
    if (err == ESP_OK) { err = nvs_commit(h->nvs); }
    if (err == ESP_OK) {
        for (unsigned i = 1; i <= KEYBOARD_MACRO_KEYS; ++i) {
            if (!id || id == i) { set_default(i, &h->keys[i]); }
        }
    }
    xSemaphoreGive(h->lock);
    return err;
}

static void merge(keyboard_macro_report_t *out, const keyboard_macro_report_t *add,
                  unsigned *count)
{
    out->modifiers |= add->modifiers;
    for (unsigned i = 0; i < KEYBOARD_MACRO_SLOTS; ++i) {
        uint8_t key = add->keys[i];
        if (!key) { continue; }
        bool duplicate = false;
        for (unsigned j = 0; j < KEYBOARD_MACRO_SLOTS; ++j) { duplicate |= out->keys[j] == key; }
        if (duplicate) { continue; }
        if (*count < KEYBOARD_MACRO_SLOTS) { out->keys[*count] = key; }
        ++*count;
    }
}

bool keyboard_macro_poll(keyboard_macro_handle_t h, uint16_t pressed,
                         uint32_t now, bool connected, keyboard_macro_report_t *out)
{
    memset(out, 0, sizeof(*out));
    if (!connected) {
        h->previous = pressed;
        h->phase = IDLE;
        h->queued = 0;
        h->head = 0;
        return false;
    }
    const uint16_t rising = pressed & ~h->previous;
    h->previous = pressed;
    xSemaphoreTake(h->lock, portMAX_DELAY);
    for (unsigned id = 1; id <= KEYBOARD_MACRO_KEYS; ++id) {
        if ((rising & (1U << (id - 1))) && h->keys[id].is_sequence) {
            if (h->queued < KEYBOARD_MACRO_KEYS) {
                h->queue[(h->head + h->queued) % KEYBOARD_MACRO_KEYS] = id;
                ++h->queued;
            } else { ESP_LOGW(TAG, "macro queue full, key %u ignored", id); }
        }
    }
    if (h->phase == IDLE && h->queued) {
        const unsigned id = h->queue[h->head];
        h->head = (h->head + 1) % KEYBOARD_MACRO_KEYS;
        --h->queued;
        h->playing = h->keys[id]; /* Snapshot; edits do not change a running macro. */
        h->step = 0;
        h->phase = PREPARE;
    }
    if ((h->phase == START_GAP || h->phase == HOLD || h->phase == WAIT) &&
        (int32_t)(now - h->due) >= 0) {
        if (h->phase == START_GAP) { h->phase = PRESS; }
        else if (h->phase == HOLD) { h->phase = RELEASE; }
        else if (++h->step < h->playing.count) { h->phase = PRESS; }
        else { h->phase = IDLE; }
    }
    if (h->phase == IDLE) {
        unsigned count = 0;
        for (unsigned id = 1; id <= KEYBOARD_MACRO_KEYS; ++id) {
            if ((pressed & (1U << (id - 1))) && !h->keys[id].is_sequence) {
                merge(out, &h->keys[id].steps[0].report, &count);
            }
        }
        if (count > KEYBOARD_MACRO_SLOTS) { memset(out->keys, 0x01, sizeof(out->keys)); }
    } else if (h->phase == PRESS || h->phase == HOLD) {
        *out = h->playing.steps[h->step].report;
    }
    xSemaphoreGive(h->lock);
    return h->phase == PREPARE || h->phase == PRESS || h->phase == RELEASE;
}

void keyboard_macro_sent(keyboard_macro_handle_t h, uint32_t now)
{
    if (h->phase == PREPARE) { h->phase = START_GAP; h->due = now + START_GAP_MS; }
    else if (h->phase == PRESS) { h->phase = HOLD; h->due = now + TAP_MS; }
    else if (h->phase == RELEASE) {
        h->phase = WAIT;
        h->due = now + h->playing.steps[h->step].delay_ms;
    }
}
