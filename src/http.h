#ifndef HTTP_H
#define HTTP_H

#define PATH_BUF_SIZE 2048
#define REQUEST_BUF_SIZE 4096

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

typedef struct http_request_line {
    http_method method;
    char path[PATH_BUF_SIZE];
    http_version version;
} http_request_line;

http_status http_parse_request(const char *request, http_request_line *out);

const char *http_method_name(http_method method);

const char *http_status_reason(http_status status);

const char *http_version_name(http_version version);

#endif // HTTP_H
