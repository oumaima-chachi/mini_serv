#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct s_client {
    int id;
    int fd;
    char *buf;
    struct s_client *next;
} t_client;

int max_fd = 0;
int next_id = 0;
t_client *clients = NULL;
fd_set read_fds, write_fds;

void fatal_error() {
    write(2, "Fatal error\n", 12);
    exit(1);
}

void broadcast(int sender_id, char *msg) {
    t_client *c = clients;
    while (c) {
        if (c->id != sender_id && FD_ISSET(c->fd, &write_fds)) {
            send(c->fd, msg, strlen(msg), 0);
        }
        c = c->next;
    }
}

void add_client(int fd) {
    t_client *new = calloc(1, sizeof(t_client));
    if (!new) fatal_error();
    new->id = next_id++;
    new->fd = fd;
    new->next = clients;
    clients = new;
    FD_SET(fd, &read_fds);
    FD_SET(fd, &write_fds);
    if (fd > max_fd) max_fd = fd;
    char msg[50];
    sprintf(msg, "server: client %d just arrived\n", new->id);
    broadcast(-1, msg);
}

void remove_client(t_client *prev, t_client *cur) {
    char msg[50];
    sprintf(msg, "server: client %d just left\n", cur->id);
    broadcast(-1, msg);
    FD_CLR(cur->fd, &read_fds);
    FD_CLR(cur->fd, &write_fds);
    close(cur->fd);
    free(cur->buf);
    if (prev) prev->next = cur->next;
    else clients = cur->next;
    free(cur);
}

void process_buffer(t_client *c) {
    char *line = strstr(c->buf, "\n");
    while (line) {
        *line = 0;
        char out[1024];
        sprintf(out, "client %d: %s\n", c->id, c->buf);
        broadcast(c->id, out);
        memmove(c->buf, line + 1, strlen(line + 1) + 1);
        line = strstr(c->buf, "\n");
    }
}

int main(int ac, char **av) {
    if (ac != 2) {
        write(2, "Wrong number of arguments\n", 26);
        return 1;
    }

    int port = atoi(av[1]);
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) fatal_error();

    struct sockaddr_in addr;
    bzero(&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(0x7F000001);
    addr.sin_port = htons(port);

    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) fatal_error();
    if (listen(sock, 100) < 0) fatal_error();

    FD_ZERO(&read_fds);
    FD_ZERO(&write_fds);
    FD_SET(sock, &read_fds);
    max_fd = sock;

    while (1) {
        fd_set r = read_fds, w = write_fds;
        if (select(max_fd + 1, &r, &w, NULL, NULL) < 0) fatal_error();

        if (FD_ISSET(sock, &r)) {
            int fd = accept(sock, NULL, NULL);
            if (fd >= 0) add_client(fd);
        }

        t_client *prev = NULL, *cur = clients;
        while (cur) {
            if (FD_ISSET(cur->fd, &r)) {
                char buf[1024];
                int n = recv(cur->fd, buf, 1023, 0);
                if (n <= 0) {
                    t_client *to_remove = cur;
                    cur = cur->next;
                    remove_client(prev, to_remove);
                    continue;
                }
                buf[n] = 0;
                char *new_buf = realloc(cur->buf, (cur->buf ? strlen(cur->buf) : 0) + n + 1);
                if (!new_buf) fatal_error();
                cur->buf = new_buf;
                if (!cur->buf) {
                    cur->buf = malloc(1);
                    if (!cur->buf) fatal_error();
                    cur->buf[0] = 0;
                }
                strcat(cur->buf, buf);
                process_buffer(cur);
            }
            if (FD_ISSET(cur->fd, &w) && cur->buf && strlen(cur->buf)) {
                // write_fds handling implicit in broadcast
            }
            prev = cur;
            cur = cur->next;
        }
    }
}