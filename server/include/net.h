#ifndef NET_H
#define NET_H

#include <stddef.h>

#include "server.h"

#define RECEIVE_LINE_DISCONNECTED (-1)
#define RECEIVE_LINE_ERROR (-2)

typedef struct LineReader {
    int fd;
    char buffer[4096];
    size_t position;
    size_t length;
} LineReader;

void send_to_client(Client *client, const char *format, ...);
int receive_from_client(LineReader *reader, char *buffer, size_t buffer_size);

#endif
