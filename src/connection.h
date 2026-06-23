#ifndef CONNECTION_H
#define CONNECTION_H

#include <time.h>
#include <stddef.h>
#include <sys/types.h>

#define READ_BUF_INITIAL 8192

typedef enum { CONN_READING, CONN_WRITING, CONN_CLOSING } conn_state_t;

typedef struct {
    int fd;
    
    char *read_buf;
    size_t read_len;
    size_t read_cap;

    char *write_buf;     
    size_t write_len; // FIXED: size_t, not size_len_t
    size_t write_off;

    int file_fd;         
    off_t file_size;     
    off_t file_offset;   

    conn_state_t state;
    time_t last_active; 
} connection_t;

connection_t *connection_create(int fd);
void connection_destroy(connection_t *c);
void connection_buffer_append(connection_t *c, const char *data, size_t len);
void connection_consume(connection_t *c, size_t n);

#endif