/*
 * json_parser.c - Minimal recursive-descent JSON parser
 *
 * Supports: objects, arrays, strings, numbers, booleans, null.
 * Good enough for HAI_WOA.json and OpenAI SSE chunk parsing.
 */
#include "json_parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ── Internal parser state ─────────────────────────────────────── */

typedef struct {
    const char *src;
    size_t      pos;
} parser_t;

static void skip_ws(parser_t *p)
{
    while (p->src[p->pos] && isspace((unsigned char)p->src[p->pos]))
        p->pos++;
}

static char peek(parser_t *p)
{
    skip_ws(p);
    return p->src[p->pos];
}

static char advance(parser_t *p)
{
    return p->src[p->pos++];
}

static int expect(parser_t *p, char c)
{
    skip_ws(p);
    if (p->src[p->pos] == c) {
        p->pos++;
        return 1;
    }
    return 0;
}

/* ── Allocators ────────────────────────────────────────────────── */

static json_value_t *alloc_value(json_type_t type)
{
    json_value_t *v = calloc(1, sizeof(*v));
    if (v) v->type = type;
    return v;
}

/* ── Forward declarations ──────────────────────────────────────── */

static json_value_t *parse_value(parser_t *p);

/* ── String parsing ────────────────────────────────────────────── */

static char *parse_string_raw(parser_t *p)
{
    if (!expect(p, '"'))
        return NULL;

    size_t cap = 64, len = 0;
    char *buf = malloc(cap);
    if (!buf) return NULL;

    while (p->src[p->pos] && p->src[p->pos] != '"') {
        char c = advance(p);
        if (c == '\\') {
            c = advance(p);
            switch (c) {
                case '"':  c = '"';  break;
                case '\\': c = '\\'; break;
                case '/':  c = '/';  break;
                case 'b':  c = '\b'; break;
                case 'f':  c = '\f'; break;
                case 'n':  c = '\n'; break;
                case 'r':  c = '\r'; break;
                case 't':  c = '\t'; break;
                case 'u':
                    /* Skip 4 hex digits (simplified) */
                    for (int i = 0; i < 4 && p->src[p->pos]; i++)
                        advance(p);
                    c = '?';
                    break;
                default: break;
            }
        }
        if (len + 1 >= cap) {
            cap *= 2;
            buf = realloc(buf, cap);
            if (!buf) return NULL;
        }
        buf[len++] = c;
    }
    expect(p, '"'); /* closing quote */
    buf[len] = '\0';
    return buf;
}

static json_value_t *parse_string(parser_t *p)
{
    char *s = parse_string_raw(p);
    if (!s) return NULL;
    json_value_t *v = alloc_value(JSON_STRING);
    v->u.str_val = s;
    return v;
}

/* ── Number parsing ────────────────────────────────────────────── */

static json_value_t *parse_number(parser_t *p)
{
    char buf[64];
    int i = 0;
    while (p->src[p->pos] &&
           (isdigit((unsigned char)p->src[p->pos]) ||
            p->src[p->pos] == '.' ||
            p->src[p->pos] == '-' ||
            p->src[p->pos] == '+' ||
            p->src[p->pos] == 'e' ||
            p->src[p->pos] == 'E') &&
           i < 63) {
        buf[i++] = advance(p);
    }
    buf[i] = '\0';
    json_value_t *v = alloc_value(JSON_NUMBER);
    v->u.num_val = strtod(buf, NULL);
    return v;
}

/* ── Literal parsing (true, false, null) ───────────────────────── */

static json_value_t *parse_literal(parser_t *p)
{
    if (strncmp(p->src + p->pos, "true", 4) == 0) {
        p->pos += 4;
        json_value_t *v = alloc_value(JSON_BOOL);
        v->u.bool_val = 1;
        return v;
    }
    if (strncmp(p->src + p->pos, "false", 5) == 0) {
        p->pos += 5;
        json_value_t *v = alloc_value(JSON_BOOL);
        v->u.bool_val = 0;
        return v;
    }
    if (strncmp(p->src + p->pos, "null", 4) == 0) {
        p->pos += 4;
        return alloc_value(JSON_NULL);
    }
    return NULL;
}

/* ── Array parsing ─────────────────────────────────────────────── */

static json_value_t *parse_array(parser_t *p)
{
    if (!expect(p, '['))
        return NULL;

    json_value_t *arr = alloc_value(JSON_ARRAY);
    size_t cap = 4;
    arr->u.array.items = malloc(cap * sizeof(json_value_t *));
    arr->u.array.count = 0;

    if (peek(p) != ']') {
        do {
            json_value_t *item = parse_value(p);
            if (!item) break;
            if (arr->u.array.count >= cap) {
                cap *= 2;
                arr->u.array.items = realloc(arr->u.array.items,
                                             cap * sizeof(json_value_t *));
            }
            arr->u.array.items[arr->u.array.count++] = item;
        } while (expect(p, ','));
    }
    expect(p, ']');
    return arr;
}

/* ── Object parsing ────────────────────────────────────────────── */

static json_value_t *parse_object(parser_t *p)
{
    if (!expect(p, '{'))
        return NULL;

    json_value_t *obj = alloc_value(JSON_OBJECT);
    size_t cap = 8;
    obj->u.object.pairs = malloc(cap * sizeof(json_kv_t));
    obj->u.object.count = 0;

    if (peek(p) != '}') {
        do {
            skip_ws(p);
            char *key = parse_string_raw(p);
            if (!key) break;
            if (!expect(p, ':')) { free(key); break; }
            json_value_t *val = parse_value(p);
            if (!val) { free(key); break; }

            if (obj->u.object.count >= cap) {
                cap *= 2;
                obj->u.object.pairs = realloc(obj->u.object.pairs,
                                              cap * sizeof(json_kv_t));
            }
            json_kv_t *kv = &obj->u.object.pairs[obj->u.object.count++];
            kv->key   = key;
            kv->value = val;
        } while (expect(p, ','));
    }
    expect(p, '}');
    return obj;
}

/* ── Generic value parsing ─────────────────────────────────────── */

static json_value_t *parse_value(parser_t *p)
{
    skip_ws(p);
    char c = p->src[p->pos];
    if (c == '"')  return parse_string(p);
    if (c == '{')  return parse_object(p);
    if (c == '[')  return parse_array(p);
    if (c == '-' || isdigit((unsigned char)c)) return parse_number(p);
    return parse_literal(p);
}

/* ── Public API ────────────────────────────────────────────────── */

json_value_t *json_parse(const char *input)
{
    if (!input) return NULL;
    parser_t p = { .src = input, .pos = 0 };
    return parse_value(&p);
}

void json_free(json_value_t *val)
{
    if (!val) return;
    switch (val->type) {
        case JSON_STRING:
            free(val->u.str_val);
            break;
        case JSON_ARRAY:
            for (size_t i = 0; i < val->u.array.count; i++)
                json_free(val->u.array.items[i]);
            free(val->u.array.items);
            break;
        case JSON_OBJECT:
            for (size_t i = 0; i < val->u.object.count; i++) {
                free(val->u.object.pairs[i].key);
                json_free(val->u.object.pairs[i].value);
            }
            free(val->u.object.pairs);
            break;
        default:
            break;
    }
    free(val);
}

json_value_t *json_object_get(const json_value_t *obj, const char *key)
{
    if (!obj || obj->type != JSON_OBJECT || !key)
        return NULL;
    for (size_t i = 0; i < obj->u.object.count; i++) {
        if (strcmp(obj->u.object.pairs[i].key, key) == 0)
            return obj->u.object.pairs[i].value;
    }
    return NULL;
}

const char *json_get_string(const json_value_t *obj, const char *key)
{
    json_value_t *v = json_object_get(obj, key);
    if (v && v->type == JSON_STRING)
        return v->u.str_val;
    return NULL;
}

int json_get_bool(const json_value_t *obj, const char *key, int default_val)
{
    json_value_t *v = json_object_get(obj, key);
    if (v && v->type == JSON_BOOL)
        return v->u.bool_val;
    return default_val;
}
