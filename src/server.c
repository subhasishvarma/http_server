#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include "http_parser.h"
#include "response.h"
#include "util.h"

#define PORT 8080
#define BACKLOG 128

int create_listening_socket(int port) {
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) { perror("socket"); exit(1); }

    int opt = 1;
    // Avoids "address already in use" when restarting server quickly
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;   // listen on all interfaces
    addr.sin_port = htons(port);         // host-to-network short

    if (bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind"); exit(1);
    }

    if (listen(listen_fd, BACKLOG) < 0) {
        perror("listen"); exit(1);
    }

    printf("Listening on port %d\n", port);
    return listen_fd;
}

int main() {
    int listen_fd = create_listening_socket(PORT);

    // Grab the absolute path to your www/ directory
    char www_root[1024];
    getcwd(www_root, sizeof(www_root));
    strncat(www_root, "/www", sizeof(www_root) - strlen(www_root) - 1);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(listen_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) { perror("accept"); continue; }

        char buf[8192] = {0};
        ssize_t n = read(client_fd, buf, sizeof(buf) - 1);
        if (n <= 0) { close(client_fd); continue; }

        http_request_t req;
        size_t consumed;
        int parse_status = parse_http_request(buf, n, &req, &consumed);

        if (parse_status == -1) {
            send_status_response(client_fd, 400, "Bad Request", "400 Bad Request", 0);
        } else if (parse_status == 1) {
            // Only handle GET and HEAD methods for static files
            if (strcmp(req.method, "GET") != 0 && strcmp(req.method, "HEAD") != 0) {
                send_status_response(client_fd, 405, "Method Not Allowed", "405 Method Not Allowed", 0);
            } else {
                char decoded_path[1024];
                url_decode(req.path, decoded_path);

                // Default routing: '/' becomes '/index.html'
                if (strcmp(decoded_path, "/") == 0) {
                    strcpy(decoded_path, "/index.html");
                }

                char safe_path[1024];
                if (resolve_safe_path(www_root, decoded_path, safe_path, sizeof(safe_path)) < 0) {
                    send_status_response(client_fd, 404, "Not Found", "404 Not Found", 0);
                } else {
                    printf("Serving file: %s\n", safe_path);
                    send_file_response(client_fd, safe_path, 0); // 0 means close connection (we haven't built keep-alive yet)
                }
            }
        }
        
        // In this blocking phase, we close the socket after one response
        close(client_fd);
    }
    close(listen_fd);
    return 0;
}