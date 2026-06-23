#ifndef HTTP_PARSER_H
#define HTTP_PARSER_H

#include <stddef.h>

#define MAX_HEADERS 64

typedef struct {
    char name[128];
    char value[512];
} http_header_t;

typedef struct {
    char method[16];        // for "GET", "POST", "HEAD", ...methonds
    char path[1024];        // for "/index.html" (raw, before decoding)
    char query[1024];       // part after '?', if any
    char version[16];       // "HTTP/1.1"
    http_header_t headers[MAX_HEADERS];
    int header_count;
    char *body;             
    size_t body_len;
    int keep_alive;        
} http_request_t;

// It will return :
//  1  -> a complete request was parsed; 
//  0  -> incomplete, need more data
// -1  -> malformed request 
int parse_http_request(const char *buf, size_t buf_len,
                        http_request_t *req, size_t *consumed);

#endif