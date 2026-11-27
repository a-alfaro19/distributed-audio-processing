/*
 * Demo de la matriz real a traves de libaudio.a.
 * Uso: ./bin/demo_matriz [/dev/matriz8x8]
 *
 * Recorre: patrones completos, barrido de puntos y barras con atenuacion.
 */
#include "lib/libaudio.h"

#include <stdio.h>
#include <unistd.h>

#define PAUSA_PATRON_US 1500000
#define PAUSA_PUNTO_US 40000
#define PAUSA_BARRAS_US 120000

static const uint8_t CORAZON[AUDIO_MATRIX_ROWS] = {
    0x66, 0xFF, 0xFF, 0xFF, 0x7E, 0x3C, 0x18, 0x00};
static const uint8_t CARITA[AUDIO_MATRIX_ROWS] = {
    0x3C, 0x42, 0xA5, 0x81, 0xA5, 0x99, 0x42, 0x3C};
static const uint8_t AJEDREZ[AUDIO_MATRIX_ROWS] = {
    0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55};
static const uint8_t TODO[AUDIO_MATRIX_ROWS] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

static int fallo(const char *paso)
{
    perror(paso);
    return 1;
}

int main(int argc, char **argv)
{
    audio_matrix *m = audio_matrix_open(argc > 1 ? argv[1] : NULL);
    if (m == NULL)
        return fallo("abrir la matriz");

    const uint8_t *patrones[] = {TODO, CORAZON, CARITA, AJEDREZ};
    for (size_t i = 0; i < sizeof(patrones) / sizeof(patrones[0]); i++)
    {
        if (audio_matrix_show_pattern(m, patrones[i]) < 0)
            return fallo("patron");
        usleep(PAUSA_PATRON_US);
    }

    if (audio_matrix_clear(m) < 0)
        return fallo("clear");
    for (unsigned y = 0; y < AUDIO_MATRIX_ROWS; y++)
        for (unsigned x = 0; x < AUDIO_MATRIX_COLS; x++)
        {
            if (audio_matrix_set_point(m, x, y, true) < 0)
                return fallo("punto");
            usleep(PAUSA_PUNTO_US);
        }

    /* Barras: suben de golpe y bajan con atenuacion */
    if (audio_matrix_set_timing(m, 50, 60) < 0)
        return fallo("timing");
    const uint8_t picos[AUDIO_MATRIX_COLS] = {8, 6, 7, 5, 8, 4, 6, 3};
    const uint8_t silencio[AUDIO_MATRIX_COLS] = {0};
    for (int vuelta = 0; vuelta < 6; vuelta++)
    {
        if (audio_matrix_show_levels(m, picos) < 0 ||
            audio_matrix_show_levels(m, silencio) < 0)
            return fallo("niveles");
        usleep(PAUSA_BARRAS_US * 8);
    }

    audio_matrix_clear(m);
    audio_matrix_close(m);
    puts("Demo terminada.");
    return 0;
}
