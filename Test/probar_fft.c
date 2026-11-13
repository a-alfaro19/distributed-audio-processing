#include "analisis_audio.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static void imprimir(const char *caso, const uint8_t alturas[ANALISIS_COLUMNAS])
{
    printf("%-12s [", caso);
    for (size_t i = 0; i < ANALISIS_COLUMNAS; ++i)
        printf("%s%u", i == 0 ? "" : ", ", (unsigned)alturas[i]);
    puts("]");
}

int main(void)
{
    const uint32_t frecuencia_muestreo = 44100;
    const unsigned tonos[ANALISIS_BANDAS] = { 65, 170, 440, 750, 1500, 4000, 9000 };
    int16_t muestras[ANALISIS_MUESTRAS] = { 0 };
    uint8_t alturas[ANALISIS_COLUMNAS];

    if (analizar_ventana(muestras, ANALISIS_MUESTRAS, frecuencia_muestreo, alturas) != 0)
        return EXIT_FAILURE;
    imprimir("Silencio", alturas);
    for (size_t i = 0; i < ANALISIS_COLUMNAS; ++i) {
        if (alturas[i] != 0)
            return EXIT_FAILURE;
    }

    /* En cada tono debe destacar la columna de su rango de frecuencia. */
    for (size_t banda = 0; banda < ANALISIS_BANDAS; ++banda) {
        for (size_t i = 0; i < ANALISIS_MUESTRAS; ++i) {
            double fase = 6.283185307179586 * tonos[banda] * i / frecuencia_muestreo;
            muestras[i] = (int16_t)lround(10000.0 * sin(fase));
        }
        if (analizar_ventana(muestras, ANALISIS_MUESTRAS, frecuencia_muestreo, alturas) != 0)
            return EXIT_FAILURE;

        char caso[20];
        snprintf(caso, sizeof(caso), "%u Hz", tonos[banda]);
        imprimir(caso, alturas);
        for (size_t i = 0; i < ANALISIS_COLUMNAS; ++i) {
            if (alturas[i] > ANALISIS_FILAS ||
                (i < ANALISIS_BANDAS && i != banda && alturas[i] >= alturas[banda])) {
                fprintf(stderr, "La banda esperada no destaco para %s.\n", caso);
                return EXIT_FAILURE;
            }
        }
        if (alturas[ANALISIS_BANDAS] == 0)
            return EXIT_FAILURE;
    }

    /* Un ultimo bloque corto tambien debe producir ocho alturas validas. */
    if (analizar_ventana(muestras, 1000, frecuencia_muestreo, alturas) != 0 ||
        alturas[6] == 0 ||
        analizar_ventana(muestras, 0, frecuencia_muestreo, alturas) == 0)
        return EXIT_FAILURE;

    puts("Pruebas de FFT 8 x 8 correctas.");
    return EXIT_SUCCESS;
}
