#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include <inttypes.h>
#include "crypto/cifrado.h"
#include "fft_processor.h"

/**
 * @brief Receives the XOR encryption key and its length from the Master node.
 *
 * @param rank The MPI rank ID of the current worker node (used for error logging).
 * @param out_key_len Pointer to store the received key length.
 * @return char* Dynamically allocated string containing the XOR key, or NULL on failure.
 */
char *receive_xor_key(int rank, int *out_key_len)
{
    int key_length = 0;

    // 1. Broadcast the length of the XOR key first
    MPI_Bcast(&key_length, 1, MPI_INT, 0, MPI_COMM_WORLD);

    if (key_length <= 0)
    {
        fprintf(stderr, "[Worker %d Error] Received invalid key length (%d).\n", rank, key_length);
        return NULL;
    }

    // 2. Allocate memory for the key (+1 for safety null-terminator)
    char *xor_key = (char *)malloc(key_length + 1);
    if (!xor_key)
    {
        fprintf(stderr, "[Worker %d Error] Failed to allocate memory for XOR key.\n", rank);
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    // 3. Broadcast the actual key bytes
    MPI_Bcast(xor_key, key_length, MPI_BYTE, 0, MPI_COMM_WORLD);
    xor_key[key_length] = '\0'; // Ensure null-termination

    *out_key_len = key_length;
    return xor_key;
}

/**
 * @brief Receives the chunk size, global XOR offset, and the encrypted audio fragment
 *        from the Master node via MPI Scatter and Scatterv collectives.
 *
 * @param rank The MPI rank ID of the current worker node (used for error logging).
 * @param out_chunk_size Pointer to store the received chunk size in bytes.
 * @param out_global_offset Pointer to store the received global XOR stream offset.
 * @return uint8_t* Dynamically allocated buffer containing the audio fragment, or NULL on failure.
 */
uint8_t *receive_audio_fragment(int rank, int *out_chunk_size, uint64_t *out_global_offset)
{
    int my_chunk_size = 0;
    uint64_t my_global_offset = 0;

    // 1. Receive the assigned chunk size
    MPI_Scatter(NULL, 1, MPI_INT, &my_chunk_size, 1, MPI_INT, 0, MPI_COMM_WORLD);

    // 2. Receive the global XOR stream offset
    MPI_Scatter(NULL, 1, MPI_UINT64_T, &my_global_offset, 1, MPI_UINT64_T, 0, MPI_COMM_WORLD);

    // 3. Allocate local memory for the audio chunk
    uint8_t *my_chunk = (uint8_t *)malloc(my_chunk_size);
    if (!my_chunk)
    {
        fprintf(stderr, "[Worker %d Error] Failed to allocate memory for audio chunk (%d bytes).\n", rank, my_chunk_size);
        return NULL;
    }

    // 4. Receive the actual encrypted audio bytes
    MPI_Scatterv(NULL, NULL, NULL, MPI_BYTE,
                 my_chunk, my_chunk_size, MPI_BYTE,
                 0, MPI_COMM_WORLD);

    *out_chunk_size = my_chunk_size;
    *out_global_offset = my_global_offset;
    return my_chunk;
}

void run_worker_node(int rank)
{
    // 1. Receive XOR key
    int key_length = 0;
    char *xor_key = receive_xor_key(rank, &key_length);
    if (!xor_key)
    {
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    // 2. Receive audio chunk and metadata
    int my_chunk_size = 0;
    uint64_t my_global_offset = 0;
    uint8_t *my_chunk = receive_audio_fragment(rank, &my_chunk_size, &my_global_offset);
    if (!my_chunk)
    {
        free(xor_key);
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    // 3. Decrypt audio chunk
    if (decrypt_buffer(my_chunk, my_chunk_size, (const uint8_t *)xor_key, key_length, my_global_offset) < 0)
    {
        fprintf(stderr, "[Worker %d Error] Failed to decrypt audio chunk.\n", rank);
        free(my_chunk);
        free(xor_key);
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    // 4. Apply FFT to the audio chunks
    if (process_audio_fft(my_chunk, my_chunk_size) < 0)
    {
        fprintf(stderr, "[Worker %d Error] FFT processing failed.\n", rank);
        free(my_chunk);
        free(xor_key);
        MPI_Abort(MPI_COMM_WORLD, 1);
    }
    printf("[Worker %d] ¡Procesamiento FFT completado en el fragmento local!\n", rank);

    // 4. Clean resources
    free(my_chunk);
    free(xor_key);
}
