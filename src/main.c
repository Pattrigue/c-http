#include <asm-generic/socket.h>
#include <err.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "http.h"

#define PORT 8080
#define BACKLOG 10
#define CLIENT_TIMEOUT_SEC 10
#define RESPONSE_BUF_SIZE 1024

typedef enum read_result {
    READ_OK,
    READ_TOO_LARGE,
    READ_CLOSED,
    READ_ERROR,
} read_result;

static void set_server_socket_opts(int server_fd);

static read_result read_request(int fd, char *buf, size_t buf_size);

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

    // Set socket options
    set_server_socket_opts(server_fd);

    // Create the host address
    struct sockaddr_in6 server_addr = {
        .sin6_family = AF_INET6,
        .sin6_port = htons(PORT),
        .sin6_addr = in6addr_any,
    };

    // Bind the socket to the host address
    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
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

        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);

        if (client_fd == -1) {
            perror("Accept failed");
            // Nothing to close, client never connected
            continue;
        }

        // Set client socket timeout
        struct timeval timeout = {.tv_sec = CLIENT_TIMEOUT_SEC};
        if (setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout) == -1) {
            perror("Failed to set socket timeout");
            close(client_fd);
            continue;
        }

        // Read message from the socket
        char buf[REQUEST_BUF_SIZE];
        http_status status;
        http_request_line request_line;
        read_result result = read_request(client_fd, buf, sizeof buf);

        switch (result) {
        case READ_OK:
            status = http_parse_request(buf, &request_line);
            break;
        case READ_ERROR:
            perror("Read failed");
            close(client_fd);
            continue;
        case READ_TOO_LARGE:
            status = HTTP_STATUS_REQUEST_HEADER_FIELDS_TOO_LARGE;
            break;
        case READ_CLOSED:
            close(client_fd);
            continue;
        }

        // printf("Read from client:\n%s\n", buf);

        // Log response
        if (status == HTTP_STATUS_OK) {
            const char *version_name = http_version_name(request_line.version);
            const char *method_name = http_method_name(request_line.method);

            printf(
                "OK: method=%s path=%s version=%s\n",
                method_name,
                request_line.path,
                version_name
            );
        } else {
            printf("FAIL: %d %s\n", status, http_status_reason(status));
        }

        // Return HTTP 1.1 response
        const char *reason = http_status_reason(status);
        const char *allow_header = status == HTTP_STATUS_METHOD_NOT_ALLOWED ? "Allow: GET\r\n" : "";

        char response[RESPONSE_BUF_SIZE];

        int response_len = snprintf(
            response,
            sizeof response,
            "HTTP/1.1 %d %s\r\nContent-Length: 0\r\n%s\r\n",
            status,
            reason,
            allow_header
        );

        if (response_len < 0 || (size_t)response_len >= sizeof response) {
            close(client_fd);
            fprintf(stderr, "Invalid response length: %d\n", response_len);
            continue;
        }

        if (send(client_fd, response, (size_t)response_len, 0) == -1) {
            perror("Send failed");
        }

        close(client_fd);
    }

    return 0;
}

static void set_server_socket_opts(int server_fd) {
    // Disable IPv6 only so IPv4 is also accepted
    const int v6only = 0;
    if (setsockopt(server_fd, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, sizeof(v6only)) == -1) {
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
}

static read_result read_request(int fd, char *buf, size_t buf_size) {
    size_t total_bytes_read = 0;

    while (1) {
        if (total_bytes_read >= buf_size - 1) {
            // prevent buffer overflow
            return READ_TOO_LARGE;
        }

        size_t space_left = buf_size - total_bytes_read - 1;
        ssize_t bytes_read = recv(fd, buf + total_bytes_read, space_left, 0);

        if (bytes_read == 0) {
            // client closed connection
            return READ_CLOSED;
        }

        if (bytes_read == -1) {
            // error
            return READ_ERROR;
        }

        total_bytes_read += (size_t)bytes_read;
        buf[total_bytes_read] = '\0';

        if (strstr(buf, "\r\n\r\n") != NULL) {
            return READ_OK;
        }
    }
}
