#include "request.h"
#include "net.h"
#include "log.h"
#include "typedefs.h"
#include "util.h"
#include "log.h"
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>

usize request_data_len(struct request_header const *rh) {
    usize userpass_len = rh->username_len + rh->password_len;

    usize data_len;
    switch (rh->type) {
    case PUT:
        data_len = rh->put.path_len + rh->put.file_len;
        break;
    case GET:
        data_len = rh->get.path_len;
        break;
    case LIST:
        data_len = rh->list.path_len;
        break;
    case MKDIR:
        data_len = rh->mkdir.path_len;
        break;
    default:
        TRACE("unknown type in request header %c", rh->type);
        data_len = 0;
    }

    return userpass_len + data_len;
}

void print_request(struct request const *r) {
    println("username %s", r->username);
    println("password %s", r->password);
    println("type %c", r->type);
    switch (r->type) {
    case PUT:
        println("path %s", r->put.path);
        print("file \"");
        print_escaped(r->put.file.buf, r->put.file.len);
        println("\"");
        break;
    case GET:
        println("path %s", r->get.path);
        break;
    case LIST:
        println("path %s", r->list.path);
        break;
    case MKDIR:
        println("path %s", r->mkdir.path);
        break;
    }
}

int request_from_string(struct request *r, char const *s) {
    char *save;
    char *copy = strdup(s);
    char *token = make_uppercase(strtok_r(copy, " ", &save));

    if (strings_equal(token, "LIST")) {
        r->type = LIST;
        token = strtok_r(NULL, " ", &save);
        if (!token) {
            r->list.path = strdup(".");
        } else {
            r->list.path = strdup(token);
        }
        return 0;
    }

    if (strings_equal(token, "PUT")) {
        r->type = PUT;
        token = strtok_r(NULL, " ", &save);
        if (!token) {
            goto invalid;
        }
        char *path = strdup(token);
        char *filename = take_filename(path);

        int err = read_file(path, &r->put.file.buf, &r->put.file.len);
        if (err != 0) {
            TRACE("unable to read file \"%s\"", path);
            goto invalid;
        }

        char *dir = NULL;
        token = strtok_r(NULL, " ", &save);
        if (!token) {
            dir = strdup(".");
        } else {
            dir = strdup(token);
        }

        r->put.path = join_paths(dir, filename);

        free(path);
        free(filename);
        free(dir);
        return 0;
    }

    if (strings_equal(token, "GET")) {
        r->type = GET;
        token = strtok_r(NULL, " ", &save);
        if (!token) {
            goto invalid;
        }
        r->get.path = strdup(token);
        return 0;
    }

    if (strings_equal(token, "MKDIR")) {
        r->type = MKDIR;
        token = strtok_r(NULL, " ", &save);
        if (!token) {
            goto invalid;
        }
        r->mkdir.path = strdup(token);
        return 0;
    }

invalid:
    memset(r, 0, sizeof(struct request));
    free(copy);
    return -1;
}

void drop_request(struct request *r) {
    if (r) {
        free(r->username);
        free(r->password);
        switch (r->type) {
        case PUT:
            free(r->put.path);
            free(r->put.file.buf);
            break;
        case GET:
            free(r->get.path);
            break;
        case LIST:
            free(r->list.path);
            break;
        case MKDIR:
            free(r->mkdir.path);
            break;
        }
        memset(r, 0, sizeof(struct request));
    }
}

int send_get_request(int fd, char const *username, char const *password, char const *path) {
    set_nonblocking(fd, 0);
    struct request_header header = {0};
    header.start = REQUEST_START;
    header.type = GET;
    header.username_len = strlen(username);
    header.password_len = strlen(password);
    header.get.path_len = strlen(path);

    byte const *b = (byte *)(&header);
    usize len = sizeof(struct request_header);

    { // WRITE REQUEST HEADER
        usize sent = 0;
        while (sent < len) {
            isize n = write(fd, &b[sent], len - sent);
            if (n == -1) {
                TRACE("error writing: %s", system_error());
                continue;
            }
            sent += n;
        }
    }
    { // WRITE REQUEST DATA
        assert(write(fd, username, header.username_len) == header.username_len);
        assert(write(fd, password, header.password_len) == header.password_len);
        assert(write(fd, path, header.get.path_len) == header.get.path_len);
    }

    set_nonblocking(fd, 1);
    return 0;
}

int send_list_request(int fd, char const *username, char const *password, char const *path) {
    set_nonblocking(fd, 0);
    struct request_header header = {0};
    header.start = REQUEST_START;
    header.type = LIST;
    header.username_len = strlen(username);
    header.password_len = strlen(password);
    header.list.path_len = strlen(path);

    { // WRITE REQUEST HEADER
        isize n = write(fd, &header, sizeof(struct request_header));
        assert(n == sizeof(struct request_header));
    }
    { // WRITE REQUEST DATA
        assert(write(fd, username, header.username_len) == header.username_len);
        assert(write(fd, password, header.password_len) == header.password_len);
        assert(write(fd, path, header.list.path_len) == header.list.path_len);
    }
    set_nonblocking(fd, 1);
    return 0;
}

int send_mkdir_request(int fd, char const *username, char const *password, char const *path) {
    set_nonblocking(fd, 0);
    struct request_header header = {0};
    header.start = REQUEST_START;
    header.type = MKDIR;
    header.username_len = strlen(username);
    header.password_len = strlen(password);
    header.mkdir.path_len = strlen(path);

    {
        isize n = write(fd, &header, sizeof(struct request_header));
        assert(n == sizeof(struct request_header));
    }
    {
        assert(write(fd, username, header.username_len) == header.username_len);
        assert(write(fd, password, header.password_len) == header.password_len);
        assert(write(fd, path, header.mkdir.path_len) == header.mkdir.path_len);
    }
    set_nonblocking(fd, 1);
    return 0;
}
