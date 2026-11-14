CC ?= gcc
CPPFLAGS += -Iinclude -D_POSIX_C_SOURCE=200809L -D_FILE_OFFSET_BITS=64
CFLAGS ?= -std=c11 -Wall -Wextra -O2

.PHONY: all clean probar-fft

all: cliente

cliente: src/cliente.c src/cifrado.c include/cifrado.h
	$(CC) $(CPPFLAGS) $(CFLAGS) src/cliente.c src/cifrado.c $(LDFLAGS) -o $@

probar-fft: prueba_fft
	./prueba_fft

prueba_fft: Pruebas/probar_fft.c src/analisis_audio.c include/analisis_audio.h
	$(CC) $(CPPFLAGS) $(CFLAGS) Pruebas/probar_fft.c src/analisis_audio.c $(LDFLAGS) -lm -o $@

prueba_wav: Pruebas/probar_wav.c src/analisis_audio.c include/analisis_audio.h
	$(CC) $(CPPFLAGS) $(CFLAGS) Pruebas/probar_wav.c src/analisis_audio.c $(LDFLAGS) -lm -o $@

clean:
	rm -f cliente prueba_fft prueba_wav
