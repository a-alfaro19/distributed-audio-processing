// ==========================================
// Master Node Control Logic Implementation
// ==========================================

#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <inttypes.h>
#include <sys/socket.h>
#include "master.h"
#include "socket_server.h"
#include <common/byte_utils.h>
#include <crypto/cifrado.h>

#define PORT 8080
#define HEADER_SIZE 8

/**
 * @brief Decrypts a byte buffer in-place using XOR with a given stream offset.
 *
 * @param buffer Pointer to the encrypted data buffer.
 * @param length Number of bytes to decrypt.
 * @param key XOR encryption key.
 * @param key_len Length of the XOR key.
 * @param offset Stream byte offset to keep the XOR key synchronized.
 * @return int 0 on success, or -1 on decryption failure.
 */
static int decrypt_buffer(uint8_t *buffer, size_t length, const uint8_t *key,
                          size_t key_len, uint64_t offset)
{
    if (aplicar_xor(buffer, length, key, key_len, offset) < 0)
    {
        fprintf(stderr, "[Master Error] XOR decryption failed at offset %lu.\n", offset);
        return -1;
    }
    return 0;
}

/**
 * @brief Reads, decrypts, and reconstructs the incoming WAV stream from the client.
 *
 * @param client_fd Connected client socket descriptor.
 * @param key XOR encryption key bytes.
 * @param key_len Length of the XOR key.
 * @param out_encrypted_buffer Pointer to the buffer pointer that will be dynamically allocated.
 * @param out_size Pointer to store the received file size in bytes.
 * @return int 0 on success, or -1 on error.
 */
static int read_audio_stream(int client_fd, const uint8_t *key, size_t key_len,
                             uint8_t **out_encrypted_buffer, uint64_t *out_size)
{
    uint8_t header[HEADER_SIZE];
    uint64_t audio_file_size = 0;
    uint8_t *audio_encrypted_buffer = NULL;

    // 3.1 Receive the size of the incoming audio file
    if (receive_exact_bytes(client_fd, (char *)header, HEADER_SIZE) < 0)
    {
        fprintf(stderr, "[Master Error] Failed to receive encrypted size header.\n");
        return -1;
    }

    // 3.2 Decrypt the received file size
    if (decrypt_buffer(header, HEADER_SIZE, key, key_len, 0) < 0)
    {
        fprintf(stderr, "[Master Error] Invalid or corrupted audio file size.\n");
        return -1;
    }

    // 3.3 Reconstruct the 64-bit size from Big-Endian
    audio_file_size = bytes_to_uint64_be(header);
    if (audio_file_size == 0)
    {
        fprintf(stderr, "[Master Error] Invalid audio file size (0 bytes).\n");
        return -1;
    }

    // 3.4 Allocate memory buffer for the entire audio file
    audio_encrypted_buffer = (uint8_t *)malloc((size_t)audio_file_size);
    if (audio_encrypted_buffer == NULL)
    {
        perror("[Master Error] Memory allocation failed for audio buffer");
        return -1;
    }

    // 3.5 Receive the audio stream into the buffer
    if (receive_exact_bytes(client_fd, (char *)audio_encrypted_buffer, (size_t)audio_file_size) < 0)
    {
        free(audio_encrypted_buffer);
        return -1;
    }

    *out_encrypted_buffer = audio_encrypted_buffer;
    *out_size = audio_file_size;
    return 0;
}

/**
 * @brief Calculates chunk sizes, buffer displacements, and global XOR offsets for workers.
 *
 * @param total_size Total size of the audio payload in bytes.
 * @param num_workers Number of worker processes.
 * @param out_sendcounts Pointer to store the allocated array of chunk sizes.
 * @param out_displs Pointer to store the allocated array of buffer displacements.
 * @param out_offsets Pointer to store the allocated array of global XOR offsets.
 * @return int 0 on success, or -1 on failure.
 */
static int calculate_fragment_metadata(uint64_t total_size, int num_workers,
                                       int **out_sendcounts, int **out_displs, uint64_t **out_offsets)
{
    if (num_workers <= 0 || total_size == 0)
    {
        return -1;
    }

    int *sendcounts = (int *)malloc(num_workers * sizeof(int));
    int *displs = (int *)malloc(num_workers * sizeof(int));
    uint64_t *offsets = (uint64_t *)malloc(num_workers * sizeof(uint64_t));

    if (!sendcounts || !displs || !offsets)
    {
        free(sendcounts);
        free(displs);
        free(offsets);
        return -1;
    }

    uint64_t base_chunk = total_size / num_workers;
    uint64_t remainder = total_size % num_workers;
    uint64_t current_offset = HEADER_SIZE; // Empieza en 8 (después de la cabecera de tamaño)

    for (int i = 0; i < num_workers; i++)
    {
        // El último worker absorbe cualquier byte sobrante de la división entera
        sendcounts[i] = (int)(base_chunk + (i == num_workers - 1 ? remainder : 0));

        // Desplazamiento relativo respecto al inicio del búfer de audio (en memoria del master)
        displs[i] = (int)(current_offset - HEADER_SIZE);

        // Offset global real en el flujo del archivo para mantener sincronizada la clave XOR
        offsets[i] = current_offset;

        current_offset += sendcounts[i];
    }

    *out_sendcounts = sendcounts;
    *out_displs = displs;
    *out_offsets = offsets;
    return 0;
}

void run_master_node(int size)
{
    int server_fd, client_fd;
    uint8_t *audio_buffer = NULL;
    uint64_t audio_file_size = 0;
    int *sendcounts = NULL;
    int *displs = NULL;
    uint64_t *offsets = NULL;

    // 1. Retrieve XOR key
    const char *xor_key = getenv("AUDIO_XOR_KEY");
    if (xor_key == NULL || *xor_key == '\0')
    {
        fprintf(stderr, "[Master Error] Environment variable AUDIO_XOR_KEY is not set.\n");
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    // 2. Initialize the TCP server
    server_fd = init_tcp_server(PORT);
    if (server_fd < 0)
    {
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    // 3. Accept incoming connection from client
    client_fd = accept_client_connection(server_fd);
    if (client_fd < 0)
    {
        close(server_fd);
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    // 4. Read audio stream from client
    if (read_audio_stream(client_fd, (const uint8_t *)xor_key, strlen(xor_key),
                          &audio_buffer, &audio_file_size) < 0)
    {
        close(client_fd);
        close(server_fd);
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    // 5. Fragment audio stream in chunk
    if (calculate_fragment_metadata(audio_file_size, size, &sendcounts, &displs, &offsets) < 0)
    {
        fprintf(stderr, "[Master Error] Failed to calculate fragment metadata.\n");
        free(audio_buffer);
        close(client_fd);
        close(server_fd);
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    // 6. Distribute work among the workers
    // TODO: Implement MPI_Scatter / distribution logic here.

    // 7. Clean up resources
    free(audio_buffer);
    close(client_fd);
    close(server_fd);
}
