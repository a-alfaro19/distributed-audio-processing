CC ?= gcc
CPPFLAGS += -Iinclude -D_POSIX_C_SOURCE=200809L -D_FILE_OFFSET_BITS=64
CFLAGS ?= -std=c11 -Wall -Wextra -O2

.PHONY: all clean

all: cliente

cliente: src/cliente.c src/cifrado.c include/cifrado.h
	$(CC) $(CPPFLAGS) $(CFLAGS) src/cliente.c src/cifrado.c $(LDFLAGS) -o $@

clean:
	rm -f cliente
