#include "connection.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

connection_t *connection_create(int fd) {
    connection_t *c = malloc(sizeof(connection_t));
    if (!c) return NULL;

    c->fd = fd;
    c->read_cap = READ_BUF_INITIAL;
    c->read_buf = malloc(c->read_cap);
    c->read_len = 0;

    c->write_buf = NULL;
    c->write_len = 0;
    c->write_off = 0;

    c->state = CONN_READING;
    c->last_active = time(NULL);

    return c;
}

void connection_destroy(connection_t *c) {
    if (!c) return;
    if (c->read_buf) free(c->read_buf);
    if (c->write_buf) free(c->write_buf);
    free(c);
}

// Dynamically grows the buffer if a client sends a massive request
void connection_buffer_append(connection_t *c, const char *data, size_t len) {
    if (c->read_len + len > c->read_cap) {
        while (c->read_len + len > c->read_cap) {
            c->read_cap *= 2; 
        }
        c->read_buf = realloc(c->read_buf, c->read_cap);
    }
    memcpy(c->read_buf + c->read_len, data, len);
    c->read_len += len;
}

// Drops bytes from the front of the buffer once the parser has processed them
void connection_consume(connection_t *c, size_t n) {
    if (n >= c->read_len) {
        c->read_len = 0;
    } else {
        // memmove safely handles overlapping memory regions
        memmove(c->read_buf, c->read_buf + n, c->read_len - n);
        c->read_len -= n;
    }
}