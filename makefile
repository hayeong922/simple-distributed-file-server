INCLUDE = src/include
CC = gcc $(CFLAGS) -g
OBJ = net.o log.o connection.o request.o util.o response.o
CFLAGS = -std=gnu11 -D_GNU_SOURCE

all: dfs dfc clean

trace: CFLAGS += -DSHOWTRACE
trace: all

dfs: $(OBJ) dfs.c
	$(CC) -o $@ $^ -lssl -lcrypto

dfc: $(OBJ) dfc.c
	$(CC) -o $@ $^ -lssl -lcrypto

%.o: %.c
	$(CC) -c $<

clean:
	rm $(OBJ)
