#ifndef connection_h
#define connection_h
#include "typedefs.h"

struct connection {
    int fd;
    struct {
        byte *buf;
        usize parse_idx;
        usize end;
        usize capacity;
    } read;
    struct {
        byte *buf;
        usize start;
        usize end;
        usize capacity;
    } write;
};

void drop_connection(struct connection *c);

#endif
