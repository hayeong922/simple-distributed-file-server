#ifndef log_h
#define log_h

#ifdef SHOWTRACE
#define TRACE(...) trace(__FILE__, __func__, __LINE__, __VA_ARGS__)
#else
#define TRACE(...) do{}while(0)
#endif

char const *system_error();
void trace(char const *file, char const *function, int line, char const *format, ...);
void println(char const *format, ...);
void print(char const *format, ...);
void panic(char const *format, ...);

#endif
