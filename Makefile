# ==========================================
# Root Makefile for Distributed Audio Processing
# ==========================================

# Compiler
CC = gcc
AR = ar

# Flags
ARFLAGS = rcs
CFLAGS = -Wall -Wextra -std=gnu99 -Iinclude

# Client-specific flags
CLIENT_CPPFLAGS = -Iinclude -Iinclude/client -D_POSIX_C_SOURCE=200809L -D_FILE_OFFSET_BITS=64
CLIENT_CFLAGS = -std=c11 -Wall -Wextra -O2

# Directories
BIN_DIR = bin
BUILD_DIR = build
SERVER_DIR = src/server

# Library paths
COMMON_LIB = $(BUILD_DIR)/lib/libcommon.a
CRYPTO_LIB = $(BUILD_DIR)/lib/libcrypto.a
AUDIO_LIB = $(BUILD_DIR)/lib/libaudio.a

# Common module
COMMON_SRCS = $(wildcard src/common/*.c)
COMMON_OBJS = $(patsubst src/common/%.c, $(BUILD_DIR)/lib/common/%.o, $(COMMON_SRCS))

# Crypto module
CRYPTO_SRCS = $(wildcard src/crypto/*.c)
CRYPTO_OBJS = $(patsubst src/crypto/%.c, $(BUILD_DIR)/lib/crypto/%.o, $(CRYPTO_SRCS))

.PHONY: all libs libaudio probar-libaudio demo-matriz server run-server client cliente probar-fft clean clean-server

# Default target: compile everything essential
all: libs libaudio server cliente

# Build reusable libraries
libs: $(COMMON_LIB) $(CRYPTO_LIB)

# ==========================================
# Shared Static Libraries
# ==========================================
$(COMMON_LIB): $(COMMON_OBJS)
	@mkdir -p $(dir $@)
	$(AR) $(ARFLAGS) $@ $^

$(CRYPTO_LIB): $(CRYPTO_OBJS)
	@mkdir -p $(dir $@)
	$(AR) $(ARFLAGS) $@ $^

$(BUILD_DIR)/lib/common/%.o: src/common/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/lib/crypto/%.o: src/crypto/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# ==========================================
# libaudio.a: unico acceso al driver de la matriz 8x8
# ==========================================
libaudio: $(AUDIO_LIB)

$(AUDIO_LIB): $(BUILD_DIR)/lib/audio/libaudio.o
	@mkdir -p $(dir $@)
	$(AR) $(ARFLAGS) $@ $^

$(BUILD_DIR)/lib/audio/libaudio.o: src/lib/libaudio.c include/lib/libaudio.h src/driver/audiousb_ioctl.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Prueba sin hardware: valida argumentos usando /dev/null como dispositivo.
probar-libaudio: $(BIN_DIR)/probar_libaudio
	./$(BIN_DIR)/probar_libaudio

$(BIN_DIR)/probar_libaudio: pruebas/probar_libaudio.c $(AUDIO_LIB)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $< $(AUDIO_LIB) -o $@

# Demo con la matriz conectada (requiere el driver cargado).
demo-matriz: $(BIN_DIR)/demo_matriz

$(BIN_DIR)/demo_matriz: pruebas/demo_matriz.c $(AUDIO_LIB)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $< $(AUDIO_LIB) -o $@

# ==========================================
# Server / MPI Cluster Rules
# ==========================================
server: libs
		@mkdir -p $(BIN_DIR)
		@mkdir -p $(BUILD_DIR)/server
		$(MAKE) -C $(SERVER_DIR)

run-server: server
	$(MAKE) -C $(SERVER_DIR) run

# ==========================================
# Client & Local Audio Tests Rules
# ==========================================
client: cliente

# Compila el cliente TCP y el cifrado XOR.
cliente: src/client/cliente.c src/client/cifrado.c include/client/cifrado.h
	$(CC) $(CLIENT_CPPFLAGS) $(CLIENT_CFLAGS) src/client/cliente.c src/client/cifrado.c $(LDFLAGS) -o $@

probar-fft: prueba_fft
	./prueba_fft

# Comprueba las bandas de frecuencia con tonos conocidos.
prueba_fft: pruebas/probar_fft.c src/client/analisis_audio.c include/client/analisis_audio.h
	$(CC) $(CLIENT_CPPFLAGS) $(CLIENT_CFLAGS) pruebas/probar_fft.c src/client/analisis_audio.c $(LDFLAGS) -lm -o $@

# Lee un WAV PCM16 y muestra ocho alturas por ventana.
prueba_wav: pruebas/probar_wav.c src/client/analisis_audio.c include/client/analisis_audio.h
	$(CC) $(CLIENT_CPPFLAGS) $(CLIENT_CFLAGS) pruebas/probar_wav.c src/client/analisis_audio.c $(LDFLAGS) -lm -o $@

# ==========================================
# Global Cleanup
# ==========================================
clean:
		@echo "Cleaning binaries and objects..."
		rm -rf $(BIN_DIR)/* $(BUILD_DIR)/*
		rm -f cliente prueba_fft prueba_wav
