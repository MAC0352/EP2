CC      ?= cc
CFLAGS  ?= -std=c11 -O2 -Wall -Wextra -Wpedantic -D_POSIX_C_SOURCE=200809L
CPPFLAGS = -Iinclude -Isrc
LDFLAGS ?=
LDLIBS  ?= -lpthread

BIN_DIR := bin
OBJ_DIR := obj

CORE_SRC := src/util.c src/protocol.c src/mib.c
CORE_OBJ := $(CORE_SRC:src/%.c=$(OBJ_DIR)/%.o)

AGENT_SRC   := $(wildcard src/agent/*.c)
AGENT_OBJ   := $(AGENT_SRC:src/%.c=$(OBJ_DIR)/%.o)
MANAGER_SRC := $(wildcard src/manager/*.c)
MANAGER_OBJ := $(MANAGER_SRC:src/%.c=$(OBJ_DIR)/%.o)

.PHONY: all core agent manager clean

all: core

core: $(CORE_OBJ)

# Agent / manager targets become live once their source files exist.
ifneq ($(strip $(AGENT_SRC)),)
all: $(BIN_DIR)/agent
$(BIN_DIR)/agent: $(CORE_OBJ) $(AGENT_OBJ) | $(BIN_DIR)
	$(CC) $(LDFLAGS) $^ -o $@ $(LDLIBS)
endif

ifneq ($(strip $(MANAGER_SRC)),)
all: $(BIN_DIR)/manager
$(BIN_DIR)/manager: $(CORE_OBJ) $(MANAGER_OBJ) | $(BIN_DIR)
	$(CC) $(LDFLAGS) $^ -o $@ $(LDLIBS)
endif

TOOL_SRC := $(wildcard src/tools/*.c)
TOOL_BIN := $(TOOL_SRC:src/tools/%.c=$(BIN_DIR)/%)
ifneq ($(strip $(TOOL_SRC)),)
all: $(TOOL_BIN)
$(BIN_DIR)/%: src/tools/%.c $(CORE_OBJ) | $(BIN_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) $< $(CORE_OBJ) -o $@ $(LDLIBS)
endif

$(OBJ_DIR)/%.o: src/%.c | $(OBJ_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(BIN_DIR) $(OBJ_DIR):
	@mkdir -p $@

clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)
