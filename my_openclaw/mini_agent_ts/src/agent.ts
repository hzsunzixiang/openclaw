/**
 * agent.ts — Mini Agent main entry point
 *
 * Borrowed from openclaw's agent execution pipeline:
 *   - src/agents/agent-command.ts           (agentCommand — the main orchestrator)
 *   - src/agents/command/attempt-execution.ts (runAgentAttempt — LLM call)
 *   - src/agents/model-selection.ts          (model resolution)
 *   - src/agents/defaults.ts                 (DEFAULT_PROVIDER, DEFAULT_MODEL)
 *
 * openclaw's full pipeline (simplified):
 *   ┌─────────────────────────────────────────────────────────────────┐
 *   │ 1. agentCommand()                                              │
 *   │    ├─ loadConfig() → resolve secrets → setRuntimeConfigSnapshot│
 *   │    ├─ resolveSession() → find/create session                   │
 *   │    ├─ resolveConfiguredModelRef() → pick model + provider      │
 *   │    ├─ buildAllowedModelSet() → validate model allowlist        │
 *   │    ├─ resolveSessionTranscriptFile() → session file path       │
 *   │    └─ runWithModelFallback()                                   │
 *   │         └─ runAgentAttempt()                                   │
 *   │              ├─ isCliProvider? → runCliAgent()                  │
 *   │              └─ else → runEmbeddedPiAgent()                    │
 *   │                   └─ @mariozechner/pi-ai (Anthropic/OpenAI SDK)│
 *   │                        └─ fetch POST /chat/completions         │
 *   │                             └─ SSE stream → token callbacks    │
 *   │ 2. deliverAgentCommandResult()                                 │
 *   │    └─ send reply via channel (Slack/Discord/Telegram/etc.)     │
 *   └─────────────────────────────────────────────────────────────────┘
 *
 * Our mini version:
 *   ┌─────────────────────────────────────────────────────────────────┐
 *   │ 1. loadConfig(HAI_WOA.json)                                    │
 *   │ 2. resolveModel(alias) → API model name                       │
 *   │ 3. Read user input from stdin                                  │
 *   │ 4. MessageHistory.addUser(input)                               │
 *   │ 5. streamChat() → POST /chat/completions (SSE)                │
 *   │ 6. Token callback → process.stdout.write()                    │
 *   │ 7. MessageHistory.addAssistant(fullResponse)                   │
 *   │ 8. Loop back to step 3                                        │
 *   └─────────────────────────────────────────────────────────────────┘
 */

import { createInterface } from "node:readline";
import { loadConfig, resolveModel, dumpConfig, type AgentConfig } from "./config.js";
import { MessageHistory } from "./message.js";
import { streamChat } from "./http-client.js";

// ── CLI argument parsing ──────────────────────────────────────────
// Minimal arg parsing — mirrors openclaw's commander-based CLI
// but stripped to essentials.

interface CliArgs {
  configPath: string;
  model?: string;
}

function parseArgs(): CliArgs {
  const args = process.argv.slice(2);
  let configPath = "../HAI_WOA.json";
  let model: string | undefined;

  for (let i = 0; i < args.length; i++) {
    switch (args[i]) {
      case "-c":
      case "--config":
        configPath = args[++i] ?? configPath;
        break;
      case "-m":
      case "--model":
        model = args[++i];
        break;
      case "-h":
      case "--help":
        console.log(`
Usage: mini-agent-ts [options]

Options:
  -c, --config <path>   Path to HAI_WOA.json config file (default: ../HAI_WOA.json)
  -m, --model <name>    Model name or alias (default: from config)
  -h, --help            Show this help message

Commands (in chat):
  /model <name>         Switch to a different model
  /clear                Clear conversation history
  /history              Show conversation history
  quit                  Exit the agent
`);
        process.exit(0);
    }
  }

  return { configPath, model };
}

// ── REPL loop ─────────────────────────────────────────────────────
// Interactive read-eval-print loop. Mirrors openclaw's agent command
// flow but without channels — reads directly from stdin.

async function runAgent(): Promise<void> {
  // ── Step 1: Load configuration ──────────────────────────────
  // Mirrors: loadConfig() → resolveCommandSecretRefsViaGateway()
  const cliArgs = parseArgs();
  let cfg: AgentConfig;
  try {
    cfg = loadConfig(cliArgs.configPath);
  } catch (err) {
    console.error(`[agent] Failed to load config: ${err}`);
    process.exit(1);
  }
  dumpConfig(cfg);

  // ── Step 2: Resolve model ───────────────────────────────────
  // Mirrors: resolveConfiguredModelRef() → normalizeModelRef()
  let currentModel = resolveModel(cfg, cliArgs.model);

  // ── Step 3: Initialize message history ──────────────────────
  // Mirrors: SessionManager.open(sessionFile)
  const systemPrompt =
    "You are a helpful assistant. Be concise and clear in your responses.";
  let history = new MessageHistory(systemPrompt);

  // ── Banner ──────────────────────────────────────────────────
  console.log("");
  console.log("  ╔═══════════════════════════════════════════╗");
  console.log("  ║   🤖 Mini Agent TS (openclaw-inspired)    ║");
  console.log("  ╠═══════════════════════════════════════════╣");
  console.log(`  ║  Model: ${currentModel.padEnd(32)} ║`);
  console.log("  ║  Type 'quit' or Ctrl-D to exit            ║");
  console.log("  ║  Type '/model <name>' to switch model     ║");
  console.log("  ║  Type '/clear' to reset conversation      ║");
  console.log("  ║  Type '/history' to show message history  ║");
  console.log("  ╚═══════════════════════════════════════════╝");
  console.log("");

  // ── Step 4: REPL loop ───────────────────────────────────────
  // Mirrors: the agent command's message → runAgentAttempt() → deliver cycle
  const rl = createInterface({
    input: process.stdin,
    output: process.stdout,
    prompt: "You > ",
  });

  // Line queue for proper async handling.
  // In pipe mode, readline fires all "line" events synchronously before
  // any async handler can process them. We buffer lines in a queue and
  // resolve them one at a time via askLine().
  const lineQueue: string[] = [];
  let lineResolve: ((line: string | null) => void) | null = null;
  let eofReached = false;

  rl.on("line", (line) => {
    if (lineResolve) {
      const resolve = lineResolve;
      lineResolve = null;
      resolve(line);
    } else {
      lineQueue.push(line);
    }
  });

  rl.on("close", () => {
    eofReached = true;
    if (lineResolve) {
      const resolve = lineResolve;
      lineResolve = null;
      resolve(null);
    }
  });

  const askLine = (): Promise<string | null> => {
    // Drain buffered lines first
    if (lineQueue.length > 0) {
      return Promise.resolve(lineQueue.shift()!);
    }
    if (eofReached) {
      return Promise.resolve(null);
    }
    return new Promise((resolve) => {
      lineResolve = resolve;
      rl.prompt();
    });
  };

  let running = true;
  while (running) {
    const line = await askLine();
    if (line === null) {
      // EOF (Ctrl-D or pipe exhausted)
      console.log("[Goodbye!]");
      break;
    }

    const input = line.trim();

    // ── Handle empty input ──────────────────────────────────
    if (!input) {
      continue;
    }

    // ── Handle quit ─────────────────────────────────────────
    if (input.toLowerCase() === "quit" || input.toLowerCase() === "exit") {
      console.log("[Goodbye!]");
      running = false;
      continue;
    }

    // ── Handle /model command ───────────────────────────────
    // Mirrors: openclaw's model override via session store
    if (input.startsWith("/model ")) {
      const modelArg = input.slice(7).trim();
      if (!modelArg) {
        console.log(`[Current model: ${currentModel}]`);
      } else {
        currentModel = resolveModel(cfg, modelArg);
      console.log(`[Switched to model: ${currentModel}]`);
      }
      console.log("");
      continue;
    }

    // ── Handle /clear command ───────────────────────────────
    if (input === "/clear") {
      history = new MessageHistory(systemPrompt);
      console.log("[Conversation cleared]");
      console.log("");
      continue;
    }

    // ── Handle /history command ─────────────────────────────
    if (input === "/history") {
      const msgs = history.getMessages();
      console.log(`\n[Message history: ${msgs.length} messages]`);
      for (const msg of msgs) {
        const preview =
          msg.content.length > 80
            ? msg.content.slice(0, 80) + "..."
            : msg.content;
        console.log(`  [${msg.role}] ${preview}`);
      }
      console.log("");
      continue;
    }

    // ── Step 5: Add user message to history    // Mirrors: prependInternalEventContext(message) → SessionManager.appendMessage()
    history.addUser(input);

    // ── Step 6: Call LLM API ────────────────────────────────
    // Mirrors: runAgentAttempt() → runEmbeddedPiAgent() → pi-ai fetch
    process.stdout.write("Assistant > ");

    let fullResponse = "";
    try {
      await streamChat({
        baseUrl: cfg.baseUrl,
        apiKey: cfg.apiKey,
        model: currentModel,
        messages: history.getMessages(),
        onToken: (token: string, done: boolean) => {
          if (done) {
            // Stream finished — add newline
            process.stdout.write("\n");
            return;
          }
          // ── Step 7: Stream tokens to stdout ───────────────
          // Mirrors: emitAcpAssistantDelta() → channel delivery
          fullResponse += token;
          process.stdout.write(token);
        },
      });

      // ── Step 8: Save assistant response to history ────────
      // Mirrors: SessionManager.appendMessage({ role: "assistant" })
      if (fullResponse) {
        history.addAssistant(fullResponse);
      }
    } catch (err) {
      console.error(`\n[Error: ${err instanceof Error ? err.message : String(err)}]`);
    }

    console.log("");
  }

  rl.close();
}

// ── Entry point ───────────────────────────────────────────────────

runAgent().catch((err) => {
  console.error(`[fatal] ${err}`);
  process.exit(1);
});
