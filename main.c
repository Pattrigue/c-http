#include <err.h>
#include <limits.h>
#include <netinet/in.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <wchar.h>

#define PORT 8080
#define BACKLOG 10
#define BUFFER_SIZE 4096 // bytes

typedef enum http_status {
    HTTP_STATUS_OK = 200,
    HTTP_STATUS_BAD_REQUEST = 400,
    HTTP_STATUS_METHOD_NOT_ALLOWED = 405,
    HTTP_STATUS_URI_TOO_LONG = 414,
    HTTP_STATUS_REQUEST_HEADER_FIELDS_TOO_LARGE = 431,
    HTTP_STATUS_NOT_IMPLEMENTED = 501,
} http_status;

typedef enum http_version {
    HTTP_1_0,
    HTTP_1_1,
    HTTP_2_0,
} http_version;

typedef enum http_method {
    HTTP_METHOD_GET,
    HTTP_METHOD_QUERY,
    HTTP_METHOD_HEAD,
    HTTP_METHOD_POST,
    HTTP_METHOD_PUT,
    HTTP_METHOD_DELETE,
    HTTP_METHOD_CONNECT,
    HTTP_METHOD_OPTIONS,
    HTTP_METHOD_TRACE,
    HTTP_METHOD_PATCH,
    HTTP_METHOD_COUNT, // last item to give the enum a count, not an actual HTTP method
} http_method;

static const char *method_names[HTTP_METHOD_COUNT] = {
    [HTTP_METHOD_GET] = "GET",
    [HTTP_METHOD_QUERY] = "QUERY",
    [HTTP_METHOD_HEAD] = "HEAD",
    [HTTP_METHOD_POST] = "POST",
    [HTTP_METHOD_PUT] = "PUT",
    [HTTP_METHOD_DELETE] = "DELETE",
    [HTTP_METHOD_CONNECT] = "CONNECT",
    [HTTP_METHOD_OPTIONS] = "OPTIONS",
    [HTTP_METHOD_TRACE] = "TRACE",
    [HTTP_METHOD_PATCH] = "PATCH",
};

typedef struct http_request_line {
    http_method method;
    char *path;
    http_version version;
} http_request_line;

static http_status parse_request(const char *request);
static http_status parse_request_line(const char *request_line);
static http_status
parse_method(const char *request_line, const char *method_end, http_method *method);

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
            return HTTP_STATUS_BAD_REQUEST;
        }

        size_t char_count = i - char_index; // exclude \r\n
        if (char_count == 0) {
            // breaks on final \r\n
            break;
        }

        if (char_count >= sizeof line_buff) { // >= to account for the '\0'
            return line_num == 0 ? HTTP_STATUS_URI_TOO_LONG
                                 : HTTP_STATUS_REQUEST_HEADER_FIELDS_TOO_LARGE;
        }

        memcpy(line_buff, request + char_index, char_count);
        line_buff[char_count] = '\0';

        char_index = i + 2;

        if (line_num == 0) {
            http_status status = parse_request_line(line_buff);

            if (status != HTTP_STATUS_OK) {
                return status;
            }
        }

        line_num++;
    }

    if (line_num == 0) {
        // No lines parsed
        return HTTP_STATUS_BAD_REQUEST;
    }

    return HTTP_STATUS_OK;
}

// Parse a request line like "GET /index.html HTTP/1.1"
static http_status parse_request_line(const char *request_line) {
    size_t request_line_len = strlen(request_line);
    int space_count = 0;

    const char *spaces[2];

    for (size_t i = 0; i < request_line_len; i++) {
        if (request_line[i] != ' ') {
            continue;
        }

        // No space at the start
        if (i == 0) {
            return HTTP_STATUS_BAD_REQUEST;
        }

        // No space at the end
        if (i == request_line_len - 1) {
            return HTTP_STATUS_BAD_REQUEST;
        }

        // No two spaces in a row
        if (request_line[i - 1] == ' ') {
            return HTTP_STATUS_BAD_REQUEST;
        }

        // Only two spaces allowed
        if (space_count >= 2) {
            return HTTP_STATUS_BAD_REQUEST;
        }

        spaces[space_count++] = &request_line[i];
    }

    if (space_count != 2) {
        return HTTP_STATUS_BAD_REQUEST;
    }

    // Parse method
    http_method method = HTTP_METHOD_COUNT;
    http_status status = parse_method(request_line, spaces[0], &method);

    if (status != HTTP_STATUS_OK) {
        return status;
    }

    if (method != HTTP_METHOD_GET) {
        return HTTP_STATUS_METHOD_NOT_ALLOWED;
    }

    // Parse path

    return HTTP_STATUS_OK;
}

static http_status
parse_method(const char *request_line, const char *method_end, http_method *method) {
    char method_buff[10];
    size_t buff_size = sizeof method_buff;
    size_t n = method_end - request_line;

    if (n >= buff_size) {
        return HTTP_STATUS_NOT_IMPLEMENTED;
    }

    memcpy(method_buff, request_line, n);
    method_buff[n] = '\0';

    *method = HTTP_METHOD_COUNT;
    for (int i = 0; i < HTTP_METHOD_COUNT; i++) {
        if (strcmp(method_buff, method_names[i]) == 0) {
            *method = (http_method)i;
            return HTTP_STATUS_OK;
        }
    }

    return HTTP_STATUS_NOT_IMPLEMENTED;
}
