/*
 * libaudio.c - Traduce las llamadas de alto nivel a los ioctl del driver
 * audiousb. Valida los argumentos aqui, en la frontera, para que el error
 * sea claro antes de llegar al kernel.
 */
#include "lib/libaudio.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "../driver/audiousb_ioctl.h"

_Static_assert(AUDIO_MATRIX_COLS == AUDIOUSB_COLS, "columnas distintas al driver");
_Static_assert(AUDIO_MATRIX_ROWS == AUDIOUSB_ROWS, "filas distintas al driver");
_Static_assert(AUDIO_MATRIX_REFRESH_MIN_MS == AUDIOUSB_REFRESH_MIN_MS &&
                   AUDIO_MATRIX_REFRESH_MAX_MS == AUDIOUSB_REFRESH_MAX_MS,
               "limites de refresco distintos al driver");

struct audio_matrix
{
    int fd;
};

/* Ejecuta un ioctl reintentando si una senal lo interrumpe. */
static int send_command(const audio_matrix *matrix, unsigned long request, void *arg)
{
    int result;
    do
    {
        result = ioctl(matrix->fd, request, arg);
    } while (result < 0 && errno == EINTR);
    return result < 0 ? -1 : 0;
}

static int invalid_argument(void)
{
    errno = EINVAL;
    return -1;
}

audio_matrix *audio_matrix_open(const char *path)
{
    audio_matrix *matrix = malloc(sizeof(*matrix));
    if (matrix == NULL)
        return NULL;

    matrix->fd = open(path != NULL ? path : AUDIO_MATRIX_DEFAULT_PATH, O_RDWR | O_CLOEXEC);
    if (matrix->fd < 0)
    {
        int saved = errno;
        free(matrix);
        errno = saved;
        return NULL;
    }
    return matrix;
}

void audio_matrix_close(audio_matrix *matrix)
{
    if (matrix == NULL)
        return;
    close(matrix->fd);
    free(matrix);
}

int audio_matrix_clear(audio_matrix *matrix)
{
    if (matrix == NULL)
        return invalid_argument();
    return send_command(matrix, AUDIOUSB_IOC_CLEAR, NULL);
}

int audio_matrix_set_point(audio_matrix *matrix, unsigned x, unsigned y, bool on)
{
    if (matrix == NULL || x >= AUDIO_MATRIX_COLS || y >= AUDIO_MATRIX_ROWS)
        return invalid_argument();

    struct audiousb_point point = {.x = (__u8)x, .y = (__u8)y, .on = on ? 1 : 0};
    return send_command(matrix, AUDIOUSB_IOC_SET_POINT, &point);
}

int audio_matrix_show_pattern(audio_matrix *matrix, const uint8_t rows[AUDIO_MATRIX_ROWS])
{
    if (matrix == NULL || rows == NULL)
        return invalid_argument();

    struct audiousb_pattern pattern;
    memcpy(pattern.rows, rows, sizeof(pattern.rows));
    return send_command(matrix, AUDIOUSB_IOC_SET_PATTERN, &pattern);
}

int audio_matrix_show_levels(audio_matrix *matrix, const uint8_t levels[AUDIO_MATRIX_COLS])
{
    if (matrix == NULL || levels == NULL)
        return invalid_argument();

    struct audiousb_frame frame;
    for (int c = 0; c < AUDIO_MATRIX_COLS; c++)
    {
        if (levels[c] > AUDIO_MATRIX_ROWS)
            return invalid_argument();
        frame.levels[c] = levels[c];
    }
    return send_command(matrix, AUDIOUSB_IOC_SET_FRAME, &frame);
}

int audio_matrix_set_timing(audio_matrix *matrix, unsigned refresh_ms, uint8_t decay)
{
    if (matrix == NULL ||
        refresh_ms < AUDIO_MATRIX_REFRESH_MIN_MS ||
        refresh_ms > AUDIO_MATRIX_REFRESH_MAX_MS)
        return invalid_argument();

    struct audiousb_timing timing = {.refresh_ms = (__u16)refresh_ms, .decay = decay};
    return send_command(matrix, AUDIOUSB_IOC_SET_TIMING, &timing);
}
