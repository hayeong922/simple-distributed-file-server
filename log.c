#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>

char const *system_error() {
    return strerror(errno);
}

void trace(char const *file, char const *function, int line, char const *format, ...) {
    fprintf(stderr, "%s: %s: line %d: ", file, function, line);
    va_list va;
    va_start(va, format);
    vfprintf(stderr, format, va);
    va_end(va);
    fprintf(stderr, "\n");
}

void println(char const *format, ...) {
    va_list va;
    va_start(va, format);
    vfprintf(stdout, format, va);
    va_end(va);
    fprintf(stdout, "\n");
}

void print(char const *format, ...) {
    va_list va;
    va_start(va, format);
    vfprintf(stdout, format, va);
    va_end(va);
}

void panic(char const *format, ...) {
    va_list va;
    va_start(va, format);
    vfprintf(stderr, format, va);
    va_end(va);
    fprintf(stderr, "\n");
    exit(EXIT_FAILURE);
}
