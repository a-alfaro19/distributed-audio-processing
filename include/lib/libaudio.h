/*
 * libaudio.h - Biblioteca de espacio de usuario para la matriz de LEDs 8x8.
 *
 * Es el unico modulo que habla con el driver (/dev/audiousbN). El resto del
 * sistema recibe un manejador opaco y nunca toca el descriptor ni los ioctl.
 *
 * Todas las funciones que devuelven int retornan 0 si tuvieron exito, o -1
 * con errno indicando la causa (EINVAL para argumentos invalidos, ENODEV si
 * la Pico se desconecto, etc.).
 *
 * Coordenadas: x = columna (0 = izquierda), y = fila (0 = arriba).
 * Patron: rows[0] es la fila de arriba; el bit 7 de cada byte es la
 * columna 0 (izquierda). Ejemplo: 0x80 enciende solo el LED de la izquierda.
 */
#ifndef LIBAUDIO_H
#define LIBAUDIO_H

#include <stdbool.h>
#include <stdint.h>

#define AUDIO_MATRIX_COLS 8
#define AUDIO_MATRIX_ROWS 8
#define AUDIO_MATRIX_DEFAULT_PATH "/dev/audiousb0"

/* Limites del periodo de refresco aceptado por el dispositivo */
#define AUDIO_MATRIX_REFRESH_MIN_MS 1
#define AUDIO_MATRIX_REFRESH_MAX_MS 1000

typedef struct audio_matrix audio_matrix;

/* Abre la matriz. path == NULL usa AUDIO_MATRIX_DEFAULT_PATH. NULL si falla. */
audio_matrix *audio_matrix_open(const char *path);

/* Cierra la matriz y libera el manejador. Acepta NULL. */
void audio_matrix_close(audio_matrix *matrix);

/* Apaga todos los LEDs. */
int audio_matrix_clear(audio_matrix *matrix);

/* Enciende o apaga un LED sin tocar los demas. */
int audio_matrix_set_point(audio_matrix *matrix, unsigned x, unsigned y, bool on);

/* Muestra una imagen completa de 8x8 en una sola transferencia. */
int audio_matrix_show_pattern(audio_matrix *matrix,
                              const uint8_t rows[AUDIO_MATRIX_ROWS]);

/* Muestra barras: levels[c] (0..8) es la altura de la columna c desde abajo. */
int audio_matrix_show_levels(audio_matrix *matrix,
                             const uint8_t levels[AUDIO_MATRIX_COLS]);

/*
 * Ajusta la animacion de las barras: cada refresh_ms la barra baja la
 * fraccion decay/255 de lo que le falta para llegar a su nuevo nivel.
 * decay = 0 hace que baje de inmediato.
 */
int audio_matrix_set_timing(audio_matrix *matrix, unsigned refresh_ms, uint8_t decay);

#endif /* LIBAUDIO_H */
