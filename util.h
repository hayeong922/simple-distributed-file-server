#ifndef util_h
#define util_h
#include "typedefs.h"
#include "request.h"

char *make_uppercase(char *s);
int strings_equal(char const *left, char const *right);
int read_file(char const *path, byte **buf, usize *len);
void print_escaped(byte const *ptr, usize len);
usize md5_mod4(byte const *ptr, usize len);
int send_put_request(int fd, struct request const *r);
void make_part(byte const *src, usize len, byte **part, usize *partlen, int partn);
char *make_part_path(char const *path, int part);
char *join_paths(char const *dir, char const *filename);
int write_file(char const *path, byte const *file, usize len);
char *take_filename(char const *path);
char *make_get_filename(char const *path);
char *unmake_part_filename(char const *part_filename, int *part);
byte make_mask(char const *password);
void xor_file(byte *file, usize len, byte mask);

struct users {
    char **username;
    char **password;
    usize len;
    usize capacity;
};

#endif
