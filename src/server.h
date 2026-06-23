#ifndef SERVER_H
#define SERVER_H

#define PORT 8080
#define BACKLOG 128
#define MAX_CONNS 1024

// Forward declarations
int set_nonblocking(int fd);
int create_listening_socket(int port);
void run_event_loop(int listen_fd);

#endif