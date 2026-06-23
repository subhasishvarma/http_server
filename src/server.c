#include"server.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include<time.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/sendfile.h>
#include "http_parser.h"
#include "response.h"
#include "util.h"
#include "connection.h"

#define PORT 8080
#define BACKLOG 128
#define MAX_CONNS 1024

char www_root[1024];

int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int create_listening_socket(int port) {
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) { perror("socket"); exit(1); }
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    if (bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) { perror("bind"); exit(1); }
    if (listen(listen_fd, BACKLOG) < 0) { perror("listen"); exit(1); }
    return listen_fd;
}

typedef struct {
    struct pollfd pfds[MAX_CONNS + 1];
    connection_t *conns[MAX_CONNS + 1];
    int nfds;
} poll_set_t;

int handle_readable(connection_t *c) {
    char buf[4096];
    ssize_t n = read(c->fd, buf, sizeof(buf));
    
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0; 
        return 1; 
    }
    if (n == 0) return 1; 

    connection_buffer_append(c, buf, n);
    c->last_active = time(NULL); 

    http_request_t req;
    size_t consumed;
    int parse_status = parse_http_request(c->read_buf, c->read_len, &req, &consumed);

    if (parse_status == 1) {
        // 405 Check
        if (strcmp(req.method, "GET") != 0 && strcmp(req.method, "HEAD") != 0) {
            const char *body = "405 Method Not Allowed";
            char header[512];
            int header_len = snprintf(header, sizeof(header),
                "HTTP/1.1 405 Method Not Allowed\r\nContent-Length: %zu\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\n%s",
                strlen(body), body);
            
            c->write_buf = realloc(c->write_buf, c->write_len + header_len);
            memcpy(c->write_buf + c->write_len, header, header_len);
            c->write_len += header_len;
            
            log_request("127.0.0.1", req.method, req.path, 405);
            c->state = CONN_CLOSING;
            return 0;
        }

        // Path Resolution & File Serving
        char decoded_path[1024], safe_path[1024], header[1024];
        url_decode(req.path, decoded_path);
        if (strcmp(decoded_path, "/") == 0) strcpy(decoded_path, "/index.html");

        int header_len = 0;
        int status = 200;

        if (resolve_safe_path(www_root, decoded_path, safe_path, sizeof(safe_path)) == 0) {
            struct stat st;
            int ffd = open(safe_path, O_RDONLY);
            if (ffd >= 0 && fstat(ffd, &st) == 0 && !S_ISDIR(st.st_mode)) {
                c->file_fd = ffd;
                c->file_size = st.st_size;
                c->file_offset = 0;
                header_len = snprintf(header, sizeof(header),
                    "HTTP/1.1 200 OK\r\nContent-Length: %ld\r\nContent-Type: %s\r\nConnection: %s\r\n\r\n",
                    (long)st.st_size, mime_type_for(safe_path), req.keep_alive ? "keep-alive" : "close");
            } else {
                if (ffd >= 0) close(ffd);
                goto send_404;
            }
        } else {
        send_404:
            status = 404;
            const char *body = "404 Not Found";
            header_len = snprintf(header, sizeof(header),
                "HTTP/1.1 404 Not Found\r\nContent-Length: %zu\r\nContent-Type: text/plain\r\nConnection: %s\r\n\r\n%s",
                strlen(body), req.keep_alive ? "keep-alive" : "close", body);
        }

        // Queue Response & Log
        c->write_buf = realloc(c->write_buf, c->write_len + header_len);
        memcpy(c->write_buf + c->write_len, header, header_len);
        c->write_len += header_len;
        
        log_request("127.0.0.1", req.method, req.path, status);

        connection_consume(c, consumed);
        if (!req.keep_alive) c->state = CONN_CLOSING; 
        
    } else if (parse_status == -1) {
        return 1;
    }
    
    return 0; 
}

int handle_writable(connection_t *c) {
    c->last_active = time(NULL);

    // Flush any pending HTTP headers 
    if (c->write_off < c->write_len) {
        ssize_t n = write(c->fd, c->write_buf + c->write_off, c->write_len - c->write_off);
        
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
            return 1; 
        }
        c->write_off += n;
        if (c->write_off == c->write_len) {
            free(c->write_buf);
            c->write_buf = NULL;
            c->write_len = 0;
            c->write_off = 0;
            if (c->file_fd < 0 && c->state == CONN_CLOSING) return 1;
        }
        return 0; 
    }

    // Stream the actual file if it was opened
    if (c->file_fd >= 0 && c->file_offset < c->file_size) {
        ssize_t sent = sendfile(c->fd, c->file_fd, &c->file_offset, c->file_size - c->file_offset);
        if (sent < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
            return 1; 
        }
        if (c->file_offset >= c->file_size) {
            close(c->file_fd);
            c->file_fd = -1;
            if (c->state == CONN_CLOSING) return 1;
        }
    }
    return 0; 
}

void run_event_loop(int listen_fd) {
    poll_set_t ps;
    ps.pfds[0].fd = listen_fd;
    ps.pfds[0].events = POLLIN;
    ps.conns[0] = NULL;
    ps.nfds = 1;
    while (1) {
        int ready = poll(ps.pfds, ps.nfds, 5000);
        if (ready < 0) { if (errno == EINTR) continue; perror("poll"); break; }
        time_t now = time(NULL);
        for (int i = 0; i < ps.nfds; i++) {
            if (ps.pfds[i].fd == listen_fd && (ps.pfds[i].revents & POLLIN)) {
                while (1) {
                    int client_fd = accept(listen_fd, NULL, NULL);
                    if (client_fd < 0) break;
                    set_nonblocking(client_fd);
                    if (ps.nfds <= MAX_CONNS) {
                        connection_t *c = connection_create(client_fd);
                        ps.pfds[ps.nfds].fd = client_fd;
                        ps.pfds[ps.nfds].events = POLLIN;
                        ps.conns[ps.nfds] = c;
                        ps.nfds++;
                    } else close(client_fd);
                }
                continue;
            }
            if (ps.conns[i] == NULL) continue;
            connection_t *c = ps.conns[i];
            int close_conn = 0;
            if (ps.pfds[i].revents & (POLLHUP | POLLERR)) close_conn = 1;
            else if (ps.pfds[i].revents & POLLIN) close_conn = handle_readable(c);
            else if (ps.pfds[i].revents & POLLOUT) close_conn = handle_writable(c);
            if (!close_conn && now - c->last_active > 30) close_conn = 1;
            if (close_conn) {
                connection_destroy(c);
                close(ps.pfds[i].fd);
                ps.pfds[i] = ps.pfds[ps.nfds - 1];
                ps.conns[i] = ps.conns[ps.nfds - 1];
                ps.nfds--;
                i--;
            } else ps.pfds[i].events = (c->write_len > c->write_off || c->file_fd >= 0) ? POLLOUT : POLLIN;
        }
    }
}

int main() {
    getcwd(www_root, sizeof(www_root));
    strncat(www_root, "/www", sizeof(www_root) - strlen(www_root) - 1);
    int listen_fd = create_listening_socket(PORT);
    set_nonblocking(listen_fd);
    printf("Starting high-performance non-blocking event loop...\n");
    run_event_loop(listen_fd);
    close(listen_fd);
    return 0;
}