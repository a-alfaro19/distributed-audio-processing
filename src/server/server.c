// ==========================================
// Server / Cluster Master-Worker Logic
// ==========================================

#include <mpi.h>
#include <stdio.h>
#include <unistd.h>
#include "socket_server.h"

#define PORT 8080

void run_master_node(int size)
{
    int server_fd, client_fd;

    // 1. Initialize the TCP server
    server_fd = init_tcp_server(PORT);
    if (server_fd < 0)
    {
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    // 2. Accept incoming connection from client
    client_fd = accept_client_connection(server_fd);
    if (client_fd < 0)
    {
        close(server_fd);
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    // 3. Read audio stream from client
    // TODO: Implement this part.

    // 4. Distribute work among the workers
    // TODO: Implement this part.

    // 5. Clean up connection descriptors
    close(client_fd);
    close(server_fd);
}

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
        printf("This is a worker node (rank %d) waiting for a workload running on %s.\n", rank, hostname);
    }

    // 7. Terminate MPI execution environment
    MPI_Finalize();
    return 0;
}
