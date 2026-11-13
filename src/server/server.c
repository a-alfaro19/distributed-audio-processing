// ==========================================
// Server / Cluster Master-Worker Logic
// ==========================================

#include <mpi.h>
#include <stdio.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    int rank, size;
    char hostname[256];

    // 1. Initialize MPI execution environment
    MPI_Init(&argc, &argv);

    // 2. Retrieve the rank (ID) of the calling process
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    // 3. Retrieve the total number of processes in the cluster
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    // 4. Retrieve the local host identifier
    gethostname(hostname, sizeof(hostname));

    // 5. Execute node-specific workload based on rank
    if (rank == 0)
    {
        // Master node execution flow
        printf("This is the master node (rank %d) from a cluster of %d nodes running on %s.\n", rank, size, hostname);
    }
    else
    {
        // Worker node execution flow
        printf("This is a worker node (rank %d) waiting for a workload running on %s.\n", rank, hostname);
    }

    // 6. Terminate MPI execution environment
    MPI_Finalize();
    return 0;
}
