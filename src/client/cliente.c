#include "cifrado.h"

#include <errno.h>
#include <inttypes.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

enum
{
    BYTES_TAMANO = 8,
    BYTES_BLOQUE = 16384
};

static int conectar(const char *servidor, const char *puerto)
{
    struct addrinfo opciones = {
        .ai_family = AF_UNSPEC,
        .ai_socktype = SOCK_STREAM,
        .ai_flags = AI_NUMERICSERV};
    struct addrinfo *direcciones = NULL;
    int resultado = getaddrinfo(servidor, puerto, &opciones, &direcciones);
    if (resultado != 0)
    {
        fprintf(stderr, "No se pudo resolver el servidor: %s\n",
                gai_strerror(resultado));
        return -1;
    }

    /* Un nombre puede tener varias direcciones. Probamos hasta conectar. */
    int conexion = -1;
    for (struct addrinfo *dir = direcciones; dir != NULL; dir = dir->ai_next)
    {
        conexion = socket(dir->ai_family, dir->ai_socktype, dir->ai_protocol);
        if (conexion < 0)
            continue;
        if (connect(conexion, dir->ai_addr, dir->ai_addrlen) == 0)
            break;

        close(conexion);
        conexion = -1;
    }
    freeaddrinfo(direcciones);

    if (conexion < 0)
        fprintf(stderr, "No se pudo conectar a %s:%s.\n", servidor, puerto);

    return conexion;
}

static int enviar_cifrado(int conexion, uint8_t *datos, size_t cantidad,
                          const uint8_t *clave, size_t longitud_clave,
                          uint64_t desplazamiento)
{
    if (aplicar_xor(datos, cantidad, clave, longitud_clave, desplazamiento) < 0)
        return -1;

    /* send puede aceptar solo una parte de los bytes. */
    size_t enviados = 0;
    while (enviados < cantidad)
    {
        ssize_t n = send(conexion, datos + enviados, cantidad - enviados,
                         MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
        {
            if (n == 0)
                errno = EPIPE;
            return -1;
        }
        enviados += (size_t)n;
    }

    return 0;
}

static int enviar_wav(int conexion, FILE *archivo, uint64_t tamano,
                      const uint8_t *clave, size_t longitud_clave)
{
    /* El tamano ocupa 8 bytes, del mas al menos significativo. */
    uint8_t cabecera[BYTES_TAMANO];
    for (size_t i = 0; i < sizeof(cabecera); ++i)
        cabecera[i] = (uint8_t)(tamano >> (8 * (sizeof(cabecera) - 1 - i)));

    if (enviar_cifrado(conexion, cabecera, sizeof(cabecera),
                       clave, longitud_clave, 0) < 0)
    {
        perror("Error enviando el tamaño");
        return -1;
    }

    /* se lee por bloques para usar poca memoria incluso con audios grandes. */
    uint8_t bloque[BYTES_BLOQUE];
    uint64_t leidos = 0;
    while (leidos < tamano)
    {
        uint64_t restantes = tamano - leidos;
        size_t cantidad = restantes < sizeof(bloque)
                              ? (size_t)restantes
                              : sizeof(bloque);
        if (fread(bloque, 1, cantidad, archivo) != cantidad)
        {
            fprintf(stderr, "No se pudo leer el WAV completo.\n");
            return -1;
        }

        /* Los 8 bytes del tamano tambien cuentan en el desplazamiento. */
        if (enviar_cifrado(conexion, bloque, cantidad, clave, longitud_clave,
                           sizeof(cabecera) + leidos) < 0)
        {
            perror("Error enviando el WAV");
            return -1;
        }
        leidos += cantidad;
    }

    return 0;
}

int main(int argc, char *argv[])
{
    if (argc != 4)
    {
        fprintf(stderr, "Uso: %s SERVIDOR PUERTO ARCHIVO.wav\n", argv[0]);
        return EXIT_FAILURE;
    }

    char *fin_puerto;
    long puerto = strtol(argv[2], &fin_puerto, 10);
    if (fin_puerto == argv[2] || *fin_puerto != '\0' || puerto < 1 || puerto > 65535)
    {
        fprintf(stderr, "El puerto debe ser un número entre 1 y 65535.\n");
        return EXIT_FAILURE;
    }

    /* Cliente y receptor cargan la misma clave en sus propias terminales. */
    const char *clave = getenv("AUDIO_XOR_KEY");
    if (clave == NULL || *clave == '\0')
    {
        fprintf(stderr, "Definí AUDIO_XOR_KEY antes de ejecutar el cliente.\n");
        return EXIT_FAILURE;
    }

    FILE *archivo = fopen(argv[3], "rb");
    if (archivo == NULL)
    {
        perror("No se pudo abrir el WAV");
        return EXIT_FAILURE;
    }

    /* Comprobamos la firma RIFF/WAVE y obtenemos el tamano en bytes. */
    struct stat datos_archivo;
    uint8_t firma[12];
    if (fstat(fileno(archivo), &datos_archivo) < 0 ||
        !S_ISREG(datos_archivo.st_mode) ||
        datos_archivo.st_size < (off_t)sizeof(firma) ||
        fread(firma, 1, sizeof(firma), archivo) != sizeof(firma) ||
        memcmp(firma, "RIFF", 4) != 0 || memcmp(firma + 8, "WAVE", 4) != 0 ||
        fseek(archivo, 0, SEEK_SET) != 0)
    {
        fprintf(stderr, "Se requiere un archivo WAV con encabezado RIFF/WAVE.\n");
        fclose(archivo);
        return EXIT_FAILURE;
    }

    int conexion = conectar(argv[1], argv[2]);
    if (conexion < 0)
    {
        fclose(archivo);
        return EXIT_FAILURE;
    }

    uint64_t tamano = (uint64_t)datos_archivo.st_size;
    int resultado = enviar_wav(conexion, archivo, tamano,
                               (const uint8_t *)clave, strlen(clave));

    /* Cerramos ambos recursos aunque el envio falle. */
    close(conexion);
    fclose(archivo);
    if (resultado < 0)
        return EXIT_FAILURE;

    printf("Enviados %" PRIu64 " bytes del WAV a %s:%s.\n", tamano, argv[1], argv[2]);
    return EXIT_SUCCESS;
}
