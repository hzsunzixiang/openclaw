/*
 * message.c - Conversation message management
 *
 * Builds and serializes the messages array for the OpenAI API.
 * The conversation follows openclaw's pattern:
 *   [system_prompt] -> [user_msg] -> [assistant_reply] -> [user_msg] -> ...
 */
#include "message.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *role_to_string(msg_role_t role)
{
    switch (role) {
        case ROLE_SYSTEM:    return "system";
        case ROLE_USER:      return "user";
        case ROLE_ASSISTANT: return "assistant";
    }
    return "user";
}

void conversation_init(conversation_t *conv, const char *system_prompt)
{
    conv->capacity = 16;
    conv->count    = 0;
    conv->msgs     = malloc(conv->capacity * sizeof(message_t));

    if (system_prompt && system_prompt[0] != '\0') {
        conversation_add(conv, ROLE_SYSTEM, system_prompt);
    }
}

void conversation_add(conversation_t *conv, msg_role_t role, const char *content)
{
    if (conv->count >= conv->capacity) {
        conv->capacity *= 2;
        conv->msgs = realloc(conv->msgs, conv->capacity * sizeof(message_t));
    }
    message_t *m = &conv->msgs[conv->count++];
    m->role    = role;
    m->content = strdup(content ? content : "");
}

/*
 * Escape a string for JSON embedding.
 * Handles: \, ", \n, \r, \t
 */
static char *json_escape(const char *src)
{
    size_t cap = strlen(src) * 2 + 1;
    char *out = malloc(cap);
    size_t j = 0;

    for (size_t i = 0; src[i]; i++) {
        if (j + 6 >= cap) {
            cap *= 2;
            out = realloc(out, cap);
        }
        switch (src[i]) {
            case '\\': out[j++] = '\\'; out[j++] = '\\'; break;
            case '"':  out[j++] = '\\'; out[j++] = '"';  break;
            case '\n': out[j++] = '\\'; out[j++] = 'n';  break;
            case '\r': out[j++] = '\\'; out[j++] = 'r';  break;
            case '\t': out[j++] = '\\'; out[j++] = 't';  break;
            default:   out[j++] = src[i]; break;
        }
    }
    out[j] = '\0';
    return out;
}

char *conversation_to_json(const conversation_t *conv)
{
    /* Estimate buffer size */
    size_t cap = 256;
    for (size_t i = 0; i < conv->count; i++)
        cap += strlen(conv->msgs[i].content) * 2 + 64;

    char *buf = malloc(cap);
    size_t pos = 0;

    buf[pos++] = '[';
    for (size_t i = 0; i < conv->count; i++) {
        if (i > 0) buf[pos++] = ',';

        char *escaped = json_escape(conv->msgs[i].content);
        int written = snprintf(buf + pos, cap - pos,
                               "{\"role\":\"%s\",\"content\":\"%s\"}",
                               role_to_string(conv->msgs[i].role),
                               escaped);
        free(escaped);

        if (written > 0) pos += (size_t)written;
    }
    buf[pos++] = ']';
    buf[pos]   = '\0';

    return buf;
}

void conversation_free(conversation_t *conv)
{
    for (size_t i = 0; i < conv->count; i++)
        free(conv->msgs[i].content);
    free(conv->msgs);
    conv->msgs  = NULL;
    conv->count = 0;
}
