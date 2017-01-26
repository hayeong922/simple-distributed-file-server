#include "response.h"
#include "request.h"
#include "log.h"
#include "util.h"
#include "net.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <unistd.h>
#include <assert.h>

usize responselen(struct response const *res) {
    usize len = sizeof(struct response_header);

    switch (res->type) {
    case PUT:
        break;
    case GET:
        len += res->get.file.len;
        break;
    case LIST:
        len += res->list.count * NAME_MAX;
        break;
    case MKDIR:
        break;
    }

    return len;
}

int invalid_identity(struct users const *users, char const *username, char const *password) {
    for (usize i = 0; i < users->len; ++i) {
        if (strings_equal(username, users->username[i]) && strings_equal(password, users->password[i])) {
            return 0;
        }
    }
    return 1;
}

int handle_put(char const *dir, char const *path, byte const *file, usize filelen, struct response *res) {
    // must not be prepended with root slash
    if (path[0] == '/') {
        path = path + 1;
    }

    char *fullpath = join_paths(dir, path);

    TRACE("writing file %s", fullpath);
    int err = write_file(fullpath, file, filelen);
    if (err != 0) {
        res->status = INVALID_PATH;
    } else {
        res->status = SUCCESS;
    }

    free(fullpath);
    return 0;
}

int handle_get(char const *dir, char const *path, struct response *res) {
    if (path[0] == '/') {
        path = path + 1;
    }
    char *fullpath = join_paths(dir, path);

    TRACE("getting file %s", fullpath);
    int err = read_file(fullpath, &res->get.file.buf, &res->get.file.len);
    if (err != 0) {
        res->status = FILE_NOT_FOUND;
    } else {
        res->status = SUCCESS;
    }

    free(fullpath);
    return 0;
}

int handle_list(char const *rootdir, char const *path, struct response *res) {
    if (path[0] == '/') {
        path = path + 1;
    }
    char *fullpath = join_paths(rootdir, path);

    TRACE("opening directory %s for listing", fullpath);
    DIR *dir = opendir(fullpath);
    if (!dir) {
        if (errno == ENOTDIR) {
            res->status = NOT_DIRECTORY;
        } else if (errno == ENOENT) {
            res->status = FILE_NOT_FOUND;
        } else {
            panic("unexpected opendir error");
        }
        return 0;
    }

    res->status = SUCCESS;
    res->list.filenames = NULL;
    res->list.count = 0;
    for (struct dirent *de = readdir(dir);
         de != NULL;
         de = readdir(dir))
    {
        if (strings_equal(de->d_name, ".") || strings_equal(de->d_name, "..")) {
            continue;
        }
        TRACE("adding directory entry %s to list", de->d_name);
        res->list.count += 1;
        res->list.filenames = realloc(res->list.filenames, sizeof(char *) * res->list.count);
        res->list.filenames[res->list.count - 1] = malloc(NAME_MAX);
        usize d_name_len = strlen(de->d_name);
        memcpy(res->list.filenames[res->list.count - 1], de->d_name, d_name_len);
        if (d_name_len < NAME_MAX) {
            (res->list.filenames[res->list.count - 1])[d_name_len] = '\0';
        }
    }

    closedir(dir);
    free(fullpath);
    return 0;
}

int handle_mkdir(char const *rootdir, char const *path, struct response *res) {
    if (path[0] == '/') {
        path = path + 1;
    }
    char *fullpath = join_paths(rootdir, path);

    TRACE("making directory %s", fullpath);
    int err = mkdir(fullpath, 0777);
    if (err != 0) {
        if (errno == EEXIST) {
            res->status = PATH_ALREADY_EXISTS;
        } else {
            res->status = INVALID_PATH;
        }
    }

    free(fullpath);
    return 0;
}

int make_response(char const *root, struct users const *users, struct request const *req, struct response *res) {
    memset(res, 0, sizeof(struct response));
    res->type = req->type;

    if (invalid_identity(users, req->username, req->password)) {
        TRACE("invalid identity %s:%s", req->username, req->password);
        res->status = INVALID_IDENTITY;
        return 0;
    }

    char *dir = join_paths(root, req->username);
    struct stat st = {0};
    int err = stat(dir, &st);
    if (err != 0) {
        TRACE("user %s dir does not exist, creating at %s...", req->username, dir);
        mkdir(dir, 0700);
    }

    switch (req->type) {
    case PUT:
        handle_put(dir, req->put.path, req->put.file.buf, req->put.file.len, res);
        break;
    case GET:
        handle_get(dir, req->get.path, res);
        break;
    case LIST:
        handle_list(dir, req->list.path, res);
        break;
    case MKDIR:
        handle_mkdir(dir, req->mkdir.path, res);
        break;
    }

    free(dir);
    return -1;
}

int serialize_response(struct response const *res, byte *buf) {
    struct response_header header = {0};
    header.start = RESPONSE_START;
    header.type = res->type;
    header.status = res->status;

    switch (res->type) {
    case GET:
        header.get.file_len = res->get.file.len;
        break;
    case LIST:
        header.list.count = res->list.count;
        break;
    default:
        break;
    }

    memcpy(buf, &header, sizeof(struct response_header));

    if (res->type == GET) {
        memcpy(&buf[sizeof(struct response_header)], res->get.file.buf, res->get.file.len);
    } else if (res->type == LIST) {
        for (usize i = 0; i < res->list.count; ++i) {
            memcpy(&buf[sizeof(struct response_header) + NAME_MAX * i], res->list.filenames[i], NAME_MAX);
        }
    }

    return 0;
}

char const *status_to_string(byte status) {
    switch (status) {
    case SUCCESS:
        return "success";
    case FILE_NOT_FOUND:
        return "file not found";
    case INVALID_IDENTITY:
        return "invalid username/password";
    case NOT_DIRECTORY:
        return "path is not a directory";
    case PATH_ALREADY_EXISTS:
        return "path already exists";
    case INVALID_PATH:
        return "path is invalid";
    }
}

void print_response(struct response const *res) {
    println("type %c", res->type);
    println("status %s", status_to_string(res->status));
    if (res->type == GET) {
        print("file \"");
        print_escaped(res->get.file.buf, res->get.file.len);
        println("\"");
    } else if (res->type == LIST) {
        println("list entries");
        for (usize i = 0 ; i < res->list.count; ++i) {
            print("%s ", res->list.filenames[i]);
        }
        println("");
    }
}

int recv_put_response(int fd, struct response *res) {
    byte buf[sizeof(struct response_header)];
    usize recvd = 0;
    while (recvd < sizeof(buf)) {
        usize n = read(fd, &buf[recvd], sizeof(buf) - recvd);
        if (n == -1) {
            TRACE("error reading: %s", system_error());
            continue;
        }
        recvd += n;
    }
    struct response_header *header = (struct response_header *)(buf);
    assert(header->start == RESPONSE_START);
    assert(header->type == PUT);
    res->type = PUT;
    res->status = header->status;
    return 0;
}

int recv_get_response(int fd, struct response *res) {
    byte buf[sizeof(struct response_header)];
    usize recvd = 0;
    while (recvd < sizeof(buf)) {
        usize n = read(fd, &buf[recvd], sizeof(buf) - recvd);
        if (n == -1) {
            TRACE("error reading: %s", system_error());
            continue;
        }
        recvd += n;
    }
    struct response_header *header = (struct response_header *)(buf);
    assert(header->start == RESPONSE_START);
    assert(header->type == GET);
    res->type = GET;
    res->status = header->status;

    if (res->status != SUCCESS) {
        return 0;
    }

    res->get.file.len = header->get.file_len;
    res->get.file.buf = malloc(res->get.file.len);
    recvd = 0;
    while (recvd < res->get.file.len) {
        usize n = read(fd, &res->get.file.buf[recvd], res->get.file.len - recvd);
        if (n == -1) {
            TRACE("error reading: %s", system_error());
        }
        recvd += n;
    }

    return 0;
}

int recv_list_response(int fd, struct response *res) {
    set_nonblocking(fd, 0);
    byte buf[sizeof(struct response_header)];
    usize recvd = 0;
    while (recvd < sizeof(buf)) {
        usize n = read(fd, &buf[recvd], sizeof(buf) - recvd);
        if (n == -1) {
            TRACE("error reading: %s", system_error());
            continue;
        }
        recvd += n;
    }
    struct response_header *header = (struct response_header *)(buf);
    assert(header->start == RESPONSE_START);
    assert(header->type == LIST);
    res->type = LIST;
    res->status = header->status;

    if (res->status != SUCCESS) {
        return 0;
    }

    res->list.count = header->list.count;
    res->list.filenames = malloc(sizeof(char *) * res->list.count);
    for (usize i = 0; i < res->list.count; ++i) {
        byte namebuf[NAME_MAX];
        memset(namebuf, 0, sizeof(namebuf));
        usize n = read(fd, namebuf, NAME_MAX);
        assert(n == NAME_MAX);
        TRACE("received filename \"%.*s\"", NAME_MAX, namebuf);
        res->list.filenames[i] = strndup(namebuf, NAME_MAX);
    }
    set_nonblocking(fd, 1);
    return 0;
}

int recv_mkdir_response(int fd, struct response *res) {
    set_nonblocking(fd, 0);

    byte buf[sizeof(struct response_header)];
    usize recvd = 0;
    while (recvd < sizeof(buf)) {
        usize n = read(fd, &buf[recvd], sizeof(buf) - recvd);
        if (n == -1) {
            TRACE("error reading: %s", system_error());
            continue;
        }
        recvd += n;
    }

    struct response_header *header = (struct response_header *)(buf);
    assert(header->start == RESPONSE_START);
    assert(header->type == MKDIR);
    res->type = MKDIR;
    res->status = header->status;
    set_nonblocking(fd, 1);
    return 0;
}
