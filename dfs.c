#include "net.h"
#include "log.h"
#include "connection.h"
#include "request.h"
#include "response.h"
#include "util.h"
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <limits.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <sys/epoll.h>
#include <sys/socket.h>

#define MIN_PORT    5000
#define MAX_EVENTS      1024
#define INIT_BUF_LEN    1024
#define DFS_CONF    "dfs.conf"

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
        println("invalid root directory \"%s\": %s", args[1], system_error());
        goto cleanup;
    }

    port = strdup(args[2]);
    if (!is_valid_port(port)) {
        println("invalid port: %s", port);
        goto cleanup;
    }

    struct users user = {0};
    {
        FILE *file = fopen(DFS_CONF, "r");
        if (!file) {
            println("unable to open %s: %s", DFS_CONF, system_error());
            goto cleanup;
        }
        char *line_buf = NULL;
        usize line_buf_len = 0;
        for (usize n = getline(&line_buf, &line_buf_len, file);
             n != -1;
             n = getline(&line_buf, &line_buf_len, file))
        {
            if (user.len == user.capacity) {
                usize new_capacity = user.capacity == 0 ? 1 : user.capacity * 2;
                user.username = realloc(user.username, sizeof(char *) * new_capacity);
                user.password = realloc(user.password, sizeof(char *) * new_capacity);
                user.capacity = new_capacity;
            }
            line_buf[n - 1] = '\0';
            usize username_len = strchr(&line_buf[0], ' ') - &line_buf[0];
            usize num_spaces = strspn(&line_buf[username_len], " ");
            usize password_idx = username_len + num_spaces;
            char *username = strndup(&line_buf[0], username_len);
            char *password = strdup(&line_buf[password_idx]);
            user.username[user.len] = username;
            user.password[user.len] = password;
            user.len += 1;
            
        }
        free(line_buf);
        fclose(file);
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
    struct connection connection_buf[MAX_EVENTS];
    memset(connection_buf, 0, sizeof(connection_buf));
    struct epoll_event events_buf[MAX_EVENTS];
    while (1) {
        int num_ready = epoll_wait(epoll, events_buf, MAX_EVENTS, -1);
        if (num_ready == -1) {
            TRACE("epoll_wait: %s", system_error());
            goto cleanup;
        }

        TRACE("num fds ready: %d", num_ready);
        for (int i = 0; i < num_ready; ++i) {
            u32 events = events_buf[i].events;
            int event_fd = events_buf[i].data.fd;
            TRACE("event fd %d, rd %d, wr %d, rdhup %d, hup %d, err %d",
                  event_fd,
                  events & EPOLLIN ? 1 : 0,
                  events & EPOLLOUT ? 1 : 0,
                  events & EPOLLRDHUP ? 1 : 0,
                  events & EPOLLHUP ? 1 : 0,
                  events & EPOLLERR ? 1: 0);
            if (event_fd == tcp_listener) {
                // LOOP ACCEPT
                while (1) {
                    // ACCEPT CONNECTION
                    int connection_fd = accept4(tcp_listener, NULL, NULL, SOCK_CLOEXEC|SOCK_NONBLOCK);
                    if (connection_fd == -1) {
                        TRACE("accept4: %s", system_error());
                        break;
                    }
                    // WAIT FOR READ EVENTS ON NEW CONNECTION
                    struct epoll_event connection_readable = {
                        .events = EPOLLIN | EPOLLRDHUP | EPOLLET,
                        .data.fd = connection_fd,
                    };
                    err = epoll_ctl(epoll, EPOLL_CTL_ADD, connection_fd, &connection_readable);
                    if (err != 0) {
                        TRACE("epoll_ctl: %s", system_error());
                        close(connection_fd);
                        continue;
                    }
                    // ALLOCATE CONNECTION READ AND WRITE BUFFERS
                    connection_buf[connection_fd] = (struct connection){
                        .fd = connection_fd,
                        .read = {
                            .buf = malloc(INIT_BUF_LEN),
                            .parse_idx = 0,
                            .end = 0,
                            .capacity = INIT_BUF_LEN,
                        },
                        .write = {
                            .buf = malloc(INIT_BUF_LEN),
                            .start = 0,
                            .end = 0,
                            .capacity = INIT_BUF_LEN,
                        },
                    };
                }
            } else {
                // HANDLE CONNECTION
                struct connection *c = &connection_buf[event_fd];

                // IF READABLE
                if (events & EPOLLIN) {
                    // READ ALL NEW BYTES
                    while (1) {
                        isize n = read(c->fd, &c->read.buf[c->read.end], c->read.capacity - c->read.end);
                        if (n == -1) {
                            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                                break;
                            } else if (errno == EINTR) {
                                continue;
                            } else {
                                break;
                            }
                        }
                        TRACE("%d -> %zd", c->fd, n);
                        if (n == 0) {
                            events = events | EPOLLRDHUP;
                            break;
                        }
                        c->read.end += n;
                        // RESIZE READ BUF IF NECESSARY
                        if (c->read.end == c->read.capacity) {
                            usize new_cap = c->read.capacity * 2;
                            c->read.buf = realloc(c->read.buf, new_cap);
                            c->read.capacity = new_cap;
                        }
                    }
                    TRACE("read.buf parseable content \"%.*s\"",
                          c->read.end - c->read.parse_idx,
                          &c->read.buf[c->read.parse_idx]);
                }

                // COPY READABLE TO WRITEABLE ECHO TEST
                if (c->read.end - c->read.parse_idx >= sizeof(struct request_header)) {
                    byte const *buf = &c->read.buf[c->read.parse_idx];
                    usize const len = c->read.end - c->read.parse_idx;

                    assert(memchr(buf, REQUEST_START, len) == buf);

                    struct request_header const *rh = (struct request_header *) buf;
                    byte const *data = buf + sizeof(struct request_header);
                    usize data_len = request_data_len(rh);

                    if (len >= sizeof(struct request_header) + data_len) {
                        // PARSE REQUEST, WRITE RESPONSE TO WRITE BUFFER
                        struct request r = {0};
                        r.username = strndup(&data[0], rh->username_len);
                        r.password = strndup(&data[rh->username_len], rh->password_len);

                        byte const *uniondata = &data[rh->username_len + rh->password_len];
                        r.type = rh->type;
                        switch (r.type) {
                        case PUT:
                            r.put.path = strndup(&uniondata[0], rh->put.path_len);
                            r.put.file.buf = malloc(rh->put.file_len);
                            r.put.file.len = rh->put.file_len;
                            memcpy(r.put.file.buf, &uniondata[rh->put.path_len], rh->put.file_len);
                            break;
                        case GET:
                            r.get.path = strndup(&uniondata[0], rh->get.path_len);
                            break;
                        case LIST:
                            r.list.path = strndup(&uniondata[0], rh->list.path_len);
                            break;
                        case MKDIR:
                            r.mkdir.path = strndup(&uniondata[0], rh->mkdir.path_len);
                            break;
                        default:
                            TRACE("unknown request type %c", r.type);
                        }
                        print_request(&r);
                        c->read.parse_idx += sizeof(struct request_header) + data_len;
                        if (c->read.parse_idx == c->read.end) {
                            TRACE("moving parse and read indices back to start");
                            c->read.parse_idx = 0;
                            c->read.end = 0;
                        }

                        struct response res;
                        make_response(root_directory, &user, &r, &res);
                        usize reslen = responselen(&res);
                        while (c->write.capacity <= c->write.end + reslen) {
                            usize newcap = c->write.capacity * 2;
                            c->write.buf = realloc(c->write.buf, newcap);
                            c->write.capacity = newcap;
                        }
                        print_response(&res);
                        serialize_response(&res, &c->write.buf[c->write.end]);
                        //TRACE("serialized response: %.*s", reslen, c->write.buf[c->write.end]);
                        c->write.end += reslen;
                    }
                }

                if (c->write.end - c->write.start > 0) {
                    // WRITE UNTIL WOULD BLOCK
                    while (1) {
                        usize wrlen = c->write.end - c->write.start;
                        usize idx = c->write.start;
                        isize nwritten = write(c->fd, &c->write.buf[idx], wrlen);
                        if (nwritten == -1) {
                            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                                struct epoll_event add_writeable = {
                                    .events = EPOLLIN | EPOLLOUT | EPOLLET,
                                    .data.fd = c->fd,
                                };
                                err = epoll_ctl(epoll, EPOLL_CTL_MOD, c->fd, &add_writeable);
                                if (err != 0) {
                                    TRACE("error adding EPOLLOUT to %d events: %s",
                                          c->fd, system_error());
                                }
                                break;
                            } else if (errno == EINTR) {
                                continue;
                            } else {
                                break;
                            }
                        }
                        TRACE("%d <- %zd \"%.*s\"", c->fd, nwritten, 10, c->write.buf);
                        if (nwritten == 0) {
                            break;
                        }
                        c->write.start += nwritten;
                        if (c->write.end - c->write.start == 0) {
                            c->write.start = 0;
                            c->write.end = 0;
                            struct epoll_event only_readable = {
                                .events = EPOLLIN | EPOLLET,
                                .data.fd = c->fd,
                            };
                            err = epoll_ctl(epoll, EPOLL_CTL_MOD, c->fd, &only_readable);
                            if (err != 0) {
                                TRACE("error removing EPOLLOUT from %d events: %s",
                                      c->fd, system_error());
                            }
                            break;
                        }
                    }
                }

                // IF OTHER SIDE NO LONGER READING
                if (events & EPOLLRDHUP) {
                    TRACE("%d -> rdhup", c->fd);
                    err = epoll_ctl(epoll, EPOLL_CTL_DEL, c->fd, NULL);
                    if (err != 0) {
                        TRACE("error removing %d from epoll", c->fd);
                    } else {
                        TRACE("epoll -> %d", c->fd);
                    }
                    drop_connection(c);
                }
            }
        }
    }

cleanup:
    TRACE("exiting...");
    close(tcp_listener);
    close(epoll);
    free(root_directory);
    free(port);
    for (usize i = 0; i < MAX_EVENTS; ++i) {
        drop_connection(&connection_buf[i]);
    }
    for (usize i = 0; i < user.len; ++i) {
        free(user.username[i]);
        free(user.password[i]);
    }
    free(user.username);
    free(user.password);
    user.len = 0;
    user.capacity = 0;
}
