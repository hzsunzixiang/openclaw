# ──────────────────────────────────────────────────────────────────
# Makefile for mini_agent_ts
#
# A minimal TypeScript agent inspired by openclaw source code.
# Uses Node.js native fetch (>=18) — zero runtime dependencies.
#
# Targets:
#   make          — install deps + compile TypeScript
#   make build    — compile TypeScript only
#   make run      — build and run the agent
#   make dev      — build and run with custom model
#   make clean    — remove build artifacts
#   make install  — install npm dependencies
# ──────────────────────────────────────────────────────────────────

# Configuration
CONFIG   ?= ../HAI_WOA.json
MODEL    ?=
NODE     := node
NPM      := npm
TSC      := npx tsc

# Default target
.PHONY: all
all: install build
	@echo ""
	@echo "  ✅ Build successful: dist/agent.js"
	@echo "  Run with: make run"
	@echo "  Or:       $(NODE) dist/agent.js -c $(CONFIG)"

# Install npm dependencies
.PHONY: install
install: node_modules

node_modules: package.json
	@echo "  📦 Installing dependencies..."
	@$(NPM) install --silent
	@touch node_modules

# Compile TypeScript
.PHONY: build
build: node_modules
	@echo "  🔨 Compiling TypeScript..."
	@$(TSC)
	@echo "  ✅ Compiled to dist/"

# Run the agent
.PHONY: run
run: build
	@echo ""
	@$(NODE) dist/agent.js -c $(CONFIG) $(if $(MODEL),-m $(MODEL),)

# Run with a specific model alias (e.g., make dev MODEL=qwen)
.PHONY: dev
dev: build
	@echo ""
	@$(NODE) dist/agent.js -c $(CONFIG) $(if $(MODEL),-m $(MODEL),)

# Clean build artifacts
.PHONY: clean
clean:
	@rm -rf dist node_modules
	@echo "  🧹 Cleaned dist/ and node_modules/"

# Clean only build output (keep node_modules)
.PHONY: distclean
distclean:
	@rm -rf dist
	@echo "  🧹 Cleaned dist/"

# Show help
.PHONY: help
help:
	@echo ""
	@echo "  mini_agent_ts — Minimal TypeScript Agent (openclaw-inspired)"
	@echo ""
	@echo "  Usage:"
	@echo "    make              Build the project (install + compile)"
	@echo "    make run          Build and run the agent"
	@echo "    make run MODEL=ds Run with a specific model alias"
	@echo "    make clean        Remove all build artifacts"
	@echo "    make distclean    Remove only compiled output"
	@echo "    make help         Show this help"
	@echo ""
	@echo "  Environment:"
	@echo "    CONFIG=<path>     Config file path (default: ../HAI_WOA.json)"
	@echo "    MODEL=<name>      Model name or alias (default: from config)"
	@echo ""
