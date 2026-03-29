/*
 * config.c - Load and parse HAI_WOA.json configuration
 *
 * This mirrors how openclaw loads provider configuration:
 *   1. Read the JSON file
 *   2. Extract env.OPENAI_API_KEY and env.OPENAI_BASE_URL
 *   3. Extract the default model and model aliases
 *   4. Check openai.useChatCompletions flag
 */
#include "config.h"
#include "json_parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Read entire file into a malloc'd buffer ───────────────────── */

static char *read_file_contents(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "[config] Cannot open: %s\n", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *buf = malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    fread(buf, 1, (size_t)sz, f);
    buf[sz] = '\0';
    fclose(f);
    return buf;
}

/* ── Safe string copy helper ───────────────────────────────────── */

static void safe_copy(char *dst, size_t dstsz, const char *src)
{
    if (!src) { dst[0] = '\0'; return; }
    strncpy(dst, src, dstsz - 1);
    dst[dstsz - 1] = '\0';
}

/* ── Public: load config ───────────────────────────────────────── */

int config_load(agent_config_t *cfg, const char *filepath)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->use_chat_completions = 1; /* default on */

    char *text = read_file_contents(filepath);
    if (!text) return -1;

    json_value_t *root = json_parse(text);
    free(text);
    if (!root) {
        fprintf(stderr, "[config] JSON parse error in %s\n", filepath);
        return -1;
    }

    /* ── Top-level "model" ─────────────────────────────────────── */
    const char *model = json_get_string(root, "model");
    safe_copy(cfg->default_model, sizeof(cfg->default_model), model);

    /* ── "models" object: alias -> { "id": "..." } ────────────── */
    json_value_t *models = json_object_get(root, "models");
    if (models && models->type == JSON_OBJECT) {
        for (size_t i = 0; i < models->u.object.count && cfg->model_count < MAX_MODELS; i++) {
            json_kv_t *kv = &models->u.object.pairs[i];
            const char *id = json_get_string(kv->value, "id");
            if (id) {
                model_entry_t *e = &cfg->models[cfg->model_count++];
                safe_copy(e->alias, sizeof(e->alias), kv->key);
                safe_copy(e->id, sizeof(e->id), id);
            }
        }
    }

    /* ── "env" object ──────────────────────────────────────────── */
    json_value_t *env = json_object_get(root, "env");
    if (env) {
        safe_copy(cfg->api_key,  sizeof(cfg->api_key),
                  json_get_string(env, "OPENAI_API_KEY"));
        safe_copy(cfg->base_url, sizeof(cfg->base_url),
                  json_get_string(env, "OPENAI_BASE_URL"));
    }

    /* ── "openai" object ───────────────────────────────────────── */
    json_value_t *openai = json_object_get(root, "openai");
    if (openai) {
        cfg->use_chat_completions = json_get_bool(openai,
                                                  "useChatCompletions", 1);
    }

    json_free(root);
    return 0;
}

/* ── Public: dump config ───────────────────────────────────────── */

void config_dump(const agent_config_t *cfg)
{
    printf("╔══════════════════════════════════════════════╗\n");
    printf("║         HAI_WOA Configuration Loaded        ║\n");
    printf("╠══════════════════════════════════════════════╣\n");
    printf("║ Default model : %-27s ║\n", cfg->default_model);
    printf("║ Base URL      : %-27s ║\n", cfg->base_url);
    printf("║ API Key       : %.8s...%-17s ║\n", cfg->api_key, "(hidden)");
    printf("║ ChatCompletions: %-26s ║\n",
           cfg->use_chat_completions ? "enabled" : "disabled");
    printf("║ Model aliases (%d):%-24s ║\n", cfg->model_count, "");
    for (int i = 0; i < cfg->model_count; i++) {
        printf("║   %-6s -> %-31s ║\n",
               cfg->models[i].alias, cfg->models[i].id);
    }
    printf("╚══════════════════════════════════════════════╝\n");
}

/*
 * Strip the provider prefix from a model id.
 *
 * openclaw uses "provider/model-name" format internally (e.g.
 * "openai/DeepSeek-V3-0324"), but the actual API only wants the
 * model name part (e.g. "DeepSeek-V3-0324").
 *
 * This function returns a pointer into the original string, past
 * the first '/' if present.
 */
static const char *strip_provider_prefix(const char *model_id)
{
    if (!model_id) return model_id;
    const char *slash = strchr(model_id, '/');
    return slash ? slash + 1 : model_id;
}

/* ── Public: resolve model name ────────────────────────────────── */

const char *config_resolve_model(const agent_config_t *cfg, const char *name)
{
    const char *raw;

    if (!name || name[0] == '\0')
        raw = cfg->default_model;
    else {
        /* Check aliases first */
        raw = NULL;
        for (int i = 0; i < cfg->model_count; i++) {
            if (strcmp(cfg->models[i].alias, name) == 0) {
                raw = cfg->models[i].id;
                break;
            }
        }
        /* Not an alias, use as-is */
        if (!raw) raw = name;
    }

    /*
     * Strip provider prefix: "openai/DeepSeek-V3-0324" -> "DeepSeek-V3-0324"
     * The haihub API (and most OpenAI-compatible APIs) don't recognize
     * the "openai/" prefix that openclaw uses for internal routing.
     */
    return strip_provider_prefix(raw);
}
