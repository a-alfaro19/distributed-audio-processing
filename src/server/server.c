// ==========================================
// Server / Cluster Master-Worker Logic
// ==========================================

#include <mpi.h>
#include <stdio.h>
#include <unistd.h>
#include "master.h"
#include "worker.h"

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

    // 5. Validate minimum cluster size requirement
    if (size < 3)
    {
        if (rank == 0)
        {
            fprintf(stderr, "[Error] The cluster requires at least 3 processes (current: %d).\n", size);
        }
        MPI_Finalize();
        return 1;
    }

    // 6. Execute node-specific workload based on rank
    if (rank == 0)
    {
        // Master node execution flow
        run_master_node(size);
    }
    else
    {
        // Worker node execution flow
        run_worker_node(rank);
    }

    // 7. Terminate MPI execution environment
    MPI_Finalize();
    return 0;
}
