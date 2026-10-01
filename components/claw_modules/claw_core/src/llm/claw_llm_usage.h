#pragma once

#include <math.h>
#include "claw_llm_types.h"

/* Missing usage is unknown, not zero. No token estimates from text length. */
static inline bool claw_llm_usage_count(cJSON *usage, const char *name, uint64_t *out)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(usage, name);
    if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble) ||
            item->valuedouble < 0 || item->valuedouble > UINT32_MAX ||
            floor(item->valuedouble) != item->valuedouble) {
        return false;
    }
    *out = (uint64_t)item->valuedouble;
    return true;
}

static inline void claw_llm_usage_parse(cJSON *root, bool anthropic, claw_llm_response_t *out)
{
    cJSON *usage = cJSON_GetObjectItemCaseSensitive(root, "usage");
    uint64_t input = 0, output = 0, cached = 0, created = 0;
    if (!cJSON_IsObject(usage) ||
            !claw_llm_usage_count(usage, anthropic ? "input_tokens" : "prompt_tokens", &input) ||
            !claw_llm_usage_count(usage, anthropic ? "output_tokens" : "completion_tokens", &output)) {
        return;
    }
    if (anthropic) {
        if (cJSON_HasObjectItem(usage, "cache_read_input_tokens") &&
                !claw_llm_usage_count(usage, "cache_read_input_tokens", &cached)) {
            return;
        }
        if (cJSON_HasObjectItem(usage, "cache_creation_input_tokens") &&
                !claw_llm_usage_count(usage, "cache_creation_input_tokens", &created)) {
            return;
        }
    }
    out->input_tokens = input + cached + created;
    out->output_tokens = output;
    out->usage_available = true;
}
