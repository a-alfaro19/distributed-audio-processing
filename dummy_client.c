// ==========================================
// Dummy TCP Client for Testing Master Server
// ==========================================

#include <arpa/inet.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

// Ajusta estas rutas si colocaste cifrado.h y byte_utils.h en otro directorio
#include "crypto/cifrado.h"
#include "common/byte_utils.h"

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 8080
#define HEADER_SIZE 8

/**
 * @brief Sends all bytes reliably over a TCP socket.
 */
static int send_all(int sock_fd, const uint8_t *data, size_t length)
{
    size_t sent = 0;
    while (sent < length)
    {
        ssize_t n = send(sock_fd, data + sent, length - sent, 0);
        if (n <= 0)
        {
            perror("[DummyClient Error] Failed sending data");
            return -1;
        }
        sent += (size_t)n;
    }
    return 0;
}

int main(void)
{
    // 1. Validar que la clave XOR esté definida en el entorno
    const char *env_key = getenv("AUDIO_XOR_KEY");
    if (env_key == NULL || *env_key == '\0')
    {
        fprintf(stderr, "[DummyClient Error] Define AUDIO_XOR_KEY antes de ejecutar.\n");
        return EXIT_FAILURE;
    }
    const uint8_t *key = (const uint8_t *)env_key;
    size_t key_len = strlen(env_key);

    // 2. Construir un payload WAV sintético en memoria (44 bytes de cabecera RIFF/WAVE + 16 bytes de audio dummy)
    uint8_t dummy_wav[60] = {0};
    memcpy(dummy_wav, "RIFF", 4);
    memcpy(dummy_wav + 8, "WAVE", 4);
    memcpy(dummy_wav + 12, "fmt ", 4);
    memcpy(dummy_wav + 36, "data", 4);
    // Datos de audio simulados al final
    for (size_t i = 44; i < sizeof(dummy_wav); ++i)
    {
        dummy_wav[i] = (uint8_t)(i * 3);
    }

    uint64_t payload_size = sizeof(dummy_wav);

    // 3. Conectar al servidor TCP (Nodo Maestro)
    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0)
    {
        perror("[DummyClient Error] Socket creation failed");
        return EXIT_FAILURE;
    }

    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(SERVER_PORT)};
    inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr);

    printf("[DummyClient] Conectando a %s:%d...\n", SERVER_IP, SERVER_PORT);
    if (connect(sock_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("[DummyClient Error] Connection failed (is the MPI server running?)");
        close(sock_fd);
        return EXIT_FAILURE;
    }

    // 4. Preparar y cifrar la cabecera de 8 bytes (Big-Endian, offset = 0)
    uint8_t header[HEADER_SIZE];
    uint64_to_bytes_be(payload_size, header);

    if (aplicar_xor(header, HEADER_SIZE, key, key_len, 0) < 0)
    {
        fprintf(stderr, "[DummyClient Error] Error cifrando cabecera.\n");
        close(sock_fd);
        return EXIT_FAILURE;
    }

    // 5. Cifrar el payload WAV (offset = 8)
    if (aplicar_xor(dummy_wav, (size_t)payload_size, key, key_len, HEADER_SIZE) < 0)
    {
        fprintf(stderr, "[DummyClient Error] Error cifrando payload WAV.\n");
        close(sock_fd);
        return EXIT_FAILURE;
    }

    // 6. Enviar cabecera cifrada + payload cifrado
    printf("[DummyClient] Enviando cabecera (8 bytes) + WAV cifrado (%" PRIu64 " bytes)...\n", payload_size);
    if (send_all(sock_fd, header, HEADER_SIZE) < 0 ||
        send_all(sock_fd, dummy_wav, (size_t)payload_size) < 0)
    {
        close(sock_fd);
        return EXIT_FAILURE;
    }

    printf("[DummyClient] Transmisión completada con éxito.\n");
    close(sock_fd);
    return EXIT_SUCCESS;
}
