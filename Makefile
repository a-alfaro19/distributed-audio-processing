CC ?= gcc
# Busca las cabeceras en la carpeta del cliente y habilita las funciones POSIX.
CPPFLAGS += -Iinclude -Iinclude/client -D_POSIX_C_SOURCE=200809L -D_FILE_OFFSET_BITS=64
CFLAGS ?= -std=c11 -Wall -Wextra -O2

.PHONY: all clean probar-fft

all: cliente

# Compila el cliente junto con el modulo que cifra los bloques de audio.
cliente: src/client/cliente.c src/client/cifrado.c include/client/cifrado.h
	$(CC) $(CPPFLAGS) $(CFLAGS) src/client/cliente.c src/client/cifrado.c $(LDFLAGS) -o $@

# Compila y ejecuta las pruebas del analisis de audio.
probar-fft: prueba_fft
	./prueba_fft

# Enlaza la biblioteca matematica necesaria para la FFT.
prueba_fft: Test/probar_fft.c src/client/analisis_audio.c include/client/analisis_audio.h
	$(CC) $(CPPFLAGS) $(CFLAGS) Test/probar_fft.c src/client/analisis_audio.c $(LDFLAGS) -lm -o $@

# Borra los ejecutables generados para permitir una compilacion desde cero.
clean:
	rm -f cliente prueba_fft
