#include <stdio.h>
#include <string.h>
#include "../src/http_parser.h"

int main() {
    // A fake, raw HTTP request string
    const char *raw_req = 
        "GET /search?q=networking HTTP/1.1\r\n"
        "Host: localhost:8080\r\n"
        "Content-Length: 11\r\n"
        "Connection: close\r\n"
        "\r\n"
        "hello world";

    http_request_t req;
    size_t consumed = 0;
    
    printf("Testing parser...\n");
    int result = parse_http_request(raw_req, strlen(raw_req), &req, &consumed);

    if (result == 1) {
        printf("SUCCESS! Parsed a complete request.\n");
        printf("Method: %s\n", req.method);
        printf("Path: %s\n", req.path);
        printf("Query: %s\n", req.query);
        printf("Keep-Alive (0=close, 1=keep-alive): %d\n", req.keep_alive);
        printf("Body length: %zu\n", req.body_len);
        
        // Using %.*s to print exactly body_len bytes, since req.body isn't null-terminated
        printf("Body: %.*s\n", (int)req.body_len, req.body);
        printf("Bytes consumed from buffer: %zu\n", consumed);
    } else if (result == 0) {
        printf("Result: 0 (Incomplete request, need more bytes)\n");
    } else {
        printf("Result: -1 (Malformed request)\n");
    }

    return 0;
}