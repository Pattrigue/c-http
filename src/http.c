#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "http.h"

#define METHOD_BUFF_SIZE 10

static http_status parse_http_version(const char *version_str, http_version *out);

static http_status parse_request_line(const char *request_line, http_request_line *out);

static http_status parse_method(const char *method_start, const char *method_end, http_method *out);

static http_status
parse_path(const char *path_start, const char *path_end, char *out, size_t out_size);

static bool is_digit(char c);

static bool is_visible_ascii(char c);

static const char *http_method_names[HTTP_METHOD_COUNT] = {
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

http_status http_parse_request(const char *request, http_request_line *out) {
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

const char *http_method_name(http_method method) {
    if (method >= HTTP_METHOD_COUNT) {
        return "Unknown";
    }

    return http_method_names[method];
}

const char *http_status_reason(http_status status) {
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

const char *http_version_name(http_version version) {
    switch (version) {
    case HTTP_VERSION_1_0:
        return "HTTP/1.0";
    case HTTP_VERSION_1_1:
        return "HTTP/1.1";
    }

    return "Unknown";
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
parse_method(const char *method_start, const char *method_end, http_method *out) {
    char method_buff[METHOD_BUFF_SIZE];
    size_t n = method_end - method_start;

    if (n >= METHOD_BUFF_SIZE) {
        return HTTP_STATUS_NOT_IMPLEMENTED;
    }

    memcpy(method_buff, method_start, n);
    method_buff[n] = '\0';

    *out = HTTP_METHOD_COUNT;
    for (int i = 0; i < HTTP_METHOD_COUNT; i++) {
        if (strcmp(method_buff, http_method_names[i]) == 0) {
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

    // Only allow visible ASCII (RFC VCHAR). Rejects control characters and
    // non-ASCII bytes, which also prevents log and terminal injection
    for (size_t i = 0; i < len; i++) {
        if (!is_visible_ascii(path_start[i])) {
            return HTTP_STATUS_BAD_REQUEST;
        }
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

static bool is_visible_ascii(char c) { return c >= 0x21 && c <= 0x7E; }
