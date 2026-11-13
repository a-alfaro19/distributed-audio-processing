# ==========================================
# Server / Cluster Makefile (OpenMPI)
# ==========================================

# MPI Compiler
CC = mpicc

# Compilation flags
CFLAGS = -Wall -Wextra -std=gnu99 -I../../include/server

# Binary output target (placed in the root bin directory)
TARGET = ../../bin/server

# Build route from root
BUILD_ROOT = ../../build/server

# Source files and objects
SRCS = $(wildcard *.c)
OBJS = $(patsubst %.c, $(BUILD_ROOT)/%.o, $(SRCS))

# Main build rule
all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $(TARGET)

$(BUILD_ROOT)/%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Run rule: execute the MPI cluster locally using the hostfile
run: $(TARGET)
	mpirun -np 3 --hostfile hostfile ./$(TARGET)

# Clean rule: remove compiled binaries
clean:
	rm -f $(OBJS) $(TARGET)
