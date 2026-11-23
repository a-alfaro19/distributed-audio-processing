// ==========================================
// Worker Node Control Logic Header
// ==========================================

#ifndef WORKER_H
#define WORKER_H

/**
 * @brief Main execution routine for cluster worker nodes (Rank > 0).
 *        Receives the XOR key, fragment size, global stream offset, and
 *        ciphered audio chunk from the Master via MPI collectives, then
 *        decrypts the audio block in parallel.
 *
 * @param rank The MPI rank ID of the current worker node.
 */
void run_worker_node(int rank);

#endif /* WORKER_H */
