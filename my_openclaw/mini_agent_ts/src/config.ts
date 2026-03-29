/**
 * config.ts — Load and parse HAI_WOA.json configuration
 *
 * Borrowed from openclaw's config loading pipeline:
 *   - src/config/config.ts          (loadConfig, readConfigFileSnapshotForWrite)
 *   - src/agents/model-selection.ts  (resolveConfiguredModelRef, buildModelAliasIndex)
 *   - src/agents/defaults.ts         (DEFAULT_PROVIDER, DEFAULT_MODEL)
 *
 * In openclaw, configuration flows through multiple layers:
 *   1. JSON/YAML config file → raw config object
 *   2. Secret refs resolved via gateway
 *   3. Model selection: default model, allowlist, aliases, fallbacks
 *   4. Provider auth: API keys, base URLs, auth profiles
 *
 * Our mini version simplifies this to:
 *   1. Read HAI_WOA.json
 *   2. Extract env (API key + base URL)
 *   3. Extract model aliases
 *   4. Resolve model by alias or default
 */

import { readFileSync } from "node:fs";
import { type ModelRef, parseModelRef, stripProviderPrefix } from "./model-ref.js";

// ── Types ─────────────────────────────────────────────────────────

export interface ModelEntry {
  alias: string;
  id: string;       // raw id from config, e.g. "openai/DeepSeek-V3-0324"
  apiModel: string;  // stripped for API use, e.g. "DeepSeek-V3-0324"
}

export interface AgentConfig {
  /** Raw default model from config (may include provider prefix) */
  defaultModelRaw: string;
  /** Default model stripped for API use */
  defaultModel: string;
  /** Default provider extracted from model id */
  defaultProvider: string;
  /** API key from env.OPENAI_API_KEY */
  apiKey: string;
  /** Base URL from env.OPENAI_BASE_URL */
  baseUrl: string;
  /** Whether to use /chat/completions endpoint */
  useChatCompletions: boolean;
  /** Model alias entries */
  models: ModelEntry[];
}

// ── Raw JSON shape ────────────────────────────────────────────────

interface RawConfig {
  model?: string;
  models?: Record<string, { id?: string }>;
  env?: {
    OPENAI_API_KEY?: string;
    OPENAI_BASE_URL?: string;
  };
  openai?: {
    useChatCompletions?: boolean;
  };
}

// ── loadConfig ────────────────────────────────────────────────────
// Mirrors openclaw's loadConfig() → resolveConfiguredModelRef() chain.

export function loadConfig(filepath: string): AgentConfig {
  const text = readFileSync(filepath, "utf-8");
  const raw: RawConfig = JSON.parse(text);

  // Extract env
  const apiKey = raw.env?.OPENAI_API_KEY ?? "";
  const baseUrl = raw.env?.OPENAI_BASE_URL ?? "";

  // Extract default model — openclaw uses parseModelRef() to split
  // "provider/model" format. We do the same.
  const defaultModelRaw = raw.model ?? "";
  const defaultRef = parseModelRef(defaultModelRaw, "openai");
  const defaultProvider = defaultRef?.provider ?? "openai";
  const defaultModel = stripProviderPrefix(defaultModelRaw);

  // Extract model aliases — mirrors openclaw's buildModelAliasIndex()
  const models: ModelEntry[] = [];
  if (raw.models) {
    for (const [alias, entry] of Object.entries(raw.models)) {
      const id = entry?.id;
      if (id) {
        models.push({
          alias,
          id,
          apiModel: stripProviderPrefix(id),
        });
      }
    }
  }

  // openai.useChatCompletions flag
  const useChatCompletions = raw.openai?.useChatCompletions ?? true;

  return {
    defaultModelRaw,
    defaultModel,
    defaultProvider,
    apiKey,
    baseUrl,
    useChatCompletions,
    models,
  };
}

// ── resolveModel ──────────────────────────────────────────────────
// Mirrors openclaw's resolveModelRefFromString() + alias lookup.
// Returns the API-ready model name (provider prefix stripped).

export function resolveModel(cfg: AgentConfig, nameOrAlias?: string): string {
  if (!nameOrAlias || nameOrAlias.trim() === "") {
    return cfg.defaultModel;
  }

  // Check aliases first (case-insensitive, like openclaw's normalizeAliasKey)
  const normalized = nameOrAlias.trim().toLowerCase();
  for (const entry of cfg.models) {
    if (entry.alias.toLowerCase() === normalized) {
      return entry.apiModel;
    }
  }

  // Not an alias — strip provider prefix if present
  return stripProviderPrefix(nameOrAlias.trim());
}

// ── dumpConfig ────────────────────────────────────────────────────

export function dumpConfig(cfg: AgentConfig): void {
  const keyPreview = cfg.apiKey.length > 8
    ? `${cfg.apiKey.slice(0, 8)}...(hidden)`
    : "(not set)";

  console.log("╔══════════════════════════════════════════════╗");
  console.log("║         HAI_WOA Configuration Loaded        ║");
  console.log("╠══════════════════════════════════════════════╣");
  console.log(`║ Default model : ${cfg.defaultModel.padEnd(27)} ║`);
  console.log(`║ Provider      : ${cfg.defaultProvider.padEnd(27)} ║`);
  console.log(`║ Base URL      : ${cfg.baseUrl.padEnd(27)} ║`);
  console.log(`║ API Key       : ${keyPreview.padEnd(27)} ║`);
  console.log(`║ ChatCompletions: ${(cfg.useChatCompletions ? "enabled" : "disabled").padEnd(26)} ║`);
  console.log(`║ Model aliases (${cfg.models.length}):${" ".repeat(24)} ║`);
  for (const m of cfg.models) {
    console.log(`║   ${m.alias.padEnd(6)} -> ${m.apiModel.padEnd(31)} ║`);
  }
  console.log("╚══════════════════════════════════════════════╝");
}
