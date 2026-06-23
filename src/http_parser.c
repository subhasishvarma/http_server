#include "http_parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// Find "\r\n\r\n" — end of headers. Returns offset of byte AFTER it, or -1.
static long find_header_end(const char *buf, size_t len) {
    for (size_t i = 0; i + 3 < len; i++) {
        if (buf[i]=='\r' && buf[i+1]=='\n' && buf[i+2]=='\r' && buf[i+3]=='\n')
            return (long)(i + 4);
    }
    return -1;
}

static char *next_line(char **cursor, char *end) {
    char *start = *cursor;
    if (start >= end) return NULL;
    char *p = start;
    while (p < end - 1 && !(p[0]=='\r' && p[1]=='\n')) p++;
    *p = '\0';            // terminate this line
    *cursor = p + 2;      // skip CRLF
    return start;
}

static void to_lower(char *s) { for (; *s; s++) *s = tolower((unsigned char)*s); }

int parse_http_request(const char *buf, size_t buf_len,
                        http_request_t *req, size_t *consumed) {
    long header_end = find_header_end(buf, buf_len);
    if (header_end < 0) return 0;  // need more bytes

    // Work on a mutable copy of the header section only
    size_t hdr_len = (size_t)header_end;
    char *copy = malloc(hdr_len + 1);
    memcpy(copy, buf, hdr_len);
    copy[hdr_len] = '\0';

    char *cursor = copy;
    char *end = copy + hdr_len;

    char *request_line = next_line(&cursor, end);
    if (!request_line) { free(copy); return -1; }

    char method[16], target[1024], version[16];
    // Security note: field width limits (%15s, %1023s) prevent buffer overflows
    if (sscanf(request_line, "%15s %1023s %15s", method, target, version) != 3) {
        free(copy); return -1;
    }
    strncpy(req->method, method, sizeof(req->method)-1);
    strncpy(req->version, version, sizeof(req->version)-1);

    // Split target into path + query at '?'
    char *q = strchr(target, '?');
    if (q) {
        *q = '\0';
        strncpy(req->query, q + 1, sizeof(req->query)-1);
    } else {
        req->query[0] = '\0';
    }
    strncpy(req->path, target, sizeof(req->path)-1);

    req->header_count = 0;
    long content_length = -1;
    int chunked = 0;
    int connection_close = 0;

    char *line;
    while ((line = next_line(&cursor, end)) != NULL && *line != '\0') {
        char *colon = strchr(line, ':');
        if (!colon) { free(copy); return -1; }   // malformed header
        *colon = '\0';
        char *name = line;
        char *value = colon + 1;
        while (*value == ' ') value++;           // skip leading space

        if (req->header_count < MAX_HEADERS) {
            strncpy(req->headers[req->header_count].name, name, 127);
            strncpy(req->headers[req->header_count].value, value, 511);
            req->header_count++;
        }

        // Headers are case-insensitive per RFC
        char lname[128];
        strncpy(lname, name, 127); lname[127] = '\0';
        to_lower(lname);

        if (strcmp(lname, "content-length") == 0) {
            content_length = atol(value);
        } else if (strcmp(lname, "transfer-encoding") == 0) {
            char lval[64]; strncpy(lval, value, 63); lval[63]=0; to_lower(lval);
            if (strstr(lval, "chunked")) chunked = 1;
        } else if (strcmp(lname, "connection") == 0) {
            char lval[64]; strncpy(lval, value, 63); lval[63]=0; to_lower(lval);
            if (strstr(lval, "close")) connection_close = 1;
        }
    }
    free(copy);

    // Spec violation: Content-Length and Transfer-Encoding are mutually exclusive
    if (content_length >= 0 && chunked) return -1;  

    // Determine keep-alive default based on HTTP version
    req->keep_alive = (strcmp(req->version, "HTTP/1.0") != 0) && !connection_close;

    size_t body_len = content_length > 0 ? (size_t)content_length : 0;
    if (buf_len < (size_t)header_end + body_len) return 0;  // Body hasn't fully arrived

    req->body = (char *)(buf + header_end);
    req->body_len = body_len;
    *consumed = (size_t)header_end + body_len;
    return 1;
}