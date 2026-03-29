/**
 * http-client.ts — HTTP client for OpenAI-compatible streaming API
 *
 * Borrowed from openclaw's API call patterns:
 *   - src/plugins/provider-zai-endpoint.ts  (URL construction: `${baseUrl}/chat/completions`)
 *   - src/agents/tools/image-tool.test.ts   (fetch + SSE pattern)
 *   - src/utils/fetch-timeout.ts            (fetchWithTimeout)
 *
 * In openclaw, the actual LLM API call happens deep inside the
 * @mariozechner/pi-ai library, but the URL construction and auth
 * header patterns are visible in the provider endpoint probing code.
 *
 * The SSE (Server-Sent Events) streaming protocol:
 *   - Response is a stream of lines
 *   - Lines starting with "data: " contain JSON payloads
 *   - "data: [DONE]" signals stream end
 *   - Each payload has: choices[0].delta.content (the token)
 *
 * Data flow:
 *   fetch(POST) → ReadableStream → TextDecoder → line split → SSE parse → token callback
 */

import type { ChatMessage } from "./message.js";

// ── Types ─────────────────────────────────────────────────────────

export type TokenCallback = (token: string, done: boolean) => void;

export interface StreamChatOptions {
  baseUrl: string;
  apiKey: string;
  model: string;
  messages: ChatMessage[];
  onToken: TokenCallback;
  timeoutMs?: number;
}

// ── URL construction ──────────────────────────────────────────────
// Mirrors openclaw's pattern: `${params.baseUrl}/chat/completions`
// with trailing-slash stripping and http→https upgrade.

function buildUrl(baseUrl: string): string {
  let url = baseUrl.replace(/\/+$/, "");

  // Auto-upgrade http:// to https:// — most LLM API providers require HTTPS.
  // Plain HTTP often gets a 404 from the gateway even though the route exists.
  if (url.startsWith("http://")) {
    url = "https://" + url.slice(7);
  }

  return `${url}/chat/completions`;
}

// ── SSE line parser ───────────────────────────────────────────────
// Parses Server-Sent Events format line by line.
// Each "data: {...}" line contains a JSON chunk with:
//   { choices: [{ delta: { content: "token" } }] }

function processSSELine(line: string, onToken: TokenCallback): void {
  // Skip empty lines and comments
  if (!line || line.startsWith(":")) return;

  // We only care about "data: " prefixed lines
  if (!line.startsWith("data: ")) return;

  const payload = line.slice(6);

  // Stream termination signal
  if (payload === "[DONE]") {
    onToken("", true);
    return;
  }

  // Parse JSON chunk — navigate: choices[0].delta.content
  try {
    const chunk = JSON.parse(payload) as {
      choices?: Array<{
        delta?: { content?: string; reasoning_content?: string };
        finish_reason?: string | null;
      }>;
    };

    const delta = chunk.choices?.[0]?.delta;
    if (delta?.content) {
      onToken(delta.content, false);
    }
    // Some models (DeepSeek) emit reasoning_content for chain-of-thought
    if (delta?.reasoning_content) {
      onToken(delta.reasoning_content, false);
    }
  } catch {
    // Malformed JSON — skip silently (mirrors openclaw's defensive parsing)
  }
}

// ── streamChat ────────────────────────────────────────────────────
// Main API call function. Uses Node.js native fetch (available since v18).
//
// Request body mirrors openclaw's format:
//   { "model": "...", "stream": true, "messages": [...] }
//
// Auth header: "Authorization: Bearer {api_key}"

export async function streamChat(opts: StreamChatOptions): Promise<void> {
  const url = buildUrl(opts.baseUrl);
  const timeoutMs = opts.timeoutMs ?? 120_000;

  // Build request body — same format as openclaw's OpenAI-compatible calls
  const body = JSON.stringify({
    model: opts.model,
    stream: true,
    messages: opts.messages,
  });

  process.stderr.write(`[http] POST ${url}  model=${opts.model}\n`);

  // Create abort controller for timeout
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), timeoutMs);

  try {
    const response = await fetch(url, {
      method: "POST",
      headers: {
        "Content-Type": "application/json",
        Authorization: `Bearer ${opts.apiKey}`,
      },
      body,
      signal: controller.signal,
    });

    if (!response.ok) {
      // Try to read error body for diagnostics
      let errorBody = "";
      try {
        errorBody = await response.text();
      } catch { /* ignore */ }
      throw new Error(
        `HTTP ${response.status} from ${url}${errorBody ? `: ${errorBody}` : ""}`,
      );
    }

    if (!response.body) {
      throw new Error("Response body is null — streaming not supported?");
    }

    // ── Stream processing ───────────────────────────────────────
    // Read the response as a stream of bytes, decode to text,
    // split into lines, and process each SSE line.
    //
    // This mirrors the C version's write_callback approach but uses
    // Node.js ReadableStream + TextDecoder.

    const reader = response.body.getReader();
    const decoder = new TextDecoder();
    let buffer = "";

    while (true) {
      const { done, value } = await reader.read();
      if (done) break;

      buffer += decoder.decode(value, { stream: true });

      // Split on newlines and process complete lines
      const lines = buffer.split("\n");
      // Keep the last (possibly incomplete) line in the buffer
      buffer = lines.pop() ?? "";

      for (const line of lines) {
        const trimmed = line.replace(/\r$/, "");
        processSSELine(trimmed, opts.onToken);
      }
    }

    // Process any remaining data in buffer
    if (buffer.trim()) {
      processSSELine(buffer.replace(/\r$/, ""), opts.onToken);
    }
  } finally {
    clearTimeout(timer);
  }
}
