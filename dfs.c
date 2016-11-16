#include "net.h"
#include "log.h"
#include "connection.h"
#include "server.h"
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <string.h>

#define MIN_PORT 5000

int is_valid_port(char const *port) {
        unsigned long int ul = strtoul(port, NULL, 10);
        if (ul > USHRT_MAX || ul < MIN_PORT) {
                return 0;
        } else {
                return 1;
        }
}

int main(int argc, char const *const args[]) {
        int tcp_listener = 0;
        int epoll = 0;
        char *root_directory = NULL;
        char *port = NULL;
        int err = -1;

        if (argc < 3) {
                println("not enough arguments");
                println("usage: %s [root directory] [port]", args[0]);
                goto cleanup;
        }

        root_directory = realpath(args[1], NULL);
        if (!root_directory) {
                println("invalid root directory \"%s\": %s", root_directory, system_error());
                goto cleanup;
        }

        port = strdup(args[2]);
        if (!is_valid_port(port)) {
                println("invalid port: %s", port);
                goto cleanup;
        }

        TRACE("starting dfs: root directory %s, port %s", root_directory, port);

        tcp_listener = make_tcp_listener("127.0.0.1", port);
        if (tcp_listener == -1) {
                println("error creating tcp listening socket: %s", system_error());
                goto cleanup;
        }

        err = set_nonblocking(tcp_listener, 1);
        if (err != 0) {
                println("error making tcp listening socket non-blocking: %s", system_error());
                goto cleanup;
        }

        epoll = epoll_create1(EPOLL_CLOEXEC);
        if (epoll == -1) {
                println("unable to create epoll instance: %s", system_error());
                goto cleanup;
        }

        {
                struct epoll_event accept_event = {
                        .events = EPOLLIN|EPOLLET,
                        .data.fd = tcp_listener,
                };
                err = epoll_ctl(epoll, EPOLL_CTL_ADD, tcp_listener, &accept_event);
                if (err != 0) {
                        println("error registering listener with epoll: %s\n", system_error());
                        goto cleanup;
                }
        }

        TRACE("starting event loop");
        int const connection_buf_len = 1024;
        struct connection connection_buf[connection_buf_len];
        int const events_buf_len = 1024;
        struct epoll_event events_buf[events_buf_len];
        while (1) {
                int num_ready = epoll_wait(epoll, events_buf, events_buf_len, -1);
                if (num_ready == -1) {
                        TRACE("epoll_wait: %s", system_error());
                        goto cleanup;
                }

                TRACE("num fds ready: %d", num_ready);
                for (int i = 0; i < num_ready; ++i) {
                        u32 events = events_buf[i].events;
                        int fd = events_buf[i].data.fd;
                        TRACE("fd %d: events %u", fd, events);
                        if (fd == tcp_listener) {
                                // ACCEPT CONNECTION
                        } else {
                                // HANDLE CONNECTION
                        }
                }
        }

cleanup:
        TRACE("exiting...");
        close(tcp_listener);
        close(epoll);
        free(root_directory);
        free(port);
}
