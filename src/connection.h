#ifndef CONNECTION_H
#define CONNECTION_H

#include <time.h>
#include <stddef.h>

#define READ_BUF_INITIAL 8192

typedef enum { CONN_READING, CONN_WRITING, CONN_CLOSING } conn_state_t;

typedef struct {
    int fd;
    
    // The incoming data buffer (growable)
    char *read_buf;
    size_t read_len;
    size_t read_cap;

    // The outgoing data buffer (crucial for Phase 6 non-blocking writes)
    char *write_buf;     
    size_t write_len;
    size_t write_off;

    conn_state_t state;
    time_t last_active; // Used later to drop idle connections
} connection_t;

connection_t *connection_create(int fd);
void connection_destroy(connection_t *c);
void connection_buffer_append(connection_t *c, const char *data, size_t len);
void connection_consume(connection_t *c, size_t n);

#endif