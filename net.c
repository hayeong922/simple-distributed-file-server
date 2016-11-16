#include "net.h"
#include "log.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>

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
