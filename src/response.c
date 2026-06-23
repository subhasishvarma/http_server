#include "response.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/sendfile.h> // Required for zero-copy file transfers on Linux

const char *mime_type_for(const char *path) {
    const char *ext = strrchr(path, '.');
    if (!ext) return "application/octet-stream";
    if (strcmp(ext, ".html") == 0) return "text/html";
    if (strcmp(ext, ".css")  == 0) return "text/css";
    if (strcmp(ext, ".js")   == 0) return "application/javascript";
    if (strcmp(ext, ".json") == 0) return "application/json";
    if (strcmp(ext, ".png")  == 0) return "image/png";
    if (strcmp(ext, ".jpg")  == 0 || strcmp(ext, ".jpeg") == 0) return "image/jpeg";
    if (strcmp(ext, ".txt")  == 0) return "text/plain";
    return "application/octet-stream";
}

void send_status_response(int fd, int code, const char *reason,
                           const char *body, int keep_alive) {
    char header[512];
    size_t body_len = body ? strlen(body) : 0;
    
    // snprintf is safe and prevents buffer overflows when building strings
    int n = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Length: %zu\r\n"
        "Content-Type: text/plain\r\n"
        "Connection: %s\r\n"
        "\r\n",
        code, reason, body_len, keep_alive ? "keep-alive" : "close");
        
    write(fd, header, n);
    if (body_len) write(fd, body, body_len);
}

void send_file_response(int fd, const char *filepath, int keep_alive) {
    struct stat st;
    int file_fd = open(filepath, O_RDONLY);
    
    // Check if file exists, can be stat-ed, and is NOT a directory
    if (file_fd < 0 || fstat(file_fd, &st) < 0 || S_ISDIR(st.st_mode)) {
        if (file_fd >= 0) close(file_fd);
        send_status_response(fd, 404, "Not Found", "404 Not Found", keep_alive);
        return;
    }

    char header[512];
    int n = snprintf(header, sizeof(header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: %ld\r\n"
        "Content-Type: %s\r\n"
        "Connection: %s\r\n"
        "\r\n",
        (long)st.st_size, mime_type_for(filepath), keep_alive ? "keep-alive" : "close");
        
    write(fd, header, n);

    // sendfile() provides zero-copy transfer directly inside the kernel
    off_t offset = 0;
    ssize_t sent;
    while (offset < st.st_size) {
        sent = sendfile(fd, file_fd, &offset, st.st_size - offset);
        if (sent <= 0) break; // Error or client disconnected
    }
    close(file_fd);
}