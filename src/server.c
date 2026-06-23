#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include "http_parser.h"
#include "response.h"
#include "util.h"
#include "connection.h" // Phase 5 persistent connection state

#define PORT 8080
#define BACKLOG 128
#define MAX_CONNS 1024

char www_root[1024]; // Made global so our I/O handlers can access it

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

    if (bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind"); exit(1);
    }

    if (listen(listen_fd, BACKLOG) < 0) {
        perror("listen"); exit(1);
    }

    printf("Listening on port %d\n", port);
    return listen_fd;
}

// Struct to hold the massive array of concurrent connections
typedef struct {
    struct pollfd pfds[MAX_CONNS + 1];
    connection_t *conns[MAX_CONNS + 1]; // NULL for the listening socket slot
    int nfds;
} poll_set_t;

// ==========================================
// I/O HANDLERS (WE WILL WRITE THESE NEXT)
// ==========================================

int handle_readable(connection_t *c) {
    char buf[4096];
    
    // 1. Non-blocking read
    ssize_t n = read(c->fd, buf, sizeof(buf));
    
    if (n < 0) {
        // EAGAIN means "I have no data right now, check back later." It is NOT an error.
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0; 
        return 1; // A real error occurred, tell the loop to close the connection
    }
    if (n == 0) return 1; // The client hung up

    // 2. Append the new network bytes to our growable state buffer
    connection_buffer_append(c, buf, n);
    c->last_active = time(NULL); // Reset the 30-second idle timeout clock

    // 3. Check if we have a full HTTP request yet
    http_request_t req;
    size_t consumed;
    int parse_status = parse_http_request(c->read_buf, c->read_len, &req, &consumed);

    if (parse_status == 1) {
        // WE HAVE A FULL REQUEST! 
        // Build a dynamic response string
        const char *body = "<h1>Non-Blocking Phase 6 Works!</h1>";
        char response[2048];
        int resp_len = snprintf(response, sizeof(response),
            "HTTP/1.1 200 OK\r\n"
            "Content-Length: %zu\r\n"
            "Content-Type: text/html\r\n"
            "Connection: %s\r\n"
            "\r\n"
            "%s",
            strlen(body), req.keep_alive ? "keep-alive" : "close", body);

        // 4. QUEUE the response for writing (Do not call write() here!)
        c->write_buf = realloc(c->write_buf, c->write_len + resp_len);
        memcpy(c->write_buf + c->write_len, response, resp_len);
        c->write_len += resp_len;

        // 5. Remove the parsed request from the front of the read buffer
        connection_consume(c, consumed);
        
        // 6. If they asked to close, mark our state machine to drop them AFTER we finish writing
        if (!req.keep_alive) c->state = CONN_CLOSING; 
        
    } else if (parse_status == -1) {
        return 1; // Malformed junk data, drop the connection
    }
    
    return 0; // Tell the event loop to keep monitoring this connection
}

int handle_writable(connection_t *c) {
    if (c->write_len == 0) return 0; // We have nothing queued to say

    // 1. Non-blocking write (only sends what the OS is ready to accept)
    ssize_t n = write(c->fd, c->write_buf + c->write_off, c->write_len - c->write_off);
    
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
        return 1; // Connection broke while writing
    }

    // 2. Track exactly how many bytes we successfully pushed
    c->write_off += n;
    c->last_active = time(NULL);

    // 3. Check if we have flushed our entire queue
    if (c->write_off == c->write_len) {
        // Free the memory to prevent leaks
        free(c->write_buf);
        c->write_buf = NULL;
        c->write_len = 0;
        c->write_off = 0;
        
        // If they requested Connection: close, tell the event loop to hang up now
        if (c->state == CONN_CLOSING) return 1; 
    }

    return 0; 
}

// ==========================================
// THE EVENT LOOP
// ==========================================

void run_event_loop(int listen_fd) {
    poll_set_t ps;
    ps.pfds[0].fd = listen_fd;
    ps.pfds[0].events = POLLIN;
    ps.conns[0] = NULL;
    ps.nfds = 1;

    while (1) {
        int ready = poll(ps.pfds, ps.nfds, 5000); // 5s timeout
        if (ready < 0) { if (errno == EINTR) continue; perror("poll"); break; }

        time_t now = time(NULL);

        for (int i = 0; i < ps.nfds; i++) {
            // 1. Accept new clients
            if (ps.pfds[i].fd == listen_fd && (ps.pfds[i].revents & POLLIN)) {
                // Drain accept loop: pull ALL pending clients off the queue
                while (1) {
                    int client_fd = accept(listen_fd, NULL, NULL);
                    if (client_fd < 0) break; // EAGAIN = queue empty
                    
                    set_nonblocking(client_fd);
                    if (ps.nfds <= MAX_CONNS) {
                        connection_t *c = connection_create(client_fd);
                        ps.pfds[ps.nfds].fd = client_fd;
                        ps.pfds[ps.nfds].events = POLLIN;
                        ps.conns[ps.nfds] = c;
                        ps.nfds++;
                    } else {
                        close(client_fd); // Server full
                    }
                }
                continue;
            }

            // 2. Handle existing clients
            if (ps.conns[i] == NULL) continue;
            connection_t *c = ps.conns[i];
            int close_conn = 0;

            if (ps.pfds[i].revents & (POLLHUP | POLLERR)) {
                close_conn = 1;
            } else if (ps.pfds[i].revents & POLLIN) {
                close_conn = handle_readable(c);
            } else if (ps.pfds[i].revents & POLLOUT) {
                close_conn = handle_writable(c);
            }

            // 3. Sweep idle connections (Slowloris protection)
            if (!close_conn && now - c->last_active > 30) close_conn = 1;

            // 4. Cleanup or Update State
            if (close_conn) {
                connection_destroy(c);
                close(ps.pfds[i].fd);
                ps.pfds[i] = ps.pfds[ps.nfds - 1];
                ps.conns[i] = ps.conns[ps.nfds - 1];
                ps.nfds--;
                i--; 
            } else {
                // IMPORTANT: If we have queued data, tell OS to wake us up when ready to write
                ps.pfds[i].events = (c->write_len > c->write_off) ? POLLOUT : POLLIN;
            }
        }
    }
}

int main() {
    // 1. Setup the web root
    getcwd(www_root, sizeof(www_root));
    strncat(www_root, "/www", sizeof(www_root) - strlen(www_root) - 1);

    // 2. Start listening
    int listen_fd = create_listening_socket(PORT);
    
    // 3. Make the listening socket non-blocking
    set_nonblocking(listen_fd);

    printf("Starting non-blocking event loop...\n");
    
    // 4. Enter the matrix
    run_event_loop(listen_fd);

    close(listen_fd);
    return 0;
}