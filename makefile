INCLUDE = src/include
CC = gcc $(CFLAGS)
OBJ = net.o log.o connection.o
CFLAGS = -DSHOWTRACE

all: dfs dfc clean

trace: CFLAGS += -DSHOWTRACE
trace: all

dfs: $(OBJ) dfs.c
	$(CC) -o $@ $^

dfc: $(OBJ) dfc.c
	$(CC) -o $@ $^

%.o: %.c
	$(CC) -c $<

clean:
	rm $(OBJ)
