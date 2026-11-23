// ==========================================
// Master Node Control Logic Header
// ==========================================

#ifndef MASTER_H
#define MASTER_H

/**
 * @brief Executes the main control flow for the Master node (rank 0).
 *
 * Initializes the TCP server, receives the incoming audio stream,
 * distributes tasks across worker nodes via MPI, and cleans up resources.
 *
 * @param size Total number of MPI processes in the cluster.
 */
void run_master_node(int size);

#endif // MASTER_H
