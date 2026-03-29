# ═══════════════════════════════════════════════════════════════
#  Makefile for mini_agent
# ═══════════════════════════════════════════════════════════════
#
#  Build targets:
#    make          - Build the agent binary
#    make run      - Build and run
#    make clean    - Remove build artifacts
#    make debug    - Build with debug symbols
#
#  Dependencies:
#    - C compiler (gcc or clang)
#    - libcurl (brew install curl / apt install libcurl4-openssl-dev)
#
# ═══════════════════════════════════════════════════════════════

CC       = cc
CFLAGS   = -Wall -Wextra -Wpedantic -std=c11
LDFLAGS  = -lcurl

# Source files
SRCS     = agent.c config.c message.c http_client.c json_parser.c
OBJS     = $(SRCS:.c=.o)
TARGET   = mini_agent

# Config file path (relative to build dir)
CONFIG   = ../HAI_WOA.json

# ── Default target ─────────────────────────────────────────────
all: $(TARGET)
	@echo ""
	@echo "  ✅ Build successful: ./$(TARGET)"
	@echo "  Run with: ./$(TARGET) -c $(CONFIG)"
	@echo ""

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# ── Run the agent ──────────────────────────────────────────────
run: $(TARGET)
	@./$(TARGET) -c $(CONFIG)

# ── Debug build ────────────────────────────────────────────────
debug: CFLAGS += -g -O0 -DDEBUG
debug: clean $(TARGET)
	@echo "  🐛 Debug build ready. Use: lldb ./$(TARGET)"

# ── Clean ──────────────────────────────────────────────────────
clean:
	rm -f $(OBJS) $(TARGET)
	@echo "  🧹 Cleaned."

# ── Header dependencies ───────────────────────────────────────
agent.o:       agent.c config.h message.h http_client.h
config.o:      config.c config.h json_parser.h
message.o:     message.c message.h
http_client.o: http_client.c http_client.h json_parser.h
json_parser.o: json_parser.c json_parser.h

.PHONY: all run debug clean
