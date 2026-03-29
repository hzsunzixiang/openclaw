/*
 * agent.c - Mini Agent: the complete input-to-output pipeline
 *
 * ═══════════════════════════════════════════════════════════════
 *  Architecture Overview (mirrors openclaw's core flow):
 * ═══════════════════════════════════════════════════════════════
 *
 *  ┌─────────────┐
 *  │  HAI_WOA.json│  ← Configuration (model, API key, base URL)
 *  └──────┬──────┘
 *         │ config_load()
 *         ▼
 *  ┌─────────────┐
 *  │  agent_config│  ← Parsed config struct
 *  └──────┬──────┘
 *         │
 *         ▼
 *  ┌─────────────────────────────────────────────────────┐
 *  │                   MAIN LOOP                         │
 *  │                                                     │
 *  │  1. Read user input from stdin                      │
 *  │  2. Add to conversation (message.c)                 │
 *  │  3. Serialize messages to JSON                      │
 *  │  4. POST to /chat/completions with SSE streaming    │
 *  │  5. Parse SSE "data:" lines in real-time            │
 *  │  6. Extract choices[0].delta.content                │
 *  │  7. Print tokens as they arrive                     │
 *  │  8. Accumulate full response                        │
 *  │  9. Add assistant reply to conversation history     │
 *  │ 10. Loop back to step 1                             │
 *  └─────────────────────────────────────────────────────┘
 *
 * ═══════════════════════════════════════════════════════════════
 *  Data Flow:
 * ═══════════════════════════════════════════════════════════════
 *
 *  stdin ──► conversation_add(USER) ──► conversation_to_json()
 *                                              │
 *                                              ▼
 *                                    http_stream_chat()
 *                                              │
 *                                    ┌─────────┴─────────┐
 *                                    │  libcurl POST      │
 *                                    │  SSE write_callback│
 *                                    │  parse "data:" line│
 *                                    │  json_parse(chunk) │
 *                                    │  extract .content  │
 *                                    └─────────┬─────────┘
 *                                              │
 *                                              ▼
 *                                    stream_token_cb()
 *                                              │
 *                                    ┌─────────┴─────────┐
 *                                    │  print to stdout   │
 *                                    │  accumulate reply  │
 *                                    └─────────┬─────────┘
 *                                              │
 *                                              ▼
 *                                    conversation_add(ASSISTANT)
 *                                              │
 *                                              ▼
 *                                         next turn
 *
 * ═══════════════════════════════════════════════════════════════
 *  Module Dependency:
 * ═══════════════════════════════════════════════════════════════
 *
 *  agent.c (this file)
 *    ├── config.h      → loads HAI_WOA.json
 *    │     └── json_parser.h  → parses JSON
 *    ├── message.h     → manages conversation history
 *    └── http_client.h → streams chat completions
 *          ├── json_parser.h  → parses SSE chunks
 *          └── libcurl        → HTTP transport
 */

#include "config.h"
#include "message.h"
#include "http_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

/* ── Default system prompt ─────────────────────────────────────── */

#define DEFAULT_SYSTEM_PROMPT \
    "You are a helpful AI assistant. Answer concisely and clearly."

/* ── Global state for signal handling ──────────────────────────── */

static volatile int g_running = 1;

static void sigint_handler(int sig)
{
    (void)sig;
    g_running = 0;
    printf("\n[Interrupted]\n");
}

/* ── Response accumulator (used in streaming callback) ─────────── */

typedef struct {
    char   *buf;
    size_t  len;
    size_t  cap;
} response_buf_t;

static void response_init(response_buf_t *r)
{
    r->cap = 1024;
    r->len = 0;
    r->buf = malloc(r->cap);
    r->buf[0] = '\0';
}

static void response_append(response_buf_t *r, const char *s, size_t slen)
{
    while (r->len + slen + 1 > r->cap) {
        r->cap *= 2;
        r->buf = realloc(r->buf, r->cap);
    }
    memcpy(r->buf + r->len, s, slen);
    r->len += slen;
    r->buf[r->len] = '\0';
}

static void response_free(response_buf_t *r)
{
    free(r->buf);
}

/* ── Streaming callback: print tokens + accumulate ─────────────── */

static void on_token(const char *token, size_t len, int done, void *userdata)
{
    response_buf_t *resp = (response_buf_t *)userdata;

    if (done) {
        /* Stream finished */
        printf("\n");
        fflush(stdout);
        return;
    }

    /* Print token immediately (real-time streaming effect) */
    fwrite(token, 1, len, stdout);
    fflush(stdout);

    /* Accumulate for conversation history */
    response_append(resp, token, len);
}

/* ── Read a line of input from stdin ───────────────────────────── */

static char *read_input(void)
{
    printf("\033[1;36mYou > \033[0m");
    fflush(stdout);

    size_t cap = 256, len = 0;
    char *buf = malloc(cap);

    int c;
    while ((c = fgetc(stdin)) != EOF && c != '\n') {
        if (len + 1 >= cap) {
            cap *= 2;
            buf = realloc(buf, cap);
        }
        buf[len++] = (char)c;
    }

    if (c == EOF && len == 0) {
        free(buf);
        return NULL; /* EOF */
    }

    buf[len] = '\0';
    return buf;
}

/* ── Print usage ───────────────────────────────────────────────── */

static void print_usage(const char *prog)
{
    printf("Usage: %s [options]\n", prog);
    printf("Options:\n");
    printf("  -c <path>    Path to HAI_WOA.json (default: ../HAI_WOA.json)\n");
    printf("  -m <model>   Model name or alias (default: from config)\n");
    printf("  -s <prompt>  Custom system prompt\n");
    printf("  -h           Show this help\n");
}

/* ── Print banner ──────────────────────────────────────────────── */

static void print_banner(const char *model)
{
    printf("\n");
    printf("  ╔═══════════════════════════════════════════╗\n");
    printf("  ║     🤖 Mini Agent (openclaw-inspired)     ║\n");
    printf("  ╠═══════════════════════════════════════════╣\n");
    printf("  ║  Model: %-33s║\n", model);
    printf("  ║  Type 'quit' or Ctrl-D to exit            ║\n");
    printf("  ║  Type '/model <name>' to switch model     ║\n");
    printf("  ╚═══════════════════════════════════════════╝\n");
    printf("\n");
}

/* ═══════════════════════════════════════════════════════════════ */
/*                          MAIN                                  */
/* ═══════════════════════════════════════════════════════════════ */

int main(int argc, char *argv[])
{
    /* ── Parse command-line arguments ──────────────────────────── */
    const char *config_path   = "../HAI_WOA.json";
    const char *model_arg     = NULL;
    const char *system_prompt = DEFAULT_SYSTEM_PROMPT;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0 && i + 1 < argc)
            config_path = argv[++i];
        else if (strcmp(argv[i], "-m") == 0 && i + 1 < argc)
            model_arg = argv[++i];
        else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc)
            system_prompt = argv[++i];
        else if (strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        }
    }

    /* ── Step 1: Load configuration from HAI_WOA.json ─────────── */
    agent_config_t cfg;
    if (config_load(&cfg, config_path) != 0) {
        fprintf(stderr, "Failed to load config from: %s\n", config_path);
        return 1;
    }
    config_dump(&cfg);

    /* Resolve which model to use */
    const char *model = config_resolve_model(&cfg, model_arg);

    /* ── Step 2: Initialize subsystems ─────────────────────────── */
    http_init();
    signal(SIGINT, sigint_handler);

    /* ── Step 3: Initialize conversation with system prompt ────── */
    conversation_t conv;
    conversation_init(&conv, system_prompt);

    print_banner(model);

    /* ── Step 4: Main interaction loop ─────────────────────────── */
    while (g_running) {
        /* 4a. Read user input */
        char *input = read_input();
        if (!input) {
            printf("\n[EOF - Goodbye!]\n");
            break;
        }

        /* Skip empty input */
        if (input[0] == '\0') {
            free(input);
            continue;
        }

        /* Check for quit command */
        if (strcmp(input, "quit") == 0 || strcmp(input, "exit") == 0) {
            free(input);
            printf("[Goodbye!]\n");
            break;
        }

        /* Check for /model command to switch models */
        if (strncmp(input, "/model ", 7) == 0) {
            const char *new_model = config_resolve_model(&cfg, input + 7);
            model = new_model;
            printf("[Switched to model: %s]\n\n", model);
            free(input);
            continue;
        }

        /* 4b. Add user message to conversation */
        conversation_add(&conv, ROLE_USER, input);
        free(input);

        /* 4c. Serialize conversation to JSON */
        char *messages_json = conversation_to_json(&conv);

        /* 4d. Stream the response */
        printf("\033[1;32mAssistant > \033[0m");
        fflush(stdout);

        response_buf_t resp;
        response_init(&resp);

        int rc = http_stream_chat(cfg.base_url, cfg.api_key,
                                  model, messages_json,
                                  on_token, &resp);

        free(messages_json);

        if (rc != 0) {
            fprintf(stderr, "\n[Error: request failed]\n");
            response_free(&resp);
            continue;
        }

        /* 4e. Add assistant reply to conversation history */
        if (resp.len > 0) {
            conversation_add(&conv, ROLE_ASSISTANT, resp.buf);
        }

        response_free(&resp);
        printf("\n");
    }

    /* ── Cleanup ───────────────────────────────────────────────── */
    conversation_free(&conv);
    http_cleanup();

    return 0;
}
