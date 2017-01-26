README

NAME: Peter Delevoryas
CLASS: 4273

EXTRA CREDIT
    I have attempted to do all of the extra credit on this assignment.

    Encryption
        I have completed the encryption requirement by simpling xor'ing
        files before sending and after receiving with the sum of the
        byte values in the user's password % 256 (i.e., single xor
        byte encryption).

    Subfolders
        I have completed the subfolder requirement by allowing
        the user to create new directories using "mkdir <dir path>".
        This allows for subfolders within each user's root folder,
        as well as creating folders within folders, etc.
        This means that one can also list the files within a certain
        subfolder, by "list <dir path>". This will print the files
        and directories as separate lists.

    Traffic Optimization
        I have completed the traffic optimization by taking advantadge
        of the fact that a file will only be retrievable if servers
        1 and 3, or 2 and 4, are available. 1 and 2, or 2 and 3, or
        3 and 4, or 4 and 1 are not sufficient. Thus, it is
        pointless to attempt to retrieve files from server 1 if
        server 3 does not respond, and vice versa, and for server
        2 and server 4 as well. Thus, when getting a file, my client
        will first attempt to create a TCP connection with
        servers 1 and 3. If unable to create a connection with
        servers 1 or 3, it will close any connections that were
        created, and attempt to connect to servers 2 and 4.
        If unable to create a connection with servers 2 or 4,
        then it will be impossible to recreate the requested file.
        Otherwise, the minimum amount of duplication will be achieved:
        only one of each part of the file will ever be retrieved.

GENERAL ARCHITECTURE
    To achieve concurrent connection handling, I used the epoll
    linux interface. Each server has an epoll instance which
    monitors a listening file descriptor, and a set of connection
    file descriptors. When the listener is readable, connections
    are accepted: when a connection is readable, data is read from
    the socket, attempted to be parsed, and if a request is
    parsed, it is handled and then the response is written to
    a write buffer, at which point the ability to write will be
    monitored on the epoll instance until all of the write buffer
    is sent to the client. If a client disconnects at any point,
    the epoll instance discards the connection and unregisters
    the connection with epoll.

LIBRARIES FOR MD5
    To compute the MD5 hashsum, I used the openssl/md5.h header,
    and linked the ssl and cryptographic libraries
    on linux using -lssl and -lcrypto. I am using Ubuntu 16.04,
    hopefully and these libraries should be standard on any
    linux distribution as far as I know!

BUILDING
    To build the project simply type make. If you wish to
    see some of the debug information, you can do "make trace",
    which will include the TRACE() print statements (a simple
    debug loggin facility I used for this project). The TRACE
    statements were merely for my personal uses, but may be useful
    to understand how my code works.
