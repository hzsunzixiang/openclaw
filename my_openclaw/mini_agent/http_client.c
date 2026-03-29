/*
 * http_client.c - HTTP client using libcurl for SSE streaming
 *
 * This is the network layer of our mini agent. It mirrors openclaw's
 * approach to calling OpenAI-compatible endpoints:
 *
 *   POST {base_url}/chat/completions
 *   Authorization: Bearer {api_key}
 *   Content-Type: application/json
 *   Body: { "model": "...", "stream": true, "messages": [...] }
 *
 * The response is Server-Sent Events (SSE):
 *   data: {"choices":[{"delta":{"content":"Hello"}}]}
 *   data: [DONE]
 *
 * We parse each SSE line in the write callback and extract the
 * content delta, forwarding it to the user's token callback.
 */
#include "http_client.h"
#include "json_parser.h"
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── SSE stream parsing context ────────────────────────────────── */

typedef struct {
    stream_token_cb callback;
    void           *userdata;
    char           *line_buf;    /* accumulates partial lines */
    size_t          line_len;
    size_t          line_cap;
} sse_ctx_t;

static void sse_ctx_init(sse_ctx_t *ctx, stream_token_cb cb, void *ud)
{
    ctx->callback = cb;
    ctx->userdata = ud;
    ctx->line_cap = 1024;
    ctx->line_buf = malloc(ctx->line_cap);
    ctx->line_len = 0;
}

static void sse_ctx_free(sse_ctx_t *ctx)
{
    free(ctx->line_buf);
}

/*
 * Process one complete SSE line.
 * Lines look like:
 *   data: {"id":"...","choices":[{"delta":{"content":"token"}}]}
 *   data: [DONE]
 */
static void process_sse_line(sse_ctx_t *ctx, const char *line)
{
    /* Skip empty lines and comments */
    if (line[0] == '\0' || line[0] == ':')
        return;

    /* We only care about "data: " prefixed lines */
    if (strncmp(line, "data: ", 6) != 0)
        return;

    const char *payload = line + 6;

    /* Check for stream termination */
    if (strcmp(payload, "[DONE]") == 0) {
        ctx->callback("", 0, 1, ctx->userdata);
        return;
    }

    /* Parse the JSON chunk */
    json_value_t *root = json_parse(payload);
    if (!root) return;

    /*
     * Navigate: choices[0].delta.content
     * This is the standard OpenAI streaming response format.
     */
    json_value_t *choices = json_object_get(root, "choices");
    if (choices && choices->type == JSON_ARRAY && choices->u.array.count > 0) {
        json_value_t *first = choices->u.array.items[0];
        json_value_t *delta = json_object_get(first, "delta");
        if (delta) {
            const char *content = json_get_string(delta, "content");
            if (content && content[0] != '\0') {
                ctx->callback(content, strlen(content), 0, ctx->userdata);
            }
        }
    }

    json_free(root);
}

/* ── libcurl write callback ────────────────────────────────────── */

static size_t write_callback(char *ptr, size_t size, size_t nmemb,
                             void *userdata)
{
    size_t total = size * nmemb;
    sse_ctx_t *ctx = (sse_ctx_t *)userdata;

    for (size_t i = 0; i < total; i++) {
        char c = ptr[i];
        if (c == '\n') {
            /* End of line - process it */
            ctx->line_buf[ctx->line_len] = '\0';
            /* Strip trailing \r */
            if (ctx->line_len > 0 && ctx->line_buf[ctx->line_len - 1] == '\r')
                ctx->line_buf[--ctx->line_len] = '\0';
            process_sse_line(ctx, ctx->line_buf);
            ctx->line_len = 0;
        } else {
            /* Accumulate character */
            if (ctx->line_len + 1 >= ctx->line_cap) {
                ctx->line_cap *= 2;
                ctx->line_buf = realloc(ctx->line_buf, ctx->line_cap);
            }
            ctx->line_buf[ctx->line_len++] = c;
        }
    }

    return total;
}

/* ── Public API ────────────────────────────────────────────────── */

void http_init(void)
{
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

void http_cleanup(void)
{
    curl_global_cleanup();
}

int http_stream_chat(const char *base_url,
                     const char *api_key,
                     const char *model,
                     const char *messages_json,
                     stream_token_cb callback,
                     void *userdata)
{
    CURL *curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "[http] curl_easy_init failed\n");
        return -1;
    }

    /* ── Build URL: {base_url}/chat/completions ────────────────── */
    char url[512];
    /* Strip trailing slash from base_url */
    size_t blen = strlen(base_url);
    while (blen > 0 && base_url[blen - 1] == '/')
        blen--;

    /*
     * Auto-upgrade http:// to https:// for security.
     * Most LLM API providers require HTTPS; plain HTTP often gets
     * a 404 from the gateway even though the route exists.
     */
    char upgraded_base[512];
    if (blen >= 7 && strncmp(base_url, "http://", 7) == 0) {
        snprintf(upgraded_base, sizeof(upgraded_base), "https://%.*s",
                 (int)(blen - 7), base_url + 7);
        blen = strlen(upgraded_base);
        snprintf(url, sizeof(url), "%s/chat/completions", upgraded_base);
    } else {
        snprintf(url, sizeof(url), "%.*s/chat/completions",
                 (int)blen, base_url);
    }

    fprintf(stderr, "[http] POST %s  model=%s\n", url, model);

    /* ── Build request body ────────────────────────────────────── */
    /*
     * {
     *   "model": "<model>",
     *   "stream": true,
     *   "messages": [ ... ]
     * }
     */
    size_t body_cap = strlen(messages_json) + strlen(model) + 128;
    char *body = malloc(body_cap);
    snprintf(body, body_cap,
             "{\"model\":\"%s\",\"stream\":true,\"messages\":%s}",
             model, messages_json);

    /* ── Set headers ───────────────────────────────────────────── */
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    char auth_header[320];
    snprintf(auth_header, sizeof(auth_header),
             "Authorization: Bearer %s", api_key);
    headers = curl_slist_append(headers, auth_header);

    /* ── Configure curl ────────────────────────────────────────── */
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);

    /* SSE streaming: use our write callback */
    sse_ctx_t ctx;
    sse_ctx_init(&ctx, callback, userdata);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);

    /* ── Execute ───────────────────────────────────────────────── */
    CURLcode res = curl_easy_perform(curl);

    int ret = 0;
    if (res != CURLE_OK) {
        fprintf(stderr, "[http] curl error: %s\n",
                curl_easy_strerror(res));
        ret = -1;
    } else {
        /* Check HTTP status */
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        if (http_code != 200) {
            fprintf(stderr, "[http] HTTP %ld from %s\n", http_code, url);
            ret = -1;
        }
    }

    /* ── Cleanup ───────────────────────────────────────────────── */
    sse_ctx_free(&ctx);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    free(body);

    return ret;
}
