CC ?= gcc
CPPFLAGS += -Iinclude -Iinclude/client -D_POSIX_C_SOURCE=200809L -D_FILE_OFFSET_BITS=64
CFLAGS ?= -std=c11 -Wall -Wextra -O2

.PHONY: all clean probar-fft

all: cliente

# Compila el cliente TCP y el cifrado XOR.
cliente: src/client/cliente.c src/client/cifrado.c include/client/cifrado.h
	$(CC) $(CPPFLAGS) $(CFLAGS) src/client/cliente.c src/client/cifrado.c $(LDFLAGS) -o $@

probar-fft: prueba_fft
	./prueba_fft

# Comprueba las bandas de frecuencia con tonos conocidos.
prueba_fft: pruebas/probar_fft.c src/client/analisis_audio.c include/client/analisis_audio.h
	$(CC) $(CPPFLAGS) $(CFLAGS) pruebas/probar_fft.c src/client/analisis_audio.c $(LDFLAGS) -lm -o $@

# Lee un WAV PCM16 y muestra ocho alturas por ventana.
prueba_wav: pruebas/probar_wav.c src/client/analisis_audio.c include/client/analisis_audio.h
	$(CC) $(CPPFLAGS) $(CFLAGS) pruebas/probar_wav.c src/client/analisis_audio.c $(LDFLAGS) -lm -o $@

clean:
	rm -f cliente prueba_fft prueba_wav
