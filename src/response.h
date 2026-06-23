#ifndef RESPONSE_H
#define RESPONSE_H

#include <stddef.h>

void send_status_response(int fd, int code, const char *reason,
                           const char *body, int keep_alive);
                           
void send_file_response(int fd, const char *filepath, int keep_alive);

const char *mime_type_for(const char *path);

#endif