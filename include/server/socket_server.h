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

#endif // SOCKET_SERVER_H
