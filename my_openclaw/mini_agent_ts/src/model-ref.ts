/**
 * model-ref.ts — Model reference parsing and normalization
 *
 * Borrowed from openclaw's src/agents/model-selection.ts
 *
 * In openclaw, model identifiers use a "provider/model" format internally
 * (e.g. "openai/DeepSeek-V3-0324"). The provider prefix is used for
 * internal routing and auth-profile resolution, but must be stripped
 * before sending to the actual API endpoint.
 *
 * Key concepts from openclaw:
 *   - ModelRef: { provider: string; model: string }
 *   - modelKey(): canonical key for deduplication
 *   - parseModelRef(): split "provider/model" string
 *   - normalizeModelRef(): normalize provider + model ids
 */

// ── Types ─────────────────────────────────────────────────────────

export interface ModelRef {
  provider: string;
  model: string;
}

// ── modelKey ──────────────────────────────────────────────────────
// Mirrors openclaw's modelKey() — produces a canonical "provider/model"
// string for use as a map key or display label.

export function modelKey(provider: string, model: string): string {
  const providerId = provider.trim();
  const modelId = model.trim();
  if (!providerId) return modelId;
  if (!modelId) return providerId;
  // Avoid double-prefixing: "openai/openai/gpt-4" → "openai/gpt-4"
  if (modelId.toLowerCase().startsWith(`${providerId.toLowerCase()}/`)) {
    return modelId;
  }
  return `${providerId}/${modelId}`;
}

// ── parseModelRef ─────────────────────────────────────────────────
// Mirrors openclaw's parseModelRef() — splits "provider/model" into
// a ModelRef. If no slash is present, uses the defaultProvider.

export function parseModelRef(
  raw: string,
  defaultProvider: string,
): ModelRef | null {
  const trimmed = raw.trim();
  if (!trimmed) return null;

  const slash = trimmed.indexOf("/");
  if (slash === -1) {
    // No provider prefix — use default
    return { provider: defaultProvider, model: trimmed };
  }

  const provider = trimmed.slice(0, slash).trim();
  const model = trimmed.slice(slash + 1).trim();
  if (!provider || !model) return null;

  return { provider, model };
}

// ── stripProviderPrefix ───────────────────────────────────────────
// The haihub API (and most OpenAI-compatible APIs) don't recognize
// the "openai/" prefix that openclaw uses for internal routing.
// This strips it before sending to the API.

export function stripProviderPrefix(modelId: string): string {
  const slash = modelId.indexOf("/");
  return slash !== -1 ? modelId.slice(slash + 1) : modelId;
}
