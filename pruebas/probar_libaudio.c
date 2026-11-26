/*
 * Prueba de libaudio.a sin hardware.
 *
 * Abre /dev/null como si fuera la matriz: los argumentos invalidos deben
 * rechazarse con EINVAL antes de llegar al driver, y los validos deben
 * llegar hasta ioctl(), que en /dev/null falla con ENOTTY.
 */
#include "lib/libaudio.h"

#include <errno.h>
#include <stdio.h>

static int fallos = 0;

static void esperar(const char *caso, int resultado, int errno_esperado)
{
    int errno_real = errno;
    if (resultado != -1 || errno_real != errno_esperado)
    {
        printf("FALLA  %s: resultado=%d errno=%d (esperado -1, %d)\n",
               caso, resultado, errno_real, errno_esperado);
        fallos++;
        return;
    }
    printf("ok     %s\n", caso);
}

int main(void)
{
    const uint8_t patron[AUDIO_MATRIX_ROWS] = {0x80, 0, 0, 0, 0, 0, 0, 0x01};
    const uint8_t niveles_ok[AUDIO_MATRIX_COLS] = {0, 1, 2, 3, 4, 5, 6, 8};
    const uint8_t niveles_malos[AUDIO_MATRIX_COLS] = {0, 0, 0, 9, 0, 0, 0, 0};

    errno = 0;
    audio_matrix *inexistente = audio_matrix_open("/no/existe/audiousb0");
    if (inexistente != NULL || errno != ENOENT)
    {
        printf("FALLA  abrir ruta inexistente debe dar NULL y ENOENT\n");
        fallos++;
    }
    else
    {
        printf("ok     abrir ruta inexistente\n");
    }

    audio_matrix *m = audio_matrix_open("/dev/null");
    if (m == NULL)
    {
        perror("no se pudo abrir /dev/null");
        return 1;
    }

    /* Manejador nulo */
    esperar("clear con NULL", audio_matrix_clear(NULL), EINVAL);
    esperar("patron con NULL", audio_matrix_show_pattern(NULL, patron), EINVAL);
    esperar("patron sin filas", audio_matrix_show_pattern(m, NULL), EINVAL);
    esperar("niveles sin datos", audio_matrix_show_levels(m, NULL), EINVAL);

    /* Valores fuera de rango */
    esperar("nivel 9", audio_matrix_show_levels(m, niveles_malos), EINVAL);
    esperar("punto x=8", audio_matrix_set_point(m, 8, 0, true), EINVAL);
    esperar("punto y=8", audio_matrix_set_point(m, 0, 8, true), EINVAL);
    esperar("refresco 0 ms", audio_matrix_set_timing(m, 0, 10), EINVAL);
    esperar("refresco 1001 ms", audio_matrix_set_timing(m, 1001, 10), EINVAL);

    /* Valores validos: llegan al driver (ENOTTY porque es /dev/null) */
    esperar("clear llega al driver", audio_matrix_clear(m), ENOTTY);
    esperar("punto llega al driver", audio_matrix_set_point(m, 7, 7, false), ENOTTY);
    esperar("patron llega al driver", audio_matrix_show_pattern(m, patron), ENOTTY);
    esperar("niveles llegan al driver", audio_matrix_show_levels(m, niveles_ok), ENOTTY);
    esperar("timing llega al driver", audio_matrix_set_timing(m, 1000, 255), ENOTTY);

    audio_matrix_close(m);
    audio_matrix_close(NULL); /* no debe fallar */

    printf(fallos == 0 ? "\nTodas las pruebas pasaron.\n" : "\n%d pruebas fallaron.\n", fallos);
    return fallos == 0 ? 0 : 1;
}
