#include "analisis_audio.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

typedef struct
{
    uint32_t frecuencia;
    uint16_t canales;
    uint64_t inicio_audio;
    uint32_t bytes_audio;
} Wav;

/* El WAV guarda primero el byte menos significativo de cada numero. */
static uint16_t leer_u16(const uint8_t *bytes)
{
    return (uint16_t)(bytes[0] | ((uint16_t)bytes[1] << 8));
}

static uint32_t leer_u32(const uint8_t *bytes)
{
    return (uint32_t)leer_u16(bytes) | ((uint32_t)leer_u16(bytes + 2) << 16);
}

static int32_t leer_muestra(const uint8_t *bytes)
{
    int32_t valor = leer_u16(bytes);
    return valor >= 32768 ? valor - 65536 : valor;
}

static const char *leer_encabezado(FILE *archivo, Wav *wav)
{
    if (fseeko(archivo, 0, SEEK_END) != 0)
        return "No se pudo medir el archivo.";
    off_t tamano = ftello(archivo);
    uint8_t cabecera[12];
    if (tamano < (off_t)sizeof(cabecera) ||
        fseeko(archivo, 0, SEEK_SET) != 0 ||
        fread(cabecera, 1, sizeof(cabecera), archivo) != sizeof(cabecera) ||
        memcmp(cabecera, "RIFF", 4) != 0 ||
        memcmp(cabecera + 8, "WAVE", 4) != 0)
        return "Se esperaba un WAV con encabezado RIFF/WAVE.";

    uint64_t fin_riff = 8 + (uint64_t)leer_u32(cabecera + 4);
    if (fin_riff < sizeof(cabecera) || fin_riff > (uint64_t)tamano)
        return "El tamano RIFF no coincide con los datos disponibles.";

    int tiene_formato = 0;
    int tiene_audio = 0;
    uint64_t posicion = sizeof(cabecera);

    /* Busca los bloques fmt y data. Otros bloques pueden guardar metadatos. */
    while (fin_riff - posicion >= 8 && (!tiene_formato || !tiene_audio))
    {
        uint8_t bloque[8];
        if (fseeko(archivo, (off_t)posicion, SEEK_SET) != 0 ||
            fread(bloque, 1, sizeof(bloque), archivo) != sizeof(bloque))
            return "No se pudo leer el encabezado de un bloque WAV.";

        uint32_t cantidad = leer_u32(bloque + 4);
        uint64_t inicio = posicion + sizeof(bloque);
        /* Los bloques con longitud impar incluyen un byte de relleno. */
        posicion = inicio + cantidad + (cantidad % 2);
        if (posicion > fin_riff)
            return "El WAV contiene un bloque incompleto.";

        if (!tiene_formato && memcmp(bloque, "fmt ", 4) == 0)
        {
            uint8_t formato[16];
            if (cantidad < sizeof(formato) ||
                fread(formato, 1, sizeof(formato), archivo) != sizeof(formato))
                return "El bloque de formato WAV esta incompleto.";

            wav->canales = leer_u16(formato + 2);
            wav->frecuencia = leer_u32(formato + 4);
            if (leer_u16(formato) != 1 || leer_u16(formato + 14) != 16 ||
                (wav->canales != 1 && wav->canales != 2) || wav->frecuencia == 0)
                return "Se requiere un WAV PCM de 16 bits, mono o estereo.";

            uint16_t bytes_por_muestra = wav->canales * 2;
            if (leer_u16(formato + 12) != bytes_por_muestra ||
                leer_u32(formato + 8) != (uint64_t)wav->frecuencia * bytes_por_muestra)
                return "El formato WAV tiene tamanos inconsistentes.";
            tiene_formato = 1;
        }
        else if (!tiene_audio && memcmp(bloque, "data", 4) == 0)
        {
            wav->inicio_audio = inicio;
            wav->bytes_audio = cantidad;
            tiene_audio = 1;
        }
    }

    if (!tiene_formato || !tiene_audio)
        return "El WAV debe tener bloques fmt y data.";
    if (wav->bytes_audio == 0 || wav->bytes_audio % (wav->canales * 2) != 0)
        return "El WAV no tiene muestras completas.";

    return NULL;
}

static const char *analizar_wav(FILE *archivo, const Wav *wav)
{
    size_t bytes_por_muestra = wav->canales * 2;
    uint32_t restantes = wav->bytes_audio / bytes_por_muestra;
    uint8_t bloque[ANALISIS_MUESTRAS * 4];
    int16_t muestras[ANALISIS_MUESTRAS];
    uint8_t alturas[ANALISIS_COLUMNAS];

    if (fseeko(archivo, (off_t)wav->inicio_audio, SEEK_SET) != 0)
        return "No se pudo buscar el inicio del audio.";

    printf("WAV PCM16 | %" PRIu32 " Hz | %u canal(es) | %" PRIu32 " muestras por canal\n",
           wav->frecuencia, (unsigned)wav->canales, restantes);

    for (uint32_t ventana = 0; restantes > 0; ++ventana)
    {
        /* La ultima ventana puede tener menos de 2048 muestras. */
        size_t cantidad = restantes < ANALISIS_MUESTRAS ? restantes : ANALISIS_MUESTRAS;
        if (fread(bloque, bytes_por_muestra, cantidad, archivo) != cantidad)
            return "El WAV termino antes de completar el audio.";

        for (size_t i = 0; i < cantidad; ++i)
        {
            const uint8_t *bytes = bloque + i * bytes_por_muestra;
            int32_t valor = leer_muestra(bytes);
            /* La FFT recibe mono. En estereo promediamos ambos canales. */
            if (wav->canales == 2)
                valor = (valor + leer_muestra(bytes + 2)) / 2;
            muestras[i] = (int16_t)valor;
        }

        if (analizar_ventana(muestras, cantidad, wav->frecuencia, alturas) != 0)
            return "No se pudo analizar una ventana de audio.";

        printf("Ventana %" PRIu32 " (%zu muestras): [", ventana, cantidad);
        for (size_t i = 0; i < ANALISIS_COLUMNAS; ++i)
            printf("%s%u", i == 0 ? "" : ", ", (unsigned)alturas[i]);
        puts("]");
        restantes -= (uint32_t)cantidad;
    }

    if (fflush(stdout) == EOF || ferror(stdout))
        return "No se pudo escribir la salida.";

    return NULL;
}

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        fprintf(stderr, "Uso: %s ARCHIVO.wav\n", argv[0]);
        return EXIT_FAILURE;
    }

    FILE *archivo = fopen(argv[1], "rb");
    if (archivo == NULL)
    {
        perror(argv[1]);
        return EXIT_FAILURE;
    }

    Wav wav = {0};
    const char *error = leer_encabezado(archivo, &wav);
    if (error == NULL)
        error = analizar_wav(archivo, &wav);
    fclose(archivo);

    if (error != NULL)
    {
        fprintf(stderr, "%s: %s\n", argv[1], error);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
