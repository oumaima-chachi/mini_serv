# mini_serv - Multi-client Chat Server

A non-blocking TCP server that allows multiple clients to communicate with each other through the server.

## Architecture Overview

```
Client A ◄──────────┐
                    │
Client B ◄──────────┼──► Server (select loop) ──► All other clients
                    │
Client C ◄──────────┘
```

The server uses `select()` to monitor multiple file descriptors simultaneously without blocking.

---

## Line-by-Line Explanation

### Includes & Data Structures

```c
#include <sys/socket.h>      // socket, bind, listen, accept, send, recv
#include <netinet/in.h>      // sockaddr_in, htons, htonl
#include <arpa/inet.h>       // inet functions (for 127.0.0.1)
#include <unistd.h>          // close, write, read
#include <stdlib.h>          // malloc, free, calloc, realloc, exit, atoi
#include <string.h>          // strlen, strcpy, strcat, memset, strstr, memmove
#include <stdio.h>           // sprintf
```

```c
typedef struct s_client {   // Client node in linked list
    int id;                 // Unique ID: 0, 1, 2...
    int fd;                 // Socket file descriptor
    char *buf;              // Accumulated partial messages
    struct s_client *next;  // Pointer to next client
} t_client;
```

**Background:** Each connected client gets a struct. The `buf` field handles **message fragmentation** - TCP is a stream, not message-based. A single `send()` from client may arrive in multiple `recv()` calls, or multiple messages may arrive in one `recv()`. We accumulate until we find `\n`.

---

### Global State

```c
int max_fd = 0;             // Highest FD for select() first argument
int next_id = 0;            // Next client ID to assign
t_client *clients = NULL;   // Head of linked list
fd_set read_fds, write_fds; // Master FD sets (copied each select() call)
```

**Background:** `select()` modifies the FD sets, so we keep master copies and copy them each iteration. `max_fd` avoids scanning all FDs.

---

### fatal_error()

```c
void fatal_error() {
    write(2, "Fatal error\n", 12);  // Write to stderr (fd 2)
    exit(1);                        // Exit with status 1
}
```

**Background:** Called on any system call failure before `accept()` loop, or on malloc failure. Uses `write()` not `printf()` - no buffering issues.

---

### broadcast()

```c
void broadcast(int sender_id, char *msg) {
    t_client *c = clients;
    while (c) {
        if (c->id != sender_id && FD_ISSET(c->fd, &write_fds)) {
            send(c->fd, msg, strlen(msg), 0);
        }
        c = c->next;
    }
}
```

**Background:** Sends `msg` to all clients **except sender**.
- `FD_ISSET(c->fd, &write_fds)` checks if client's socket is ready for writing (non-blocking)
- If client is "lazy" (not reading), `select()` won't mark it writable, so we **don't block** and **don't disconnect** - we just skip this iteration
- `sender_id = -1` for server messages (join/leave) sends to everyone

---

### add_client()

```c
void add_client(int fd) {
    t_client *new = calloc(1, sizeof(t_client));  // Zero-initialized
    if (!new) fatal_error();
    new->id = next_id++;           // Assign ID, increment for next
    new->fd = fd;                  // Store socket FD
    new->next = clients;           // Insert at head of list
    clients = new;
    FD_SET(fd, &read_fds);         // Monitor for incoming data
    FD_SET(fd, &write_fds);        // Monitor for outgoing buffer space
    if (fd > max_fd) max_fd = fd;  // Update max for select()
    char msg[50];
    sprintf(msg, "server: client %d just arrived\n", new->id);
    broadcast(-1, msg);            // Notify all clients
}
```

**Background:** 
- `calloc` zeros memory (buf = NULL, next = NULL)
- Adding to head is O(1)
- Both read and write sets updated - we need to know when we can send
- Broadcast uses `-1` so everyone including sender gets notification

---

### remove_client()

```c
void remove_client(t_client *prev, t_client *cur) {
    char msg[50];
    sprintf(msg, "server: client %d just left\n", cur->id);
    broadcast(-1, msg);                    // Notify others
    FD_CLR(cur->fd, &read_fds);           // Stop monitoring
    FD_CLR(cur->fd, &write_fds);
    close(cur->fd);                        // Close socket
    free(cur->buf);                        // Free message buffer
    if (prev) prev->next = cur->next;      // Unlink from list
    else clients = cur->next;              // Was head
    free(cur);                             // Free node
}
```

**Background:** 
- Must clean up **everything**: FD sets, socket, buffers, list pointers, node memory
- `prev` needed because singly-linked list - can't go backwards
- Broadcast before cleanup so other clients get notified

---

### process_buffer()

```c
void process_buffer(t_client *c) {
    char *line = strstr(c->buf, "\n");     // Find first newline
    while (line) {
        *line = 0;                         // Replace \n with \0 (split line)
        char out[1024];
        sprintf(out, "client %d: %s\n", c->id, c->buf);  // Format
        broadcast(c->id, out);             // Send to others
        memmove(c->buf, line + 1, strlen(line + 1) + 1); // Shift remainder
        line = strstr(c->buf, "\n");       // Check for more lines
    }
}
```

**Background:** 
- Handles **multiple lines in one buffer** (client sent "hi\nhello\n")
- `*line = 0` temporarily terminates string for sprintf
- `memmove` (not memcpy) because source and destination overlap
- `+1` copies the terminating `\0`
- Loop continues until no more `\n` - partial line stays in buffer

---

### main() - Setup

```c
int main(int ac, char **av) {
    if (ac != 2) {
        write(2, "Wrong number of arguments\n", 26);
        return 1;
    }

    int port = atoi(av[1]);
    int sock = socket(AF_INET, SOCK_STREAM, 0);  // TCP socket
    if (sock < 0) fatal_error();

    struct sockaddr_in addr;
    bzero(&addr, sizeof(addr));           // Zero struct
    addr.sin_family = AF_INET;            // IPv4
    addr.sin_addr.s_addr = htonl(0x7F000001);  // 127.0.0.1 (localhost only)
    addr.sin_port = htons(port);          // Port in network byte order

    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) fatal_error();
    if (listen(sock, 100) < 0) fatal_error();  // Backlog 100
```

**Background:**
- `AF_INET` = IPv4, `SOCK_STREAM` = TCP
- `htonl(0x7F000001)` = 127.0.0.1 in network byte order (big-endian)
- `bind()` assigns address to socket
- `listen()` marks socket as passive, queue size 100
- **Only 127.0.0.1** - not INADDR_ANY (0.0.0.0)

---

### main() - FD Set Initialization

```c
    FD_ZERO(&read_fds);    // Clear sets
    FD_ZERO(&write_fds);
    FD_SET(sock, &read_fds);  // Monitor server socket for new connections
    max_fd = sock;
```

**Background:** Server socket only in **read set** - we only `accept()` on it, never `send()`.

---

### main() - Event Loop

```c
    while (1) {
        fd_set r = read_fds, w = write_fds;  // Copy (select modifies)
        if (select(max_fd + 1, &r, &w, NULL, NULL) < 0) fatal_error();
```

**Background:** 
- `select()` blocks until **at least one FD is ready**
- First arg = highest FD + 1 (efficiency)
- `NULL` timeout = block indefinitely
- Modifies `r` and `w` to indicate ready FDs

---

### main() - New Connection

```c
        if (FD_ISSET(sock, &r)) {           // Server socket ready?
            int fd = accept(sock, NULL, NULL);  // Accept connection
            if (fd >= 0) add_client(fd);    // Add to our structures
        }
```

**Background:** 
- `accept()` returns new socket for this client
- `NULL, NULL` = don't care about client address
- New FD automatically blocking, but we only call `recv`/`send` after `select` says ready

---

### main() - Client Data Processing

```c
        t_client *prev = NULL, *cur = clients;
        while (cur) {
            if (FD_ISSET(cur->fd, &r)) {       // This client has data?
                char buf[1024];
                int n = recv(cur->fd, buf, 1023, 0);
                if (n <= 0) {                  // 0 = clean close, -1 = error
                    t_client *to_remove = cur;
                    cur = cur->next;           // Advance before removing
                    remove_client(prev, to_remove);
                    continue;
                }
                buf[n] = 0;                    // Null-terminate received data
```

**Background:** 
- `recv()` returns bytes read, 0 on orderly shutdown, -1 on error
- We save `cur->next` **before** calling `remove_client()` because it frees `cur`
- `prev` unchanged when removing (since `cur` moves to next)

---

### main() - Buffer Management

```c
                // Append to client's accumulated buffer
                char *new_buf = realloc(cur->buf, 
                    (cur->buf ? strlen(cur->buf) : 0) + n + 1);
                if (!new_buf) fatal_error();
                cur->buf = new_buf;
                if (!cur->buf) {               // First allocation
                    cur->buf = malloc(1);
                    if (!cur->buf) fatal_error();
                    cur->buf[0] = 0;
                }
                strcat(cur->buf, buf);         // Append new data
                process_buffer(cur);           // Handle complete lines
            }
            prev = cur;
            cur = cur->next;
        }
    }
}
```

**Background:**
- `realloc` grows buffer: old_len + new_data + 1 (for `\0`)
- First message: `cur->buf` is NULL, `strlen` would crash - ternary handles this
- `strcat` appends to accumulated buffer
- `process_buffer` extracts complete lines, leaves partial in buffer

---

## Key Concepts Summary

| Concept | Implementation |
|---------|---------------|
| **Non-blocking I/O** | `select()` tells us when FD ready |
| **Message framing** | `\n` delimiter, buffer accumulation |
| **Lazy clients** | Check `write_fds` before `send()` - skip if not ready |
| **Memory safety** | `free()` on disconnect, `realloc` for growth |
| **FD safety** | `FD_CLR` + `close()` on disconnect |
| **Single-threaded** | `select()` multiplexes all clients |

---

## Testing

```bash
# Terminal 1: Start server
./mini_serv 8080

# Terminal 2: Client 0
nc 127.0.0.1 8080

# Terminal 3: Client 1
nc 127.0.0.1 8080

# Type in any terminal - appears in others with prefix
```

Expected flow:
```
Server: "server: client 0 just arrived"
Client 0 types "hello\n"
Client 1 receives "client 0: hello\n"
```