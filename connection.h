#ifndef connection_h
#define connection_h
#include "typedefs.h"

struct connection {
        int fd;
        struct {
                byte *ptr;
                usize len;
        } read_buf;
        struct {
                byte *ptr;
                usize len;
        } write_buf;
};

#endif
