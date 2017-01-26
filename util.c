#include "util.h"
#include "typedefs.h"
#include "log.h"
#include "net.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include <openssl/md5.h>
#include <unistd.h>

char *make_uppercase(char *s) {
    for (usize i = 0; s[i] != '\0'; ++i) {
        s[i] = toupper(s[i]);
    }
    return s;
}

int strings_equal(char const *left, char const *right) {
    return strcmp(left, right) == 0;
}

int read_file(char const *path, byte **buf, usize *len) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        return -1;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);

    *buf = malloc(size);
    *len = size;
    usize n = fread(*buf, sizeof(byte), *len, f);
    if (n != *len) {
        TRACE("unable to read all of file");
        fclose(f);
        free(*buf);
        return -1;
    }

    fclose(f);
    return 0;
}

void print_escaped(byte const *ptr, usize len) {
    for (usize i = 0; i < len; ++i) {
        switch (ptr[i]) {
        case '\n':
            print("\\n");
            break;
        default:
            print("%c", ptr[i]);
        }
    }
}

usize md5_mod4(byte const *ptr, usize len) {
    byte digest[MD5_DIGEST_LENGTH] = {0};
    MD5(ptr, len, digest);

    usize mod = 0;
    for (usize i = 0; i < MD5_DIGEST_LENGTH; ++i) {
        mod = (mod * 16 + digest[i]) % 4;
    }
    return mod;
}

void make_part(byte const *src, usize len, byte **part, usize *partlen, int partn) {
    *partlen = partn != 3 ? len / 4 : len - (len / 4) * 3;
    TRACE("total len %zu, partlen %zu", len, *partlen);
    *part = malloc(*partlen);
    TRACE("write idx: %zu", (len / 4) * partn);
    memcpy(*part, src + (len / 4) * partn, *partlen);
}

int send_put_request(int fd, struct request const *r) {
    set_nonblocking(fd, 0);
    struct request_header rh = {0};
    rh.start = REQUEST_START;
    rh.type = PUT;
    rh.username_len = strlen(r->username);
    rh.password_len = strlen(r->password);
    rh.put.path_len = strlen(r->put.path);
    rh.put.file_len = r->put.file.len;

    { // WRITE REQUEST HEADER
        isize n = write(fd, &rh, sizeof(struct request_header));
        if (n == -1) {
            TRACE("error writing: %s", system_error());
        }
    }
    { // WRITE REQUEST DATA
        assert(write(fd, r->username, strlen(r->username)) == strlen(r->username));
        assert(write(fd, r->password, strlen(r->password)) == strlen(r->password));
        assert(write(fd, r->put.path, strlen(r->put.path)) == strlen(r->put.path));
        usize sent = 0;
        while (sent < r->put.file.len) {
            isize n = write(fd, &r->put.file.buf[sent], r->put.file.len - sent);
            if (n == -1) {
                TRACE("error writing file: %s", system_error());
                return -1;
            }
            sent += n;
        }
    }

    set_nonblocking(fd, 1);
    return 0;
}

char *make_part_path(char const *path, int part) {
    char *last_slash = strrchr(path, '/');
    if (!last_slash) {
        char *part_path = malloc(1 + strlen(path) + 2 + 1);
        part_path[0] = '.';
        memcpy(&part_path[1], path, strlen(path));
        part_path[strlen(path) + 1] = '.';
        part_path[strlen(path) + 2] = '0' + part;
        part_path[strlen(path) + 3] = '\0';
        return part_path;
    }

    char *part_path = malloc(strlen(path) + 3 + 1);
    memcpy(part_path, path, last_slash - path + 1);
    usize last_slash_idx = last_slash - path;
    part_path[last_slash_idx + 1] = '.';
    memcpy(&part_path[last_slash_idx + 2], last_slash + 1, strlen(path) - last_slash_idx - 1);
    part_path[strlen(path) + 1] = '.';
    part_path[strlen(path) + 2] = '0' + part;
    part_path[strlen(path) + 3] = '\0';
    return part_path;
}

char *join_paths(char const *dir, char const *filename) {
    usize dirlen = strlen(dir);
    usize filenamelen = strlen(filename);
    if (dir[dirlen - 1] == '/') {
        char *joined = malloc(dirlen + filenamelen + 1);
        memcpy(joined, dir, dirlen);
        memcpy(joined + dirlen, filename, filenamelen);
        joined[dirlen + filenamelen] = '\0';
        return joined;
    } else {
        char *joined = malloc(dirlen + 1 + filenamelen + 1);
        memcpy(joined, dir, dirlen);
        joined[dirlen] = '/';
        memcpy(joined + dirlen + 1, filename, filenamelen);
        joined[dirlen + 1 + filenamelen] = '\0';
        return joined;
    }
}

int write_file(char const *path, byte const *file, usize len) {
    FILE *f = fopen(path, "wb");
    if (!f) {
        TRACE("error opening file %s for writing: %s", path, system_error());
        return -1;
    }

    usize n = fwrite(file, sizeof(byte), len, f);
    assert(n == len);

    fclose(f);
    return 0;
}

char *take_filename(char const *path) {
    char *slash = strrchr(path, '/');
    if (slash == NULL) {
        return strdup(path);
    }

    return strdup(slash + 1);
}

char *make_get_filename(char const *path) {
    char *filename = take_filename(path);
    char *get_filename = malloc(strlen(filename) + strlen(".received") + 1);
    memcpy(get_filename, filename, strlen(filename));
    memcpy(get_filename + strlen(filename), ".received", strlen(".received"));
    get_filename[strlen(filename) + strlen(".received")] = '\0';

    free(filename);
    return get_filename;
}

char *unmake_part_filename(char const *part_filename, int *part) {
    char *s = strndup(part_filename + 1, strlen(part_filename) - 3);
    *part = part_filename[strlen(part_filename) - 1] - '0';
    return s;
}

byte make_mask(char const *password) {
    usize sum = 0;
    for (usize i = 0; password[i] != '\0'; ++i) {
        sum += password[i];
    }
    return (byte)(sum);
}

void xor_file(byte *file, usize len, byte mask) {
    for (usize i = 0; i < len; ++i) {
        file[i] ^= mask;
    }
}
