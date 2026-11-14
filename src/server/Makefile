# ==========================================
# Server / Cluster Makefile (OpenMPI)
# ==========================================

# MPI Compiler
CC = mpicc

# Compilation flags
CFLAGS = -Wall -Wextra -std=gnu99 -I../../include -I../../include/server

# Binary output target (placed in the root bin directory)
TARGET = ../../bin/server

# Root directories
BUILD_ROOT = ../../build/server
COMMON_LIB = ../../build/lib/libcommon.a
CRYPTO_LIB = ../../build/lib/libcrypto.a

# Source files and objects
SRCS = $(wildcard *.c)
OBJS = $(patsubst %.c, $(BUILD_ROOT)/%.o, $(SRCS))

# Main build rule
all: $(TARGET)

$(TARGET): $(OBJS) $(CRYPTO_LIB) $(COMMON_LIB)
	$(CC) $(CFLAGS) $(OBJS) $(CRYPTO_LIB) $(COMMON_LIB) -o $(TARGET)

$(BUILD_ROOT)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Run rule: execute the MPI cluster locally using the hostfile
run: $(TARGET)
	mpirun -np 3 --hostfile hostfile ./$(TARGET)

# Clean rule: remove compiled binaries
clean:
	rm -f $(OBJS) $(TARGET)
