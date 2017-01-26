#ifndef response_h
#define response_h
#include "request.h"
#include "util.h"

#define RESPONSE_START  'T'

enum {
    SUCCESS,
    FILE_NOT_FOUND,
    INVALID_IDENTITY,
    NOT_DIRECTORY,
    PATH_ALREADY_EXISTS,
    INVALID_PATH,
};

struct response_header {
    byte start;
    byte type;
    byte status;

    union {
        struct {
            usize file_len;
        } get;
        struct {
            usize count;
        } list;
    };
};

struct response {
    byte type;
    byte status;

    union {
        struct {
            struct {
                byte *buf;
                usize len;
            } file;
        } get;

        struct {
            char **filenames;
            usize count;
        } list;
    };
};

int make_response(char const *root, struct users const *users, struct request const *req, struct response *res);
int serialize_response(struct response const *res, byte *buf);
usize responselen(struct response const *res);
void print_response(struct response const *res);
char const *status_to_string(byte status);
int recv_put_response(int fd, struct response *res);
int recv_get_response(int fd, struct response *res);
int recv_list_response(int fd, struct response *res);
int recv_mkdir_response(int fd, struct response *res);

#endif
