// ==========================================
// TCP Socket Server Implementation
// ==========================================

#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "socket_server.h"

#define BACKLOG 1 // Maximum pending connections

int init_tcp_server(int port)
{
    int server_fd;
    struct sockaddr_in address;
    int opt = 1;

    // 1. Create server socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0)
    {
        perror("[SocketError] Socket creation failed");
        return -1;
    }

    // 2. Attach socket to the port to prevent 'Address already in use' errors
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)))
    {
        perror("[SocketError] setsockopt failed");
        close(server_fd);
        return -1;
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    // 3. Bind socket to port
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0)
    {
        perror("[SocketError] Bind failed");
        close(server_fd);
        return -1;
    }

    // 3. Start listening
    if (listen(server_fd, BACKLOG) < 0)
    {
        perror("[SocketError] Listen failed");
        close(server_fd);
        return -1;
    }

    return server_fd;
}

int accept_client_connection(int server_fd)
{
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    int new_socket;

    printf("[SocketServer] Waiting for incoming client connection...\n");
    if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen)) < 0)
    {
        perror("[SocketError] Accept failed");
        return -1;
    }

    printf("[SocketServer] Client successfully connected!\n");
    return new_socket;
}

int receive_exact_bytes(int client_fd, char *buffer, size_t total_bytes)
{
    size_t bytes_received = 0;
    ssize_t result;

    while (bytes_received < total_bytes)
    {
        result = recv(client_fd, buffer + bytes_received, total_bytes - bytes_received, 0);

        if (result < 0)
        {
            perror("[SocketError] Error reading from client socket");
            return -1;
        }
        else if (result == 0)
        {
            fprintf(stderr, "[SocketError] Client disconnected prematurely before sending all data.\n");
            return -1;
        }

        bytes_received += result;
    }

    printf("[SocketServer] Successfully received %zu bytes of audio.\n", bytes_received);
    return 0;
}
