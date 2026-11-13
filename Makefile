# ==========================================
# Root Makefile for Distributed Audio Processing
# ==========================================

# Directories
BIN_DIR = bin
SERVER_DIR = src/server

.PHONY: all clean server

# Default target: compile everything essential
all: server

# Build the server binary
server:
		@mkdir -p $(BIN_DIR)
		$(MAKE) -C $(SERVER_DIR)

# Run the server/cluster component specifically
run-server: server
	$(MAKE) -C $(SERVER_DIR) run

# Clean all generated binaries and objects across modules
clean:
		@echo "Cleaning binaries and objects..."
		rm -rf $(BIN_DIR)/*
		$(MAKE) -C $(SERVER_DIR) clean
