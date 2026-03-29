/*
 * json_parser.h - Minimal JSON parser for mini_agent
 *
 * A tiny recursive-descent JSON parser. Enough to read HAI_WOA.json
 * and parse OpenAI chat/completions streaming responses.
 *
 * Design: mirrors openclaw's config loading approach but in pure C.
 */
#ifndef JSON_PARSER_H
#define JSON_PARSER_H

#include <stddef.h>

/* JSON value types */
typedef enum {
    JSON_NULL,
    JSON_BOOL,
    JSON_NUMBER,
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT
} json_type_t;

/* Forward declaration */
typedef struct json_value json_value_t;

/* Key-value pair for objects */
typedef struct {
    char        *key;
    json_value_t *value;
} json_kv_t;

/* JSON value node */
struct json_value {
    json_type_t type;
    union {
        int          bool_val;
        double       num_val;
        char        *str_val;
        struct {
            json_value_t **items;
            size_t         count;
        } array;
        struct {
            json_kv_t *pairs;
            size_t     count;
        } object;
    } u;
};

/* Parse a JSON string. Returns NULL on error. */
json_value_t *json_parse(const char *input);

/* Free a parsed JSON tree */
void json_free(json_value_t *val);

/* Lookup helpers */
json_value_t *json_object_get(const json_value_t *obj, const char *key);
const char   *json_get_string(const json_value_t *obj, const char *key);
int           json_get_bool(const json_value_t *obj, const char *key, int default_val);

#endif /* JSON_PARSER_H */
