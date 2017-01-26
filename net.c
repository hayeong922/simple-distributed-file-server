#include "net.h"
#include "log.h"
#include "typedefs.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <assert.h>
#include <unistd.h>
#include <sys/poll.h>
#include <errno.h>

int make_tcp_listener(char const *ip, char const *port) {
    struct addrinfo *results = NULL;
    {
        struct addrinfo hints;
        memset(&hints, 0, sizeof(hints));
        hints.ai_socktype = SOCK_STREAM;
        int err = getaddrinfo(ip, port, &hints, &results);
        if (err != 0) {
            errno = -err;
            //info("unable to resolve %s:%s: %s\n", ip, port, gai_strerror(err));
            return -1;
        }
    }

    int fd;
    {
        struct addrinfo *r = NULL;
        for (r = results; r != NULL; r = r->ai_next) {
            fd = socket(r->ai_family, r->ai_socktype | SOCK_CLOEXEC, r->ai_protocol);
            if (fd == -1) {
                continue;
            }

            int reuseaddr = 1;
            int err = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuseaddr, sizeof(reuseaddr));
            if (err != 0) {
                continue;
            }

            err = bind(fd, r->ai_addr, r->ai_addrlen);
            if (err != 0) {
                continue;
            }
            err = listen(fd, SOMAXCONN);
            if (err != 0) {
                continue;
            }

            break;
        }
        freeaddrinfo(results);
        if (r == NULL) {
            return -1;
        }
    }

    return fd;
}

int set_nonblocking(int fd, int nonblocking) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        //trace("set_nonblocking(%d, %d): fcntl(F_GETFL): %s", fd, nonblocking, system_error());
        return -1;
    }
    int new_flags = nonblocking ? flags | O_NONBLOCK : flags & (~O_NONBLOCK);
    int err = fcntl(fd, F_SETFL, new_flags);
    if (err != 0) {
        //trace("set_nonblocking(%d, %d): fcntl(F_SETFL): %s", fd, nonblocking, system_error());
        return -1;
    }
    //trace("set_nonblocking(%d, %d) success", fd, nonblocking);
    return 0;
}

int new_sockaddr_in(struct sockaddr_in *a, char const *ip, char const *port) {
    struct addrinfo hints = {0};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *result_list;
    int err = getaddrinfo(ip, port, &hints, &result_list);
    if (err != 0) {
        TRACE("getaddrinfo: %s", gai_strerror(err));
        return -1;
    }

    if (result_list == NULL) {
        TRACE("getaddrinfo: no results");
        return -1;
    }

    assert(result_list->ai_family == AF_INET);
    memcpy(a, result_list->ai_addr, result_list->ai_addrlen);

    freeaddrinfo(result_list);
    return 0;
}

int new_tcp_socket() {
    return socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
}

int connect_with_timeout(struct sockaddr_in const *addr, int timeout_ms) {
    int fd = new_tcp_socket();
    if (fd == -1) {
        TRACE("new_tcp_socket: %s", system_error());
        return -1;
    }

    int err = set_nonblocking(fd, 1);
    if (err != 0) {
        TRACE("set_nonblocking: %s", system_error());
        close(fd);
        return -1;
    }

    usize addrlen = sizeof(struct sockaddr_in);
    err = connect(fd, addr, addrlen);
    if (err != 0) {
        if (errno == EINPROGRESS) {
            struct pollfd writeable = {
                .fd = fd,
                .events = POLLOUT,
                .revents = 0,
            };
            int ready = poll(&writeable, 1, timeout_ms);
            if (ready == 0) {
                return CONNECT_TIMEOUT;
            } else if (writeable.revents & POLLHUP) {
                return -1;
            }
            // SHOULD BE WRITEABLE
            assert(writeable.revents & POLLOUT);
        } else {
            // UNABLE TO CONNECT TO SERVER
            TRACE("error connecting %s", system_error());
            return -1;
        }
    }

    return fd;
}
