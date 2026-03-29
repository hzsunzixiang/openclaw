/*
 * http_client.h - Minimal HTTP client for OpenAI-compatible API
 *
 * Uses libcurl to POST to /v1/chat/completions with SSE streaming.
 * Mirrors openclaw's approach: build the request body, stream the
 * response, and parse SSE "data:" lines in real time.
 */
#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include <stddef.h>

/*
 * Callback invoked for each content token received from the stream.
 * - token: the text fragment (not null-terminated in general, use len)
 * - len:   byte length of token
 * - done:  1 if this is the final callback (stream finished)
 * - userdata: opaque pointer passed through
 */
typedef void (*stream_token_cb)(const char *token, size_t len,
                                int done, void *userdata);

/*
 * Send a chat completion request with streaming.
 *
 * Parameters:
 *   base_url  - e.g. "http://api.haihub.cn/v1"
 *   api_key   - Bearer token
 *   model     - model id string
 *   messages_json - pre-built JSON array string of messages
 *   callback  - called for each streamed token
 *   userdata  - passed to callback
 *
 * Returns 0 on success, -1 on error.
 */
int http_stream_chat(const char *base_url,
                     const char *api_key,
                     const char *model,
                     const char *messages_json,
                     stream_token_cb callback,
                     void *userdata);

/* Initialize / cleanup the HTTP subsystem (call once) */
void http_init(void);
void http_cleanup(void);

#endif /* HTTP_CLIENT_H */
