/*
 * message.h - Conversation message management
 *
 * Manages the message list sent to the LLM. Mirrors openclaw's
 * conversation model: system prompt + user/assistant message history.
 */
#ifndef MESSAGE_H
#define MESSAGE_H

#include <stddef.h>

/* Message roles (matching OpenAI API) */
typedef enum {
    ROLE_SYSTEM,
    ROLE_USER,
    ROLE_ASSISTANT
} msg_role_t;

/* A single message in the conversation */
typedef struct {
    msg_role_t  role;
    char       *content;
} message_t;

/* Conversation: an ordered list of messages */
typedef struct {
    message_t *msgs;
    size_t     count;
    size_t     capacity;
} conversation_t;

/* Initialize a conversation with a system prompt */
void conversation_init(conversation_t *conv, const char *system_prompt);

/* Add a message to the conversation */
void conversation_add(conversation_t *conv, msg_role_t role, const char *content);

/* Serialize all messages to a JSON array string (caller must free) */
char *conversation_to_json(const conversation_t *conv);

/* Free all resources */
void conversation_free(conversation_t *conv);

/* Get role name string */
const char *role_to_string(msg_role_t role);

#endif /* MESSAGE_H */
