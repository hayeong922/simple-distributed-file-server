#include "connection.h"
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

void drop_connection(struct connection *c) {
    if (c) {
        close(c->fd);
        free(c->read.buf);
        free(c->write.buf);
        memset(c, 0, sizeof(struct connection));
    }
}
