/* SPDX-License-Identifier: Apache-2.0 */
#include "keyboard_macro.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "cJSON.h"
#include "class/hid/hid.h"
#include "claw_cap.h"
#include "app_capabilities.h"

static keyboard_macro_handle_t s_keyboard;
typedef struct { const char *name; uint8_t code; } named_key_t;
static const named_key_t s_named_keys[] = {
    {"ENTER", HID_KEY_ENTER}, {"ESC", HID_KEY_ESCAPE}, {"BACKSPACE", HID_KEY_BACKSPACE},
    {"TAB", HID_KEY_TAB}, {"SPACE", HID_KEY_SPACE}, {"DELETE", HID_KEY_DELETE},
    {"INSERT", HID_KEY_INSERT}, {"HOME", HID_KEY_HOME}, {"END", HID_KEY_END},
    {"PAGEUP", HID_KEY_PAGE_UP}, {"PAGEDOWN", HID_KEY_PAGE_DOWN},
    {"LEFT", HID_KEY_ARROW_LEFT}, {"RIGHT", HID_KEY_ARROW_RIGHT},
    {"UP", HID_KEY_ARROW_UP}, {"DOWN", HID_KEY_ARROW_DOWN},
    {"MINUS", HID_KEY_MINUS}, {"EQUAL", HID_KEY_EQUAL}, {"COMMA", HID_KEY_COMMA},
    {"PERIOD", HID_KEY_PERIOD}, {"SLASH", HID_KEY_SLASH}, {"SEMICOLON", HID_KEY_SEMICOLON},
    {"APOSTROPHE", HID_KEY_APOSTROPHE}, {"BACKSLASH", HID_KEY_BACKSLASH},
    {"BRACKET_LEFT", HID_KEY_BRACKET_LEFT}, {"BRACKET_RIGHT", HID_KEY_BRACKET_RIGHT},
    {"GRAVE", HID_KEY_GRAVE}, {"CAPSLOCK", HID_KEY_CAPS_LOCK},
    {"PRINTSCREEN", HID_KEY_PRINT_SCREEN}, {"SCROLLLOCK", HID_KEY_SCROLL_LOCK}, {"PAUSE", HID_KEY_PAUSE},
};
static const char *s_mod_names[] = {"CTRL", "SHIFT", "ALT", "WIN"};

static esp_err_t result(cJSON *json, char *out, size_t size)
{
    if (!json) { return ESP_ERR_NO_MEM; }
    char *text = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    if (!text) { return ESP_ERR_NO_MEM; }
    if (strlen(text) >= size) { free(text); return ESP_ERR_INVALID_SIZE; }
    memcpy(out, text, strlen(text) + 1);
    free(text);
    return ESP_OK;
}
static esp_err_t failure(const char *message, char *out, size_t size)
{
    cJSON *json = cJSON_CreateObject();
    if (!json) { return ESP_ERR_NO_MEM; }
    cJSON_AddBoolToObject(json, "ok", false);
    cJSON_AddStringToObject(json, "error", message);
    return result(json, out, size);
}
static bool integer(cJSON *item, int min, int max)
{
    return cJSON_IsNumber(item) && item->valuedouble >= min && item->valuedouble <= max &&
           item->valuedouble == item->valueint;
}
static bool decode_key(const char *name, uint8_t *code, uint8_t *modifier)
{
    *code = 0; *modifier = 0;
    for (unsigned i = 0; i < 4; ++i) {
        if (!strcasecmp(name, s_mod_names[i])) { *modifier = 1U << i; return true; }
    }
    if (!strcasecmp(name, "CONTROL")) { *modifier = 1; return true; }
    if (!strcasecmp(name, "GUI")) { *modifier = 8; return true; }
    if (strlen(name) == 1) {
        char c = name[0];
        if (c >= 'a' && c <= 'z') { c -= 'a' - 'A'; }
        if (c >= 'A' && c <= 'Z') { *code = HID_KEY_A + c - 'A'; return true; }
        if (c >= '1' && c <= '9') { *code = HID_KEY_1 + c - '1'; return true; }
        if (c == '0') { *code = HID_KEY_0; return true; }
    }
    if ((name[0] == 'F' || name[0] == 'f') && name[1]) {
        char *end;
        long n = strtol(name + 1, &end, 10);
        if (!*end && n >= 1 && n <= 12) { *code = HID_KEY_F1 + n - 1; return true; }
    }
    for (unsigned i = 0; i < sizeof(s_named_keys) / sizeof(s_named_keys[0]); ++i) {
        if (!strcasecmp(name, s_named_keys[i].name)) { *code = s_named_keys[i].code; return true; }
    }
    return false;
}
static const char *encode_key(uint8_t code, char *buffer, size_t size)
{
    if (code >= HID_KEY_A && code <= HID_KEY_Z) { snprintf(buffer, size, "%c", 'A' + code - HID_KEY_A); return buffer; }
    if (code >= HID_KEY_1 && code <= HID_KEY_9) { snprintf(buffer, size, "%c", '1' + code - HID_KEY_1); return buffer; }
    if (code == HID_KEY_0) { return "0"; }
    if (code >= HID_KEY_F1 && code <= HID_KEY_F12) { snprintf(buffer, size, "F%u", code - HID_KEY_F1 + 1); return buffer; }
    for (unsigned i = 0; i < sizeof(s_named_keys) / sizeof(s_named_keys[0]); ++i) {
        if (code == s_named_keys[i].code) { return s_named_keys[i].name; }
    }
    return "UNKNOWN";
}
static cJSON *config_json(unsigned id, const keyboard_macro_config_t *config)
{
    cJSON *json = cJSON_CreateObject();
    if (!json) { return NULL; }
    cJSON_AddNumberToObject(json, "key_id", id);
    cJSON_AddNumberToObject(json, "row", (id - 1) / 3 + 1);
    cJSON_AddNumberToObject(json, "column", (id - 1) % 3 + 1);
    cJSON_AddStringToObject(json, "mode", config->is_sequence ? "macro" : "shortcut");
    cJSON *steps = cJSON_AddArrayToObject(json, "steps");
    for (unsigned i = 0; i < config->count; ++i) {
        cJSON *step = cJSON_CreateObject();
        cJSON_AddItemToArray(steps, step);
        cJSON *keys = cJSON_AddArrayToObject(step, "keys");
        for (unsigned m = 0; m < 4; ++m) {
            if (config->steps[i].report.modifiers & (1U << m)) {
                cJSON_AddItemToArray(keys, cJSON_CreateString(s_mod_names[m]));
            }
        }
        for (unsigned k = 0; k < KEYBOARD_MACRO_SLOTS; ++k) {
            uint8_t code = config->steps[i].report.keys[k];
            if (code) {
                char name[8];
                cJSON_AddItemToArray(keys, cJSON_CreateString(encode_key(code, name, sizeof(name))));
            }
        }
        cJSON_AddNumberToObject(step, "delay_ms", config->steps[i].delay_ms);
    }
    return json;
}

static esp_err_t set_macro(const char *input, const claw_cap_call_context_t *ctx, char *out, size_t size)
{
    (void)ctx;
    cJSON *json = cJSON_Parse(input);
    cJSON *id = cJSON_GetObjectItemCaseSensitive(json, "key_id");
    cJSON *mode = cJSON_GetObjectItemCaseSensitive(json, "mode");
    cJSON *steps = cJSON_GetObjectItemCaseSensitive(json, "steps");
    const char *error = NULL;
    keyboard_macro_config_t *config = calloc(1, sizeof(*config));
    if (!config) { cJSON_Delete(json); return ESP_ERR_NO_MEM; }
    if (!cJSON_IsObject(json) || !integer(id, 1, 9) || !cJSON_IsString(mode) ||
        (strcmp(mode->valuestring, "shortcut") && strcmp(mode->valuestring, "macro")) ||
        !cJSON_IsArray(steps) || cJSON_GetArraySize(steps) < 1 || cJSON_GetArraySize(steps) > KEYBOARD_MACRO_STEPS) {
        error = "Require key_id 1..9, mode shortcut/macro, and 1..16 steps.";
    } else {
        config->version = 1;
        config->is_sequence = !strcmp(mode->valuestring, "macro");
        config->count = cJSON_GetArraySize(steps);
        if (!config->is_sequence && config->count != 1) { error = "shortcut requires exactly one step."; }
        for (unsigned i = 0; !error && i < config->count; ++i) {
            cJSON *step = cJSON_GetArrayItem(steps, i);
            cJSON *keys = cJSON_GetObjectItemCaseSensitive(step, "keys");
            cJSON *delay = cJSON_GetObjectItemCaseSensitive(step, "delay_ms");
            if (!cJSON_IsObject(step) || !cJSON_IsArray(keys) || cJSON_GetArraySize(keys) < 1 || cJSON_GetArraySize(keys) > 10 ||
                (delay && !integer(delay, 0, KEYBOARD_MACRO_MAX_DELAY_MS))) {
                error = "Each step needs keys and optional integer delay_ms 0..10000."; break;
            }
            config->steps[i].delay_ms = delay ? delay->valueint : 0;
            if (!config->is_sequence && config->steps[i].delay_ms) { error = "shortcut has no delay."; break; }
            unsigned used = 0;
            cJSON *key;
            cJSON_ArrayForEach(key, keys) {
                uint8_t code, modifier;
                if (!cJSON_IsString(key) || !decode_key(key->valuestring, &code, &modifier)) {
                    error = "Unknown key name. Use supported HID key names, not text or commands."; break;
                }
                config->steps[i].report.modifiers |= modifier;
                if (code) {
                    if (used == KEYBOARD_MACRO_SLOTS) { error = "At most 6 non-modifier keys in one step."; break; }
                    config->steps[i].report.keys[used++] = code;
                }
            }
        }
    }
    esp_err_t err;
    if (error) { err = failure(error, out, size); }
    else {
        err = keyboard_macro_set(s_keyboard, id->valueint, config);
        if (err == ESP_OK) {
            cJSON *answer = config_json(id->valueint, config);
            cJSON_AddBoolToObject(answer, "ok", true);
            cJSON_AddBoolToObject(answer, "persisted", true);
            err = result(answer, out, size);
        } else { err = failure(esp_err_to_name(err), out, size); }
    }
    free(config);
    cJSON_Delete(json);
    return err;
}

static esp_err_t get_config(const char *input, const claw_cap_call_context_t *ctx, char *out, size_t size)
{
    (void)ctx;
    cJSON *json = cJSON_Parse(input);
    cJSON *id = cJSON_GetObjectItemCaseSensitive(json, "key_id");
    if (!cJSON_IsObject(json) || (id && !integer(id, 1, 9))) {
        cJSON_Delete(json); return failure("Optional key_id must be 1..9.", out, size);
    }
    unsigned selected = id ? id->valueint : 0;
    cJSON_Delete(json);
    keyboard_macro_config_t *config = malloc(sizeof(*config));
    if (!config) { return ESP_ERR_NO_MEM; }
    cJSON *answer = cJSON_CreateObject();
    cJSON_AddBoolToObject(answer, "ok", true);
    cJSON *keys = cJSON_AddArrayToObject(answer, "keys");
    for (unsigned i = 1; i <= KEYBOARD_MACRO_KEYS; ++i) {
        if (selected && selected != i) { continue; }
        esp_err_t err = keyboard_macro_get(s_keyboard, i, config);
        if (err != ESP_OK) {
            free(config); cJSON_Delete(answer); return failure(esp_err_to_name(err), out, size);
        }
        cJSON_AddItemToArray(keys, config_json(i, config));
    }
    free(config);
    return result(answer, out, size);
}

static esp_err_t reset_key(const char *input, const claw_cap_call_context_t *ctx, char *out, size_t size)
{
    (void)ctx;
    cJSON *json = cJSON_Parse(input);
    cJSON *id = cJSON_GetObjectItemCaseSensitive(json, "key_id");
    if (!cJSON_IsObject(json) || (id && !integer(id, 1, 9))) {
        cJSON_Delete(json); return failure("Optional key_id must be 1..9; omit to reset all nine keys.", out, size);
    }
    unsigned selected = id ? id->valueint : 0;
    cJSON_Delete(json);
    esp_err_t err = keyboard_macro_reset(s_keyboard, selected);
    if (err != ESP_OK) { return failure(esp_err_to_name(err), out, size); }
    cJSON *answer = cJSON_CreateObject();
    cJSON_AddBoolToObject(answer, "ok", true);
    cJSON_AddBoolToObject(answer, "persisted", true);
    cJSON_AddNumberToObject(answer, "key_id", selected);
    cJSON_AddStringToObject(answer, "message", selected ? "key restored to its default digit" : "all keys restored to digits 1..9");
    return result(answer, out, size);
}

static const claw_cap_descriptor_t s_tools[] = {
    {
        .id = "keyboard_set_macro", .name = "keyboard_set_macro", .family = "keyboard",
        .description = "Set and persist physical USB key 1..9. Use only for an explicit user request. "
            "shortcut: one key/chord held until physical release. macro: ordered tap/release steps, one run per physical press. "
            "Keys: A-Z,0-9,F1-F12,CTRL,SHIFT,ALT,WIN,ENTER,ESC,TAB,SPACE,BACKSPACE,DELETE,INSERT,HOME,END,PAGEUP,PAGEDOWN,"
            "LEFT,RIGHT,UP,DOWN,MINUS,EQUAL,COMMA,PERIOD,SLASH,SEMICOLON,APOSTROPHE,BACKSLASH,BRACKET_LEFT,BRACKET_RIGHT,GRAVE,CAPSLOCK,PRINTSCREEN,SCROLLLOCK,PAUSE. "
            "Example copy: key_id=1, mode=shortcut, steps=[{keys:[CTRL,C]}]. delay_ms waits after step release. "
            "No arbitrary text or app launch. Only claim saved after ok=true.",
        .kind = CLAW_CAP_KIND_CALLABLE, .cap_flags = CLAW_CAP_FLAG_CALLABLE_BY_LLM,
        .input_schema_json = "{\"type\":\"object\",\"properties\":{\"key_id\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":9},"
            "\"mode\":{\"type\":\"string\",\"enum\":[\"shortcut\",\"macro\"]},\"steps\":{\"type\":\"array\",\"minItems\":1,\"maxItems\":16,"
            "\"items\":{\"type\":\"object\",\"properties\":{\"keys\":{\"type\":\"array\",\"minItems\":1,\"maxItems\":10,\"items\":{\"type\":\"string\"}},"
            "\"delay_ms\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":10000}},\"required\":[\"keys\"],\"additionalProperties\":false}}},"
            "\"required\":[\"key_id\",\"mode\",\"steps\"],\"additionalProperties\":false}",
        .execute = set_macro,
    },
    {
        .id = "keyboard_get_config", .name = "keyboard_get_config", .family = "keyboard",
        .description = "Read actual saved USB keyboard configuration. Optional physical key_id 1..9; omit for all keys. "
            "Physical layout: top 1,2,3; middle 4,5,6; bottom 7,8,9. Never infer current actions from IDs.",
        .kind = CLAW_CAP_KIND_CALLABLE, .cap_flags = CLAW_CAP_FLAG_CALLABLE_BY_LLM,
        .input_schema_json = "{\"type\":\"object\",\"properties\":{\"key_id\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":9}},\"additionalProperties\":false}",
        .execute = get_config,
    },
    {
        .id = "keyboard_reset_key", .name = "keyboard_reset_key", .family = "keyboard",
        .description = "Restore default digit for a physical key 1..9. Omit key_id to reset all nine keys. "
            "Only on explicit user request; does not touch WiFi/WeChat data.",
        .kind = CLAW_CAP_KIND_CALLABLE, .cap_flags = CLAW_CAP_FLAG_CALLABLE_BY_LLM,
        .input_schema_json = "{\"type\":\"object\",\"properties\":{\"key_id\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":9}},\"additionalProperties\":false}",
        .execute = reset_key,
    },
};
static esp_err_t register_tools(const app_claw_config_t *config, const app_claw_storage_paths_t *paths)
{
    (void)config; (void)paths;
    static const claw_cap_group_t group = {
        .group_id = "cap_keyboard", .descriptors = s_tools,
        .descriptor_count = sizeof(s_tools) / sizeof(s_tools[0]),
    };
    return claw_cap_register_group(&group);
}
esp_err_t keyboard_macro_register_ai(keyboard_macro_handle_t handle)
{
    s_keyboard = handle;
    const app_capability_external_group_t group = {
        .group_id = "cap_keyboard", .display_name = "九键 USB 宏键盘",
        .llm_visible_by_default = true, .reg = register_tools,
    };
    return app_capabilities_register_external_group(&group);
}
