#ifndef HTTP_PARSER_H
#define HTTP_PARSER_H

#include <stddef.h>

#define MAX_HEADERS 64

typedef struct {
    char name[128];
    char value[512];
} http_header_t;

typedef struct {
    char method[16];        // "GET", "POST", "HEAD", ...
    char path[1024];        // "/index.html" (raw, before decoding)
    char query[1024];       // part after '?', if any
    char version[16];       // "HTTP/1.1"
    http_header_t headers[MAX_HEADERS];
    int header_count;
    char *body;             // pointer into connection buffer
    size_t body_len;
    int keep_alive;         // derived from version + Connection header
} http_request_t;

// Returns:
//  1  -> a complete request was parsed; *consumed = bytes used from buf
//  0  -> incomplete, need more data
// -1  -> malformed request (caller should respond 400 and close)
int parse_http_request(const char *buf, size_t buf_len,
                        http_request_t *req, size_t *consumed);

#endif