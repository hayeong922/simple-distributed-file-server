#ifndef net_h
#define net_h

int make_tcp_listener(char const *ip, char const *port);
int set_nonblocking(int fd, int nonblocking);

#endif
