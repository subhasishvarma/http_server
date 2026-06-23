#include "util.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <stdio.h>
#include<time.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static int hex_to_int(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

void log_request(const char *client_ip, const char *method, const char *path, int status) {
    FILE *f = fopen("server.log", "a");
    if (!f) return;
    
    time_t now = time(NULL);
    char *ts = ctime(&now);
    ts[strlen(ts) - 1] = '\0'; // Remove newline

    fprintf(f, "[%s] %s %s %s %d\n", ts, client_ip, method, path, status);
    fclose(f);
}
// Converts encoded characters like %20 back into standard bytes
void url_decode(const char *src, char *dest) {
    while (*src) {
        if (*src == '%' && src[1] && src[2]) {
            *dest++ = (char)((hex_to_int(src[1]) << 4) | hex_to_int(src[2]));
            src += 3;
        } else if (*src == '+') {
            *dest++ = ' ';
            src++;
        } else {
            *dest++ = *src++;
        }
    }
    *dest = '\0';
}

// Ensures the requested file mathematically exists inside your www/ directory
int resolve_safe_path(const char *www_root, const char *url_path, char *out, size_t out_size) {
    char raw[2048];
    snprintf(raw, sizeof(raw), "%s%s", www_root, url_path);

    char resolved[PATH_MAX];
    // realpath() resolves all symlinks and ../ navigations into an absolute path
    if (realpath(raw, resolved) == NULL) return -1;  // File doesn't exist

    char root_resolved[PATH_MAX];
    if (realpath(www_root, root_resolved) == NULL) return -1;

    // SECURITY CHECK: The resolved path MUST start with the resolved root path
    if (strncmp(resolved, root_resolved, strlen(root_resolved)) != 0) return -1;

    strncpy(out, resolved, out_size - 1);
    out[out_size - 1] = '\0';
    return 0;
}