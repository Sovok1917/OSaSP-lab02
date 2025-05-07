# Makefile for Parent/Child Process Interaction Demo

# Compiler and base flags
CC = gcc
# Base flags adhere to requirements: C11, pedantic, common warnings
BASE_CFLAGS = -std=c11 -pedantic -W -Wall -Wextra
# Optional allowed flags (uncomment if needed during development)
# BASE_CFLAGS += -Wno-unused-parameter -Wno-unused-variable

# Linker flags (can be empty)
LDFLAGS =

# Directories
SRC_DIR = src
BUILD_DIR = build
DEBUG_DIR = $(BUILD_DIR)/debug
RELEASE_DIR = $(BUILD_DIR)/release

# --- Configuration: Default to Debug ---
CURRENT_MODE = debug
CFLAGS = $(BASE_CFLAGS) -g3 -ggdb # Debugging symbols
OUT_DIR = $(DEBUG_DIR)

# --- Configuration: Adjust for Release Mode ---
# Override defaults if MODE=release is passed via command line (e.g., make MODE=release ...)
ifeq ($(MODE), release)
  CURRENT_MODE = release
  CFLAGS = $(BASE_CFLAGS) -O2 # Optimization level 2
  # CFLAGS += -Werror # Optionally treat warnings as errors in release
  OUT_DIR = $(RELEASE_DIR)
endif

# Source files for each program
PARENT_SRC = $(SRC_DIR)/parent.c
CHILD_SRC = $(SRC_DIR)/child.c

# Object files (paths automatically use the correct OUT_DIR)
PARENT_OBJ = $(OUT_DIR)/parent.o
CHILD_OBJ = $(OUT_DIR)/child.o

# Executables (paths automatically use the correct OUT_DIR)
PARENT_PROG = $(OUT_DIR)/parent
CHILD_PROG = $(OUT_DIR)/child

# Environment variable filter file path (automatically uses the correct OUT_DIR)
# This file lists the env vars the child should inherit.
ENV_FILTER_FILE = $(OUT_DIR)/env_vars.txt

# Name of the environment variable used to pass the filter file path to the child
ENV_VAR_FILTER_FILE_NAME = CHILD_ENV_FILTER_FILE

# Phony targets (targets that don't represent files)
.PHONY: all clean run run-release debug-build release-build help

# Default target: build debug version
all: debug-build

# Help target - UPDATED DESCRIPTIONS for run targets
help:
	@echo "Makefile Usage:"
	@echo "  make               Build debug version (default, same as make debug-build)"
	@echo "  make debug-build   Build debug version into $(DEBUG_DIR)"
	@echo "  make release-build Build release version into $(RELEASE_DIR)"
	@echo "  make run           Build and run debug version (sets CHILD_PATH automatically)"
	@echo "  make run-release   Build and run release version (sets CHILD_PATH automatically)"
	@echo "  make clean         Remove all build artifacts"
	@echo "  make help          Show this help message"


# --- Build Targets ---

# Target to build the debug version
# Sets MODE=debug explicitly for dependencies
debug-build: MODE=debug
debug-build: $(ENV_FILTER_FILE) $(PARENT_PROG) $(CHILD_PROG)
	@echo "Debug build complete in $(DEBUG_DIR)"

# Target to build the release version
# Sets MODE=release explicitly for dependencies
release-build: MODE=release
release-build: $(ENV_FILTER_FILE) $(PARENT_PROG) $(CHILD_PROG)
	@echo "Release build complete in $(RELEASE_DIR)"


# --- File Creation Rules ---

# Rule to create the output directories before any compilation
$(shell mkdir -p $(DEBUG_DIR) $(RELEASE_DIR))

# Create the environment variable filter file in the correct OUT_DIR
$(ENV_FILTER_FILE):
	@echo "Creating environment variable filter file: $@"
	@echo "SHELL" > $@
	@echo "HOSTNAME" >> $@
	@echo "LOGNAME" >> $@
	@echo "HOME" >> $@
	@echo "LANG" >> $@
	@echo "TERM" >> $@
	@echo "USER" >> $@
	@echo "LC_COLLATE" >> $@
	@echo "PATH" >> $@
	# Add the variable name we use to pass the filter file path itself
	@echo "$(ENV_VAR_FILTER_FILE_NAME)" >> $@


# --- Compilation and Linking Rules ---

# Link parent object file to create the parent executable
$(PARENT_PROG): $(PARENT_OBJ)
	@echo "Linking $@..."
	@$(CC) $(CFLAGS) $(PARENT_OBJ) -o $@ $(LDFLAGS)

# Link child object file to create the child executable
$(CHILD_PROG): $(CHILD_OBJ)
	@echo "Linking $@..."
	@$(CC) $(CFLAGS) $(CHILD_OBJ) -o $@ $(LDFLAGS)

# Compile source files into object files (Pattern Rule)
$(OUT_DIR)/%.o: $(SRC_DIR)/%.c
	@echo "Compiling $< -> $@..."
	@$(CC) $(CFLAGS) -c $< -o $@


# --- Execution Targets --- MODIFIED

# Run the debug version (depends on debug-build, sets CHILD_PATH using env)
run: debug-build
	@echo "Running DEBUG version $(PARENT_PROG) with filter file $(ENV_FILTER_FILE)..."
	@echo " Setting CHILD_PATH='$(abspath $(DEBUG_DIR))' for execution."
	@# Use env to correctly handle potential spaces in the path
	@env CHILD_PATH='$(abspath $(DEBUG_DIR))' $(PARENT_PROG) $(ENV_FILTER_FILE)

# Run the release version (depends on release-build, sets CHILD_PATH using env)
run-release: release-build
	@echo "Running RELEASE version $(PARENT_PROG) with filter file $(ENV_FILTER_FILE)..."
	@echo " Setting CHILD_PATH='$(abspath $(RELEASE_DIR))' for execution."
	@# Use env to correctly handle potential spaces in the path
	@env CHILD_PATH='$(abspath $(RELEASE_DIR))' $(PARENT_PROG) $(ENV_FILTER_FILE)

# --- Clean Target ---

# Clean up all build artifacts
clean:
	@echo "Cleaning build directories..."
	@rm -rf $(BUILD_DIR)
