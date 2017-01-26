#ifndef request_h
#define request_h
#include "typedefs.h"

// START
#define REQUEST_START   'R'

// TYPE
#define PUT         'P'
#define GET         'G'
#define LIST        'L'
#define MKDIR       'M'

struct request_header {
    byte start;
    byte type;
    usize username_len;
    usize password_len;

    union {
        struct {
            usize path_len;
            usize file_len;
        } put;

        struct {
            usize path_len;
        } get;

        struct {
            usize path_len;
        } list;

        struct {
            usize path_len;
        } mkdir;
    };
};

struct request {
    char *username;
    char *password;
    byte type;
    union {
        struct {
            char *path;
            struct {
                byte *buf;
                usize len;
            } file;
        } put;

        struct {
            char *path;
        } get;

        struct {
            char *path;
        } list;

        struct {
            char *path;
        } mkdir;
    };
};

usize request_data_len(struct request_header const *rh);
void print_request(struct request const *r);
int request_from_string(struct request *r, char const *s);
void drop_request(struct request *r);
int send_get_request(int fd, char const *username, char const *password, char const *path);
int send_list_request(int fd, char const *username, char const *password, char const *path);
int send_mkdir_request(int fd, char const *username, char const *password, char const *path);

#endif
