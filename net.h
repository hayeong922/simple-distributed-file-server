#ifndef net_h
#define net_h
#include <sys/socket.h>
#include <netinet/in.h>

#define CONNECT_TIMEOUT -2

int make_tcp_listener(char const *ip, char const *port);
int set_nonblocking(int fd, int nonblocking);
int new_tcp_socket();
int new_sockaddr_in(struct sockaddr_in *a, char const *ip, char const *port);
int connect_with_timeout(struct sockaddr_in const *addr, int timeout_ms);

#endif
