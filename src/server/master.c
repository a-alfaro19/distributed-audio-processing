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
#include "audio_config.h"
#include "audio_processor.h"
#include "audio_classifier.h"
#include <common/byte_utils.h>
#include <crypto/cifrado.h>

#define PORT 8080
#define HEADER_SIZE 8

/**
 * @brief Sends the audio configuration from the master to all MPI ranks.
 *
 * All ranks must participate in this collective operation by calling the
 * corresponding receive function.
 *
 * @param config Pointer to the configuration loaded by the master.
 * @return MPI_SUCCESS on success, or an MPI error code on failure.
 */
static int send_audio_config(AudioFormatConfig *config)
{
    // 1. Ensure the master has a valid configuration to send.
    if (!config)
    {
        return MPI_ERR_ARG;
    }

    // 2. Broadcast the complete configuration structure as bytes.
    return MPI_Bcast(config, (int)sizeof(*config), MPI_BYTE, 0, MPI_COMM_WORLD);
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

    // 4.1 Receive the size of the incoming audio file
    if (receive_exact_bytes(client_fd, (char *)header, HEADER_SIZE) < 0)
    {
        fprintf(stderr, "[Master Error] Failed to receive encrypted size header.\n");
        return -1;
    }

    // 4.2 Decrypt the received file size
    if (decrypt_buffer(header, HEADER_SIZE, key, key_len, 0) < 0)
    {
        fprintf(stderr, "[Master Error] Invalid or corrupted audio file size.\n");
        return -1;
    }

    // 4.3 Reconstruct the 64-bit size from Big-Endian
    audio_file_size = bytes_to_uint64_be(header);
    if (audio_file_size == 0)
    {
        fprintf(stderr, "[Master Error] Invalid audio file size (0 bytes).\n");
        return -1;
    }

    // 4.4 Allocate memory buffer for the entire audio file
    audio_encrypted_buffer = (uint8_t *)malloc((size_t)audio_file_size);
    if (audio_encrypted_buffer == NULL)
    {
        perror("[Master Error] Memory allocation failed for audio buffer");
        return -1;
    }

    // 4.5 Receive the audio stream into the buffer
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
    // 5.1 Validate input parameters: ensure valid worker count and non-empty payload
    if (num_workers <= 0 || total_size == 0)
    {
        return -1;
    }

    // 5.2 Allocate memory arrays for MPI distribution metadata
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

    // 5.3 Compute base chunk distribution metrics
    uint64_t base_chunk = total_size / num_workers;
    uint64_t remainder = total_size % num_workers;
    uint64_t current_offset = HEADER_SIZE;

    // 5.4 Populate chunk sizes, buffer displacements, and global XOR stream offsets per node
    for (int i = 0; i < num_workers; i++)
    {
        // The last worker absorbs any remaining bytes
        sendcounts[i] = (int)(base_chunk + (i == num_workers - 1 ? remainder : 0));

        displs[i] = (int)(current_offset - HEADER_SIZE);
        offsets[i] = current_offset;

        // Advance the offset for the next node's chunk
        current_offset += sendcounts[i];
    }

    *out_sendcounts = sendcounts;
    *out_displs = displs;
    *out_offsets = offsets;
    return 0;
}

/**
 * @brief Distributes chunk sizes, global XOR offsets, and ciphered audio fragments
 *        to all cluster nodes (including the master) using MPI collective operations.
 *
 * @param audio_buffer Pointer to the full ciphered audio buffer in master's RAM.
 * @param xor_key Pointer to the XOR encryption key string.
 * @param key_len Length of the XOR encryption key in bytes.
 * @param sendcounts Array containing the chunk size for each node.
 * @param displs Array containing the buffer displacements for MPI_Scatterv.
 * @param offsets Array containing the global XOR stream offsets for each node.
 * @param out_my_chunk Pointer to store the allocated local chunk buffer for the master.
 * @param out_my_chunk_size Pointer to store the master's local chunk size.
 * @param out_my_global_offset Pointer to store the master's global XOR offset.
 * @return int 0 on success, or -1 on failure.
 */
static int distribute_audio_work(const uint8_t *audio_buffer, const char *xor_key, size_t key_len,
                                 const int *sendcounts, const int *displs, const uint64_t *offsets,
                                 uint8_t **out_my_chunk, int *out_my_chunk_size, uint64_t *out_my_global_offset)
{
    int my_chunk_size = 0;
    uint64_t my_global_offset = 0;

    // 1. Broadcast the XOR key length and the key itself to all workers
    int key_length = (int)key_len;
    MPI_Bcast(&key_length, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast((void *)xor_key, key_length, MPI_BYTE, 0, MPI_COMM_WORLD);

    // 2. Scatter the chunk sizes so every node knows its assigned size
    MPI_Scatter((void *)sendcounts, 1, MPI_INT, &my_chunk_size, 1, MPI_INT, 0, MPI_COMM_WORLD);

    // 3. Scatter the global XOR offsets so every node knows its cryptographic stream offset
    MPI_Scatter((void *)offsets, 1, MPI_UINT64_T, &my_global_offset, 1, MPI_UINT64_T, 0, MPI_COMM_WORLD);

    // 4. Allocate local memory for this node's chunk
    uint8_t *my_chunk = (uint8_t *)malloc(my_chunk_size);
    if (!my_chunk)
    {
        perror("[Master Error] Failed to allocate memory for local audio chunk");
        return -1;
    }

    // 5. Scatter the physical ciphered audio payload bytes
    MPI_Scatterv((void *)audio_buffer, (int *)sendcounts, (int *)displs, MPI_BYTE,
                 my_chunk, my_chunk_size, MPI_BYTE,
                 0, MPI_COMM_WORLD);

    *out_my_chunk = my_chunk;
    *out_my_chunk_size = my_chunk_size;
    *out_my_global_offset = my_global_offset;
    return 0;
}

/**
 * @brief Collects AudioFrameResult structures from all ranks in MPI_COMM_WORLD
 *        into a sequentially ordered array on the master process (rank 0).
 *
 * @param local_result Pointer to the master rank's own computed AudioFrameResult.
 * @param cluster_size Total number of MPI processes in MPI_COMM_WORLD.
 * @param out_results Output pointer receiving a heap-allocated array of length
 *                    world_size ordered by rank [0 .. world_size - 1].
 * @return int MPI_SUCCESS on success, or an MPI error code on failure.
 */
int gather_audio_results(const AudioFrameResult *local_result,
                         int cluster_size,
                         AudioFrameResult **out_results)
{
    int rank = 0;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    // 1. Validate arguments and ensure this function is only invoked on rank 0.
    if (rank != 0 || !local_result || cluster_size <= 0 || !out_results)
    {
        return MPI_ERR_ARG;
    }

    // 2. Allocate contiguous array to store one AudioFrameResult per rank.
    AudioFrameResult *gathered = (AudioFrameResult *)calloc((size_t)cluster_size, sizeof(AudioFrameResult));
    if (!gathered)
    {
        return MPI_ERR_NO_MEM;
    }

    // 3. Gather raw struct bytes from all ranks into rank 0 in sequential rank order.
    int err = MPI_Gather(local_result, (int)sizeof(*local_result), MPI_BYTE,
                         gathered, (int)sizeof(*local_result), MPI_BYTE,
                         0, MPI_COMM_WORLD);
    if (err != MPI_SUCCESS)
    {
        free(gathered);
        return err;
    }

    *out_results = gathered;
    return MPI_SUCCESS;
}

void run_master_node(int size)
{
    // 1. Load audio processor configuration and broadcast to workers
    AudioFormatConfig audio_cfg;
    if (load_audio_config(&audio_cfg) < 0)
    {
        fprintf(stderr, "[Master Error] Failed to load audio configuration.\n");
        MPI_Abort(MPI_COMM_WORLD, 1);
    }
    if (send_audio_config(&audio_cfg) != MPI_SUCCESS)
    {
        fprintf(stderr, "[Master Error] Failed to broadcast audio configuration.\n");
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    // 2. Retrieve XOR key
    const char *xor_key = getenv("AUDIO_XOR_KEY");
    if (xor_key == NULL || *xor_key == '\0')
    {
        fprintf(stderr, "[Master Error] Environment variable AUDIO_XOR_KEY is not set.\n");
        MPI_Abort(MPI_COMM_WORLD, 1);
    }
    size_t key_len = strlen(xor_key);

    // 3. Initialize the TCP server
    int server_fd, client_fd;
    server_fd = init_tcp_server(PORT);
    if (server_fd < 0)
    {
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    // 4. Accept incoming connection from client
    client_fd = accept_client_connection(server_fd);
    if (client_fd < 0)
    {
        close(server_fd);
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    // 5. Read audio stream from client
    uint8_t *audio_buffer = NULL;
    uint64_t audio_file_size = 0;
    if (read_audio_stream(client_fd, (const uint8_t *)xor_key, key_len,
                          &audio_buffer, &audio_file_size) < 0)
    {
        close(client_fd);
        close(server_fd);
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    // 6. Fragment audio stream in chunk
    int *sendcounts = NULL;
    int *displs = NULL;
    uint64_t *offsets = NULL;
    if (calculate_fragment_metadata(audio_file_size, size, &sendcounts, &displs, &offsets) < 0)
    {
        fprintf(stderr, "[Master Error] Failed to calculate fragment metadata.\n");
        free(audio_buffer);
        close(client_fd);
        close(server_fd);
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    // 7. Distribute work among the workers
    uint8_t *my_chunk = NULL;
    int my_chunk_size = 0;
    uint64_t my_global_offset = 0;
    if (distribute_audio_work(audio_buffer, xor_key, key_len, sendcounts, displs, offsets,
                              &my_chunk, &my_chunk_size, &my_global_offset) < 0)
    {
        fprintf(stderr, "[Master Error] Failed to distribute audio work.\n");
        free(sendcounts);
        free(displs);
        free(offsets);
        free(audio_buffer);
        close(client_fd);
        close(server_fd);
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    // 8 Decrypt audio chunk
    if (decrypt_buffer(my_chunk, my_chunk_size, (const uint8_t *)xor_key, key_len, my_global_offset) < 0)
    {
        fprintf(stderr, "[Master Error] Failed to decrypt audio chunk.\n");
        free(my_chunk);
        free(xor_key);
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    // 9 Run FFT, 5-band LED mapping, and BPM estimation
    AudioFrameResult local_results = {0};
    if (process_audio_chunk(my_chunk, (size_t)my_chunk_size, &audio_cfg, &local_results) < 0)
    {
        fprintf(stderr, "[Master Error] Audio processing failed.\n");
        free(my_chunk);
        free(xor_key);
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    // 10. Gather results from workers
    AudioFrameResult *all_results = (AudioFrameResult *)calloc((size_t)size, sizeof(AudioFrameResult));
    if (!all_results)
    {
        fprintf(stderr, "[Master Error] Failed to allocate gathered results array.\n");
        free(my_chunk);
        free(xor_key);
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    if (gather_audio_results(&local_results, size, &all_results) != MPI_SUCCESS)
    {
        fprintf(stderr, "[Master Error] MPI_Gather failed while collecting results.\n");
        free(all_results);
        free(my_chunk);
        free(xor_key);
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    // 11. Clasify audio and calculate stats
    GlobalAudioStats global_stats;
    if (consolidate_audio_stats(all_results, size, &audio_cfg, &global_stats) == 0)
    {
        printf("[Master Summary] Class: %s | BPM: %.1f | RMS: %.4f | Centroid: %.1f Hz | "
               "Refresh: %.1f ms | Decay: %.2f\n",
               global_stats.label,
               global_stats.global_bpm,
               global_stats.weighted_overall_rms,
               global_stats.weighted_spectral_centroid_hz,
               global_stats.refresh_interval_ms,
               global_stats.decay_per_step);
    }

    // 10. Clean up resources
    free(all_results);
    free(my_chunk);
    free(sendcounts);
    free(displs);
    free(offsets);
    free(audio_buffer);
    close(client_fd);
    close(server_fd);
}
