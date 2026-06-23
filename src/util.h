#ifndef UTIL_H
#define UTIL_H

#include <stddef.h>

void url_decode(const char *src, char *dest);
int resolve_safe_path(const char *www_root, const char *url_path, char *out, size_t out_size);

#endif