# ==========================================
# Server / Cluster Makefile (OpenMPI)
# ==========================================

# MPI Compiler
CC = mpicc

# Compilation flags
CFLAGS = -Wall -Wextra -std=gnu99

# Binary output target (placed in the root bin directory)
TARGET = ../../bin/server

# Source files
SRC = server.c

# Main build rule
all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET)

# Run rule: execute the MPI cluster locally using the hostfile
run: $(TARGET)
	mpirun -np 3 --hostfile hostfile ./$(TARGET)

# Clean rule: remove compiled binaries
clean:
	rm -f $(TARGET)
