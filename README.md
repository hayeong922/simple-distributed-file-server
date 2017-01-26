# simple-distributed-file-server

This is a very simple distributed file server, composed of a server program and client program.
It was hard-coded to handle 4 server program instances, for simplicity of implementation.

## Usage

```bash
git clone https://github.com/peterdelevoryas/simple-distributed-file-server
cd simple-distributed-file-server
make # requires libopenssl-dev
mkdir DFS1 DFS2 DFS3 DFS4
./dfs DFS1 10001 &
./dfs DFS2 10002 &
./dfs DFS3 10003 &
./dfs DFS4 10004 &
./dfc dfc.conf
```

This will create 4 directories for the servers to run in (places for them to put files),
and start the servers in the background. The servers can be run wherever you want,
but you will have to update `dfc.conf`, which specifies the socket addresses of the servers.

## Client Commands

The client accepts `put file.txt`, `get file.txt`, `list .`, `mkdir dir`, `put dir/file.txt`, `list dir`, etc.
`put` will attempt sending a half of the file to each server in `dfc.conf`. If unable to connect to a particular server,
the error is ignored, and continues to try with the rest of the servers. The first server will receive the first and second
fourths of the file, the second server will receive the second and third fourths of the file, the third server the third and
fourth fourths, and the fourth server the fourth and first fourths. This means that as long as the first and third or second
and fourth server are still online, then the file can still be recovered entirely. It is important to note that directories
are not split in any way: splitting is only performed on regular files. Retrieved files are renamed to `filename.received`.

## Authentication

Inside `dfs.conf` is a list of the usernames and passwords registered with the servers. `dfc.conf` specifies the username
and password that the client will use to retrieve files. If the client's password doesn't match the server's expected
password, then it will refuse requests. Clients also encrypt and decrypt files before sending and upon receival based
on the password in `dfc.conf` (specifically, in a very insecure way, through single-byte xor masking), so switching
passwords will prevent the user from decrypting uploaded files.

## Server Architecture

The server program can handle connections concurrently, but not in parallel. This is because the server program is
entirely single-threaded. It uses `epoll` to multiplex new TCP connections from the listening socket and readable and
writable events on connection sockets. This eliminates the overhead of thread creation, destruction, and context-switching,
which generally improves throughput, but can introduce scheduling problems (such as fairness).
