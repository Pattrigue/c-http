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

typedef enum http_status {
    HTTP_OK = 200,
    HTTP_BAD_REQUEST = 400,
    HTTP_METHOD_NOT_ALLOWED = 405,
    HTTP_URI_TOO_LONG = 414,
    HTTP_REQUEST_HEADER_FIELDS_TOO_LARGE = 431,
} http_status;

static http_status parse_request(const char *request);
static http_status parse_request_line(const char *request_line);

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

        // Read message from the socket
        char buff[BUFFER_SIZE];
        ssize_t bytes_read = recv(client_fd, buff, BUFFER_SIZE - 1, 0);

        if (bytes_read == -1) {
            perror("Receive failed");
            close(client_fd);
            continue;
        }

        buff[bytes_read] = '\0';
        // printf("Read from client:\n%s\n", buff);

        http_status status = parse_request(buff);

        // Return hardcoded HTTP 1.1 response (for now)
        // TODO: Use status in response
        const char *response = "HTTP/1.1 200 OK\r\n"
                               "Content-Length: 0\r\n\r\n";

        if (send(client_fd, response, strlen(response), 0) == -1) {
            perror("Send failed");
        }

        close(client_fd);
    }

    return 0;
}

static http_status parse_request(const char *request) {
    size_t len = strlen(request);
    size_t char_index = 0;
    int line_num = 0;

    char line_buff[BUFFER_SIZE];

    for (size_t i = 0; i < len; i++) {
        if (request[i] != '\r') {
            continue;
        }

        if (request[i + 1] != '\n') {
            return HTTP_BAD_REQUEST;
        }

        size_t char_count = i - char_index; // exclude \r\n
        if (char_count == 0) {
            // breaks on final \r\n
            break;
        }

        if (char_count >= sizeof line_buff) { // >= to account for the '\0'
            return line_num == 0 ? HTTP_URI_TOO_LONG : HTTP_REQUEST_HEADER_FIELDS_TOO_LARGE;
        }

        memcpy(line_buff, request + char_index, char_count);
        line_buff[char_count] = '\0';

        char_index = i + 2;

        if (line_num == 0) {
            http_status status = parse_request_line(line_buff);

            if (status != HTTP_OK) {
                return status;
            }
        }

        line_num++;
    }

    if (line_num == 0) {
        // No lines parsed
        return HTTP_BAD_REQUEST;
    }

    return HTTP_OK;
}

static http_status parse_request_line(const char *request_line) {
    const char *first_space = strchr(request_line, ' ');
    if (first_space == NULL) {
        return HTTP_BAD_REQUEST;
    }

    const char *second_space = strchr(first_space + 1, ' ');
    if (second_space == NULL) {
        return HTTP_BAD_REQUEST;
    }

    return HTTP_OK;
}
