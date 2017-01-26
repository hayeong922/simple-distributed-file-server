#include "log.h"
#include "net.h"
#include "request.h"
#include "typedefs.h"
#include "response.h"
#include "util.h"
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <assert.h>
#include <poll.h>

struct server {
    struct sockaddr_in addr;
    char *ip;
    char *port;
};

int read_conf(char const *conf_path, char **username, char **password, struct server *dfs) {
    FILE *file = fopen(conf_path, "r");
    if (!file) {
        return -1;
    }

    char *buf = NULL;
    usize cap = 0;

    for (usize n = getline(&buf, &cap, file);
         n != -1;
         n = getline(&buf, &cap, file))
    {
        char *save;
        char *token = strtok_r(buf, " \n", &save);
        if (!token) {
            continue;
        }

        if (strcmp(token, "Server") == 0) {
            token = strtok_r(NULL, " \n", &save);
            int dfsn;
            sscanf(token, "DFS%d", &dfsn);
            dfsn -= 1;
            token = strtok_r(NULL, " \n", &save);
            char const *sep = strchr(token, ':');
            dfs[dfsn].ip = strndup(token, sep - token);
            dfs[dfsn].port = strdup(sep+1);
            new_sockaddr_in(&dfs[dfsn].addr, dfs[dfsn].ip, dfs[dfsn].port);
        } else if (strcmp(token, "Username:") == 0) {
            token = strtok_r(NULL, " \n", &save);
            *username = strdup(token);
        } else if (strcmp(token, "Password:") == 0) {
            token = strtok_r(NULL, " \n", &save);
            *password = strdup(token);
        } else {
            continue;
        }

    }

    free(buf);
    fclose(file);

    return 0;
}

#define CONNECT_TIMEOUT_MS 1000

int put_file(char const *username,
             char const *password,
             struct server dfs[4],
             char const *path,
             byte const *file,
             usize file_len)
{
    usize mod = md5_mod4(file, file_len);

    for (usize i = 0; i < 4; ++i) {
        usize dfsn = (i + mod) % 4;
        struct sockaddr *addr = (struct sockaddr *) &dfs[dfsn].addr;
        usize addrlen = sizeof(struct sockaddr_in);

        int fd = connect_with_timeout(&dfs[dfsn].addr, CONNECT_TIMEOUT_MS);
        if (fd < 0) {
            goto next_server;
        }

        struct request r;
        struct response res;

        TRACE("sending part %d to dfs[%d]", i, dfsn);
        memset(&r, 0, sizeof(struct request));
        r.username = strdup(username);
        r.password = strdup(password);
        r.type = PUT;
        // MAKE .filename.txt.x pathname
        r.put.path = make_part_path(path, i);
        make_part(file, file_len, &r.put.file.buf, &r.put.file.len, i);

        int err = send_put_request(fd, &r);
        if (err != 0) {
            TRACE("error sending part %d to dfs[%d]", i, dfsn);
        }
        free(r.put.path);
        free(r.put.file.buf);
        free(r.username);
        free(r.password);

        memset(&res, 0, sizeof(res));
        set_nonblocking(fd, 0);
        err = recv_put_response(fd, &res);
        set_nonblocking(fd, 1);
        switch (res.status) {
         case SUCCESS:
            println("success putting part %d to dfs[%d]", i, dfsn);
            break;

        case INVALID_PATH:
            println("invalid put path \"%s\", dfs[%d]", path, dfsn);
            close(fd);
            return -1;
            break;

        case INVALID_IDENTITY:
            println("invalid username/password: %s:%s", username, password);
            close(fd);
            return -1;
            break;

        default:
            panic("unexpected response status %d", res.status);
        }

        TRACE("sending part %d to dfs[%d]", (i + 1) % 4, dfsn);
        memset(&r, 0, sizeof(struct request));
        r.username = strdup(username);
        r.password = strdup(password);
        r.type = PUT;
        // MAKE .filename.txt.x pathname
        r.put.path = make_part_path(path, (i + 1) % 4);
        make_part(file, file_len, &r.put.file.buf, &r.put.file.len, (i + 1) % 4);

        err = send_put_request(fd, &r);
        if (err != 0) {
            TRACE("error sending part %d to dfs[%d]", (i + 1) % 4, dfsn);
        }
        free(r.username);
        free(r.password);
        free(r.put.path);
        free(r.put.file.buf);

        memset(&res, 0, sizeof(res));
        set_nonblocking(fd, 0);
        err = recv_put_response(fd, &res);
        set_nonblocking(fd, 1);
        switch (res.status) {
         case SUCCESS:
            println("success putting part %d to dfs[%d]", (i + 1) % 4, dfsn);
            break;

        case INVALID_PATH:
            println("invalid put path \"%s\", dfs[%d]", path, dfsn);
            close(fd);
            return -1;
            break;

        case INVALID_IDENTITY:
            println("invalid username/password: %s:%s", username, password);
            close(fd);
            return -1;
            break;

        default:
            panic("unexpected response status %d", res.status);
        }

next_server:
        close(fd);
    }
    
    return 0;
}

int get_file(char const *username,
             char const *password,
             struct server dfs[4],
             char const *path)
{
    byte *part[4];
    usize partlen[4];
    memset(part, 0, sizeof(part));
    memset(partlen, 0, sizeof(partlen));

    for (int i = 0; i < 2; ++i) {
        int fd[2];

        fd[0] = connect_with_timeout(&dfs[0 + i].addr, CONNECT_TIMEOUT_MS);
        if (fd[0] < 0) {
            if (fd[0] == CONNECT_TIMEOUT) {
                println("timeout connecting to dfs[%d]", 0 + i);
            }
            continue;
        }

        fd[1] = connect_with_timeout(&dfs[2 + i].addr, CONNECT_TIMEOUT_MS);
        if (fd[1] < 0) {
            if (fd[1] == CONNECT_TIMEOUT) {
                println("timeout connecting to dfs[%d]", 2 + i);
            }
            close(fd[0]);
            continue;
        }

        int parts_collected = 0;
        for (int j = 0; j < 2; ++j) {
            int conn = fd[j];
            for (int partn = 0; partn < 4; ++partn) {
                char *partn_path = make_part_path(path, partn);
                int err = send_get_request(conn, username, password, partn_path);
                if (err != 0) {
                    TRACE("send_get_request: %s", system_error());
                    goto finish;
                }
                free(partn_path);

                struct response res = {0};
                set_nonblocking(conn, 0);
                err = recv_get_response(conn, &res);
                set_nonblocking(conn, 1);
                if (err != 0) {
                    TRACE("recv_get_response: %s", system_error());
                    goto finish;
                }

                if (res.status == SUCCESS) {
                    parts_collected += 1;
                    assert(part[partn] == NULL);
                    assert(partlen[partn] == 0);
                    part[partn] = res.get.file.buf;
                    partlen[partn] = res.get.file.len;
                }
            }
        }
        if (parts_collected < 4) {
            close(fd[0]);
            close(fd[1]);
            continue;
        }

finish:
        close(fd[0]);
        close(fd[1]);
        break;
    }

    int incomplete = 0;
    for (int i = 0; i < 4; ++i) {
        if (part[i] == NULL) {
            incomplete = 1;
            break;
        }
    }

    if (incomplete) {
        println("failed to get \"%s\": file incomplete", path);
        for (int i = 0; i < 4; ++i) {
            free(part[i]);
            part[i] = NULL;
        }
        return -1;
    }

    usize complete_len = partlen[0] + partlen[1] + partlen[2] + partlen[3];
    byte *complete_file = malloc(complete_len);
    memcpy(complete_file, part[0], partlen[0]);
    memcpy(complete_file + partlen[0], part[1], partlen[1]);
    memcpy(complete_file + partlen[0] + partlen[1], part[2], partlen[2]);
    memcpy(complete_file + partlen[0] + partlen[1] + partlen[2], part[3], partlen[3]);

    // DECRYPT FILE
    byte mask = make_mask(password);
    xor_file(complete_file, complete_len, mask);

    char *get_filename = make_get_filename(path);
    println("success getting file, writing to \"%s\"", get_filename);
    write_file(get_filename, complete_file, complete_len);

    free(get_filename);
    return 0;
}

int list_files(char const *username, char const *password, struct server dfs[4], char const *path) {
    char **filenames = NULL;
    int (*parts)[4] = NULL;
    usize count = 0;

    char **directories = NULL;
    usize num_directories = 0;

    for (int dfsn = 0; dfsn < 4; ++dfsn) {
        int conn = connect_with_timeout(&dfs[dfsn].addr, CONNECT_TIMEOUT_MS);
        if (conn < 0) {
            TRACE("unable to connect to dfs[%d]", dfsn);
            continue;
        }

        send_list_request(conn, username, password, path);
        struct response res = {0};
        recv_list_response(conn, &res);
        if (res.status != SUCCESS) {
            if (res.status == NOT_DIRECTORY) {
                println("\"%s\" is not a directory", path);
            } else if (res.status == FILE_NOT_FOUND) {
                println("\"%s\" not found", path);
            } else {
                panic("invalid res.status error for list");
            }
            close(conn);
            return -1;
        }

        // ADD FILES TO LIST
        for (usize i = 0; i < res.list.count; ++i) {
            isize filename_idx = -1;
            int part = -1;
            char const *part_filename = res.list.filenames[i];
            if (part_filename[0] != '.') {
                int already_in = 0;
                for (usize j = 0; j < num_directories; ++j) {
                    if (strings_equal(directories[j], part_filename)) {
                        already_in = 1;
                        break;
                    }
                }
                if (!already_in) {
                    // DIRECTORY
                    num_directories += 1;
                    directories = realloc(directories, num_directories * sizeof(char *));
                    directories[num_directories - 1] = strdup(part_filename);
                }
                continue;
            }

            char *filename = unmake_part_filename(part_filename, &part);
            TRACE("part filename %s, filename %s, part %d", part_filename, filename, part);

            for (usize j = 0; j < count; ++j) {
                if (strings_equal(filenames[j], filename)) {
                    filename_idx = j;
                    break;
                }
            }
            if (filename_idx == -1) {
                count += 1;
                filenames = realloc(filenames, count * sizeof(char*));
                parts = realloc(parts, count * sizeof(int) * 4);
                filename_idx = count - 1;
                filenames[filename_idx] = strdup(filename);
                for (int k = 0; k < 4; ++k) {
                    parts[filename_idx][k] = 0;
                }
            }
            parts[filename_idx][part] = 1;

            free(filename);
        }
    }

    println("files:");
    if (count == 0) {
        println("(no files)");
    }
    for (usize i = 0; i < count; ++i) {
        int complete = 1;
        for (int j = 0; j < 4; ++j) {
            if (!parts[i][j]) {
                complete = 0;
                break;
            }
        }
        if (complete) {
            println("%s", filenames[i]);
        } else {
            println("%s [incomplete]", filenames[i]);
        }
    }
    println("directories:");
    if (num_directories == 0) {
        println("(no directories)");
    }
    for (usize i = 0; i < num_directories; ++i) {
        println("%s", directories[i]);
    }

    for (usize i = 0; i < count; ++i) {
        free(filenames[i]);
        filenames[i] = NULL;
    }
    free(filenames);
    free(parts);

    return 0;
}

int make_directory(char const *username, char const *password, struct server dfs[4], char const *path) {
    for (int dfsn = 0; dfsn < 4; ++dfsn) {
        int fd = connect_with_timeout(&dfs[dfsn].addr, CONNECT_TIMEOUT_MS);
        if (fd < 0) {
            TRACE("unable to connect to dfs[%s]", dfsn);
            continue;
        }

        send_mkdir_request(fd, username, password, path);

        struct response res = {0};
        recv_mkdir_response(fd, &res);

        if (res.status == SUCCESS) {
            println("success creating directory \"%s\" on dfs[%d]", path, dfsn);
        } else if (res.status == PATH_ALREADY_EXISTS) {
            println("error creating directory: path \"%s\" already exists on dfs[%d]", path, dfsn);
        } else if (res.status == INVALID_PATH) {
            println("error creating directory: path \"%s\" is invalid on dfs[%d]", path, dfsn);
        } else {
            panic("invalid response status from dfs[%d]", dfsn);
        }
    }
    return 0;
}

int main(int argc, char const *const args[]) {
    TRACE("running distributed file server client");

    char *dfc_conf_path = NULL;
    char *username = NULL;
    char *password = NULL;
    struct server dfs[4] = {0};
    int err = -1;

    if (argc < 2) {
        println("not enough arguments");
        println("usage: %s [dfc.conf]", args[0]);
        goto cleanup;
    }

    dfc_conf_path = realpath(args[1], NULL);
    if (!dfc_conf_path) {
        println("invalid dfc.conf path: \"%s\": %s", args[1], system_error());
        goto cleanup;
    }

    err = read_conf(dfc_conf_path, &username, &password, dfs);
    if (err != 0) {
        println("unable to read config file: %s", system_error());
        goto cleanup;
    }

    for (int i = 0; i < 4; ++i) {
        println("DFS%d %s:%s", i + 1, dfs[i].ip, dfs[i].port);
    }
    println("username %s", username);
    println("password %s", password);

    char *line = NULL;
    usize linelen = 0;
    while (1) {
        print("> ");
        isize n = getline(&line, &linelen, stdin);
        if (n == -1) {
            println("exiting...");
            break;
        } else if (n == 1) {
            continue;
        }
        line[n - 1] = '\0';

        struct request r = {0};
        err = request_from_string(&r, line);
        if (err != 0) {
            println("invalid command \"%s\"", line);
            continue;
        }

        r.username = strdup(username);
        r.password = strdup(password);

        //print_request(&r);

        if (r.type == PUT) {
            // ENCRYPT FILE
            byte mask = make_mask(password);
            xor_file(r.put.file.buf, r.put.file.len, mask);

            put_file(username, password, dfs, r.put.path, r.put.file.buf, r.put.file.len);
        } else if (r.type == GET) {
            // DECRYPTION INSIDE GET_FILE
            get_file(username, password, dfs, r.get.path);
        } else if (r.type == LIST) {
            list_files(username, password, dfs, r.list.path);
        } else if (r.type == MKDIR) {
            make_directory(username, password, dfs, r.mkdir.path);
        }

        drop_request(&r);
    }

cleanup:
    free(dfc_conf_path);
    for (int i = 0; i < 4; ++i) {
        free(dfs[i].ip);
        free(dfs[i].port);
    }
    free(username);
    free(password);
}
