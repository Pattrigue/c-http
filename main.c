#include <err.h>
#include <limits.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define PORT 8080
#define BACKLOG 10
#define PATH_BUFF_SIZE 2048
#define REQUEST_BUFF_SIZE 4096 // bytes
#define RESPONSE_BUFF_SIZE 1024
#define MAX_METHOD_LEN 10

typedef enum http_status {
    HTTP_STATUS_OK = 200,
    HTTP_STATUS_BAD_REQUEST = 400,
    HTTP_STATUS_METHOD_NOT_ALLOWED = 405,
    HTTP_STATUS_URI_TOO_LONG = 414,
    HTTP_STATUS_REQUEST_HEADER_FIELDS_TOO_LARGE = 431,
    HTTP_STATUS_NOT_IMPLEMENTED = 501,
    HTTP_STATUS_HTTP_VERSION_NOT_SUPPORTED = 505,
} http_status;

typedef enum http_version {
    HTTP_VERSION_1_0,
    HTTP_VERSION_1_1,
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
    char path[PATH_BUFF_SIZE];
    http_version version;
} http_request_line;

static http_status parse_request(const char *request, http_request_line *out);

static http_status parse_request_line(const char *request_line, http_request_line *out);

static http_status parse_method(const char *request_line, const char *method_end, http_method *out);

static http_status
parse_path(const char *path_start, const char *path_end, char *out, size_t out_size);

static http_status parse_http_version(const char *version_str, http_version *out);

static bool is_digit(char c);

static const char *http_status_reason(http_status status);

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
        char buff[REQUEST_BUFF_SIZE];
        ssize_t bytes_read = recv(client_fd, buff, REQUEST_BUFF_SIZE - 1, 0);

        if (bytes_read == -1) {
            perror("Receive failed");
            close(client_fd);
            continue;
        }

        buff[bytes_read] = '\0';
        // printf("Read from client:\n%s\n", buff);

        http_request_line request_line;
        http_status status = parse_request(buff, &request_line);

        // Return HTTP 1.1 response
        const char *reason = http_status_reason(status);
        char response[RESPONSE_BUFF_SIZE];
        int response_len = snprintf(
            response,
            sizeof response,
            "HTTP/1.1 %d %s\r\nContent-Length: 0\r\n\r\n",
            status,
            reason
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

static http_status parse_request(const char *request, http_request_line *out) {
    size_t len = strlen(request);
    size_t char_index = 0;
    int line_num = 0;

    char line_buff[REQUEST_BUFF_SIZE];

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
            http_status status = parse_request_line(line_buff, out);

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
static http_status parse_request_line(const char *request_line, http_request_line *out) {
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
    http_status status = parse_method(request_line, spaces[0], &out->method);

    if (status != HTTP_STATUS_OK) {
        return status;
    }

    if (out->method != HTTP_METHOD_GET) {
        return HTTP_STATUS_METHOD_NOT_ALLOWED;
    }

    // Parse path
    status = parse_path(spaces[0] + 1, spaces[1], out->path, sizeof out->path);
    if (status != HTTP_STATUS_OK) {
        return status;
    }

    // Parse HTTP version
    status = parse_http_version(spaces[1] + 1, &out->version);
    if (status != HTTP_STATUS_OK) {
        return status;
    }

    return HTTP_STATUS_OK;
}

static http_status
parse_method(const char *request_line, const char *method_end, http_method *out) {
    char method_buff[MAX_METHOD_LEN];
    size_t n = method_end - request_line;

    if (n >= MAX_METHOD_LEN) {
        return HTTP_STATUS_NOT_IMPLEMENTED;
    }

    memcpy(method_buff, request_line, n);
    method_buff[n] = '\0';

    *out = HTTP_METHOD_COUNT;
    for (int i = 0; i < HTTP_METHOD_COUNT; i++) {
        if (strcmp(method_buff, method_names[i]) == 0) {
            *out = (http_method)i;
            return HTTP_STATUS_OK;
        }
    }

    return HTTP_STATUS_NOT_IMPLEMENTED;
}

static http_status
parse_path(const char *path_start, const char *path_end, char *out, size_t out_size) {
    if (*path_start != '/') {
        return HTTP_STATUS_BAD_REQUEST;
    }

    size_t len = path_end - path_start;

    if (len >= out_size) {
        return HTTP_STATUS_URI_TOO_LONG;
    }

    memcpy(out, path_start, len);
    out[len] = '\0';

    return HTTP_STATUS_OK;
}

static http_status parse_http_version(const char *version_str, http_version *out) {
    if (strlen(version_str) != 8) {
        return HTTP_STATUS_BAD_REQUEST;
    }

    if (strncmp(version_str, "HTTP/", 5) != 0) {
        return HTTP_STATUS_BAD_REQUEST;
    }

    if (!is_digit(version_str[5]) || !is_digit(version_str[7])) {
        return HTTP_STATUS_BAD_REQUEST;
    }

    if (version_str[6] != '.') {
        return HTTP_STATUS_BAD_REQUEST;
    }

    if (strcmp(version_str, "HTTP/1.0") == 0) {
        *out = HTTP_VERSION_1_0;
        return HTTP_STATUS_OK;
    }

    if (strcmp(version_str, "HTTP/1.1") == 0) {
        *out = HTTP_VERSION_1_1;
        return HTTP_STATUS_OK;
    }

    return HTTP_STATUS_HTTP_VERSION_NOT_SUPPORTED;
}

static bool is_digit(char c) { return c >= '0' && c <= '9'; }

static const char *http_status_reason(http_status status) {
    switch (status) {
    case HTTP_STATUS_OK:
        return "OK";
    case HTTP_STATUS_BAD_REQUEST:
        return "Bad Request";
    case HTTP_STATUS_METHOD_NOT_ALLOWED:
        return "Method Not Allowed";
    case HTTP_STATUS_URI_TOO_LONG:
        return "URI Too Long";
    case HTTP_STATUS_REQUEST_HEADER_FIELDS_TOO_LARGE:
        return "Request Header Fields Too Large";
    case HTTP_STATUS_NOT_IMPLEMENTED:
        return "Not Implemented";
    case HTTP_STATUS_HTTP_VERSION_NOT_SUPPORTED:
        return "HTTP Version Not Supported";
    }

    return "Unknown";
}
