/**
 * message.ts — Conversation message management
 *
 * Borrowed from openclaw's message handling:
 *   - @mariozechner/pi-coding-agent SessionManager (appendMessage)
 *   - src/agents/command/attempt-execution.ts (persistAcpTurnTranscript)
 *   - OpenAI chat completions message format
 *
 * In openclaw, messages flow through:
 *   1. User input → prependInternalEventContext() → body string
 *   2. SessionManager.appendMessage({ role: "user", content: body })
 *   3. LLM API call with full message history
 *   4. SessionManager.appendMessage({ role: "assistant", content: [...] })
 *   5. Transcript persisted to JSONL file
 *
 * Our mini version maintains an in-memory message array using the
 * standard OpenAI chat message format.
 */

// ── Types ─────────────────────────────────────────────────────────

export type MessageRole = "system" | "user" | "assistant";

export interface ChatMessage {
  role: MessageRole;
  content: string;
}

// ── MessageHistory ────────────────────────────────────────────────
// Simple in-memory conversation history, mirroring openclaw's
// SessionManager but without file persistence.

export class MessageHistory {
  private messages: ChatMessage[] = [];

  constructor(systemPrompt?: string) {
    if (systemPrompt) {
      this.messages.push({ role: "system", content: systemPrompt });
    }
  }

  /** Add a user message — mirrors SessionManager.appendMessage({ role: "user" }) */
  addUser(content: string): void {
    this.messages.push({ role: "user", content });
  }

  /** Add an assistant message — mirrors SessionManager.appendMessage({ role: "assistant" }) */
  addAssistant(content: string): void {
    this.messages.push({ role: "assistant", content });
  }

  /** Get all messages for API request */
  getMessages(): ChatMessage[] {
    return [...this.messages];
  }

  /** Serialize messages array to JSON string for API body */
  toJSON(): string {
    return JSON.stringify(this.messages);
  }

  /** Get message count (excluding system prompt) */
  get turnCount(): number {
    return this.messages.filter((m) => m.role !== "system").length;
  }

  /** Clear all messages (keep system prompt if any) */
  clear(): void {
    const system = this.messages.find((m) => m.role === "system");
    this.messages = system ? [system] : [];
  }
}
