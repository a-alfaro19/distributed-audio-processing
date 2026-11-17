#ifndef ANALISIS_AUDIO_H
#define ANALISIS_AUDIO_H

#include <stddef.h>
#include <stdint.h>

enum
{
    ANALISIS_MUESTRAS = 2048,
    ANALISIS_BANDAS = 7,
    ANALISIS_COLUMNAS = 8,
    ANALISIS_FILAS = 8
};

/* Recibe PCM mono de 16 bits y llena las 8 alturas. Devuelve 0 si funciona. */
int analizar_ventana(const int16_t *muestras, size_t cantidad,
                     uint32_t frecuencia_muestreo,
                     uint8_t alturas[ANALISIS_COLUMNAS]);

#endif
