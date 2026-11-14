// ==========================================
// TCP Socket Server Header
// ==========================================

#ifndef SOCKET_SERVER_H
#define SOCKET_SERVER_H

/**
 * @brief Initializes, binds, and starts listening on a TCP socket.
 *
 * @param port The network port number on which the server will listen for incoming connections.
 * @return The server file descriptor on success, or -1 on failure.
 */
int init_tcp_server(int port);

/**
 * @brief Blocks until a client connects to the server socket.
 *
 * @param server_fd The server socket file descriptor used to accept connections.
 * @return The client file descriptor on success, or -1 on failure.
 */
int accept_client_connection(int server_fd);

/**
 * @brief Receives a fixed amount of bytes from the client socket into a buffer.
 *
 * @param client_fd The connected client socket file descriptor.
 * @param buffer Pointer to the destination memory buffer.
 * @param total_bytes Number of bytes expected to be read.
 * @return int 0 on success, or -1 on failure/disconnection.
 */
int receive_exact_bytes(int client_fd, char *buffer, size_t total_bytes);

#endif // SOCKET_SERVER_H
