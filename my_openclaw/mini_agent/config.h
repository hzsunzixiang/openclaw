/*
 * config.h - Configuration loader for HAI_WOA.json
 *
 * Mirrors openclaw's config loading: reads the JSON config file,
 * extracts model ID, API key, base URL, and streaming preference.
 */
#ifndef CONFIG_H
#define CONFIG_H

/* Maximum number of model aliases */
#define MAX_MODELS 16

/* A named model alias (e.g. "ds" -> "openai/DeepSeek-V3-0324") */
typedef struct {
    char alias[32];
    char id[128];
} model_entry_t;

/* Loaded configuration from HAI_WOA.json */
typedef struct {
    /* Default model id (from top-level "model" field) */
    char default_model[128];

    /* Named model aliases */
    model_entry_t models[MAX_MODELS];
    int           model_count;

    /* API credentials (from "env" section) */
    char api_key[256];
    char base_url[256];

    /* Whether to use chat/completions endpoint */
    int use_chat_completions;
} agent_config_t;

/*
 * Load configuration from a JSON file.
 * Returns 0 on success, -1 on error.
 */
int config_load(agent_config_t *cfg, const char *filepath);

/* Print config summary to stdout (for debugging) */
void config_dump(const agent_config_t *cfg);

/*
 * Resolve a model name: if it matches an alias, return the full id;
 * otherwise return the input as-is. If NULL, return default model.
 */
const char *config_resolve_model(const agent_config_t *cfg, const char *name);

#endif /* CONFIG_H */
