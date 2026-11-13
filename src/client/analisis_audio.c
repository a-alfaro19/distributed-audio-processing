#include "analisis_audio.h"

#include <math.h>

static const double DOS_PI = 6.28318530717958647692;
static const double PISO_DBFS = -48.0;

static const double limites_hz[ANALISIS_BANDAS + 1] = {
    20, 100, 250, 500, 1000, 2000, 6000, 20000};

typedef struct
{
    double real;
    double imag;
} Complejo;

static void fft(Complejo datos[ANALISIS_MUESTRAS])
{
    const size_t n = ANALISIS_MUESTRAS;

    /* Reordena las muestras por bits para hacer la FFT por etapas. */
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n / 2;
        while ((j & bit) != 0)
        {
            j ^= bit;
            bit /= 2;
        }
        j ^= bit;
        if (i < j)
        {
            Complejo aux = datos[i];
            datos[i] = datos[j];
            datos[j] = aux;
        }
    }

    /* Cada etapa combina pares de bloques mas pequenos. */
    for (size_t largo = 2; largo <= n; largo *= 2)
    {
        double angulo = -DOS_PI / (double)largo;
        Complejo giro = {cos(angulo), sin(angulo)};

        for (size_t inicio = 0; inicio < n; inicio += largo)
        {
            Complejo factor = {1.0, 0.0};
            for (size_t j = 0; j < largo / 2; ++j)
            {
                size_t mitad = inicio + j + largo / 2;
                Complejo a = datos[inicio + j];
                Complejo b = datos[mitad];
                Complejo rotado = {
                    b.real * factor.real - b.imag * factor.imag,
                    b.real * factor.imag + b.imag * factor.real};
                datos[inicio + j] = (Complejo){a.real + rotado.real,
                                               a.imag + rotado.imag};
                datos[mitad] = (Complejo){a.real - rotado.real,
                                          a.imag - rotado.imag};
                factor = (Complejo){
                    factor.real * giro.real - factor.imag * giro.imag,
                    factor.real * giro.imag + factor.imag * giro.real};
            }
        }
    }
}

static uint8_t altura_desde_rms(double rms)
{
    if (rms <= 0.0)
        return 0;

    /* Usa la misma escala de volumen en todas las ventanas. */
    double dbfs = 20.0 * log10(rms);
    if (dbfs <= PISO_DBFS)
        return 0;
    if (dbfs >= 0.0)
        return ANALISIS_FILAS;

    double nivel = (dbfs - PISO_DBFS) / -PISO_DBFS;
    return (uint8_t)lround(nivel * ANALISIS_FILAS);
}

int analizar_ventana(const int16_t *muestras, size_t cantidad,
                     uint32_t frecuencia_muestreo,
                     uint8_t alturas[ANALISIS_COLUMNAS])
{
    if (muestras == NULL || alturas == NULL || cantidad == 0 ||
        cantidad > ANALISIS_MUESTRAS || frecuencia_muestreo == 0)
        return -1;

    double suma = 0.0;
    double suma_cuadrados = 0.0;
    for (size_t i = 0; i < cantidad; ++i)
    {
        double valor = muestras[i] / 32768.0;
        suma += valor;
        suma_cuadrados += valor * valor;
    }

    double media = suma / (double)cantidad;
    Complejo datos[ANALISIS_MUESTRAS] = {0};
    double energia_hann = 0.0;

    /* Quita el promedio y usa Hann para reducir la fuga entre bandas. */
    for (size_t i = 0; i < cantidad; ++i)
    {
        double hann = cantidad == 1 ? 1.0
                                    : 0.5 * (1.0 - cos(DOS_PI * i / (double)cantidad));
        datos[i].real = (muestras[i] / 32768.0 - media) * hann;
        energia_hann += hann * hann;
    }
    /* Si la ventana es corta, la parte que sobra queda en cero. */
    fft(datos);

    double potencia[ANALISIS_BANDAS] = {0};
    size_t banda = 0;
    for (size_t k = 1; k <= ANALISIS_MUESTRAS / 2; ++k)
    {
        double hz = (double)k * frecuencia_muestreo / ANALISIS_MUESTRAS;
        if (hz < limites_hz[0])
            continue;
        if (hz > limites_hz[ANALISIS_BANDAS])
            break;
        while (banda + 1 < ANALISIS_BANDAS && hz >= limites_hz[banda + 1])
            ++banda;

        double real = datos[k].real;
        double imag = datos[k].imag;
        /* Duplica la energia de los bins que tienen una frecuencia negativa asociada. */
        double factor = k == ANALISIS_MUESTRAS / 2 ? 1.0 : 2.0;
        potencia[banda] += factor * (real * real + imag * imag);
    }

    double escala = ANALISIS_MUESTRAS * energia_hann;
    for (size_t i = 0; i < ANALISIS_BANDAS; ++i)
        alturas[i] = altura_desde_rms(sqrt(potencia[i] / escala));

    /* La ultima columna muestra el RMS del audio antes de aplicar Hann. */
    alturas[ANALISIS_BANDAS] = altura_desde_rms(sqrt(suma_cuadrados / cantidad));
    return 0;
}
