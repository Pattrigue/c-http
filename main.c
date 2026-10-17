#include <err.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define PORT 8080
#define BACKLOG 10
#define BUFFER_SIZE 4096 // bytes

int main(void) {
    // By default, sending to a client that has disconnected kills the whole
    // server (SIGPIPE). Ignore that signal so send() just returns an error.
    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
        err(EXIT_FAILURE, "Failed to ignore SIGPIPE");
    }

    // Create the server socket
    int server_fd = socket(AF_INET6, SOCK_STREAM, 0);

    if (server_fd == -1) {
        err(EXIT_FAILURE, "Socket creation failed");
    }

    // Disable IPv6 only so IPv4 is also accepted
    const int v6only = 0;
    if (setsockopt(
            server_fd,
            IPPROTO_IPV6,
            IPV6_V6ONLY,
            &v6only,
            sizeof(v6only)
        ) == -1) {
        err(EXIT_FAILURE, "Failed to set IPv6 only policy");
    }

    // Allow reuse of socket address
    const int reuse_socket_addr = 1;
    if (setsockopt(
            server_fd,
            SOL_SOCKET,
            SO_REUSEADDR,
            &reuse_socket_addr,
            sizeof(reuse_socket_addr)
        ) == -1) {
        err(EXIT_FAILURE, "Failed to set socket reuse policy");
    }

    // Create the host address
    struct sockaddr_in6 server_addr = {
        .sin6_family = AF_INET6,
        .sin6_port = htons(PORT),
        .sin6_addr = in6addr_any
    };

    // Bind the socket to the host address
    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) ==
        -1) {
        err(EXIT_FAILURE, "Bind failed");
    }

    // Listen for connections on the socket
    if (listen(server_fd, BACKLOG) == -1) {
        err(EXIT_FAILURE, "Listen failed");
    }

    printf("Listening on port %d.\n", PORT);

    while (1) {
        // Accept incoming connection on the socket
        struct sockaddr_in6 client_addr;
        socklen_t client_addr_len = sizeof(client_addr);

        int client_fd = accept(
            server_fd,
            (struct sockaddr *)&client_addr,
            &client_addr_len
        );

        if (client_fd == -1) {
            perror("Accept failed");
            // Nothing to close, client never connected
            continue;
        }

        // Read message from the socket
        char buff[BUFFER_SIZE];
        ssize_t bytes_read = recv(client_fd, buff, BUFFER_SIZE - 1, 0);

        if (bytes_read == -1) {
            perror("Receive failed");
            close(client_fd);
            continue;
        }

        buff[bytes_read] = '\0';
        printf("Read from client:\n%s\n", buff);

        // Return hardcoded HTTP 1.1 response (for now)
        const char *response = "HTTP/1.1 200 OK\r\n"
                               "Content-Length: 0\r\n\r\n";

        if (send(client_fd, response, strlen(response), 0) == -1) {
            perror("Send failed");
        }

        close(client_fd);
    }

    return 0;
}
