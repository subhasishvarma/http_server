#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

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

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        // Block until a new client connects, returning a new file descriptor
        int client_fd = accept(listen_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) { perror("accept"); continue; }

        char buf[4096] = {0};
        ssize_t n = read(client_fd, buf, sizeof(buf) - 1);
        if (n > 0) printf("Got %zd bytes:\n%s\n", n, buf);

        const char *resp =
            "HTTP/1.1 200 OK\r\n"
            "Content-Length: 13\r\n"
            "Content-Type: text/plain\r\n"
            "\r\n"
            "Hello, world!";
            
        write(client_fd, resp, strlen(resp));
        close(client_fd);
    }
    close(listen_fd);
    return 0;
}