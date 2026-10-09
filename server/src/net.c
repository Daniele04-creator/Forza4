#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/socket.h>

#include "net.h"

static int send_all(int fd, const char *buffer, size_t length) {
    size_t sent = 0;

    while (sent < length) {
        ssize_t n = send(fd, buffer + sent, length - sent, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) return -1;
        sent += (size_t)n;
    }
    return 0;
}

void send_to_client(Client *client, const char *format, ...) {
    char buffer[1024];
    va_list arguments;
    int message_length;
    int result_code = 0;

    if (!client || !client->connected) return;

    va_start(arguments, format);
    message_length = vsnprintf(buffer, sizeof(buffer), format, arguments);
    va_end(arguments);

    if (message_length < 0) return;
    if ((size_t)message_length >= sizeof(buffer) - 2) message_length = (int)sizeof(buffer) - 3;

    buffer[message_length++] = '\n';
    buffer[message_length] = '\0';

    pthread_mutex_lock(&client->send_mutex);
    if (client->connected) {
        result_code = send_all(client->fd, buffer, (size_t)message_length);
        if (result_code < 0) {
            client->connected = 0;
            (void)shutdown(client->fd, SHUT_RDWR);
        }
    }
    pthread_mutex_unlock(&client->send_mutex);
}

int receive_from_client(LineReader *reader, char *buffer, size_t buffer_size) {
    size_t i = 0;

    if (!reader || reader->fd < 0 || !buffer || buffer_size < 2) return RECEIVE_LINE_ERROR;

    while (1) {
        char ch;

        if (reader->position == reader->length) {
            ssize_t received = recv(reader->fd, reader->buffer, sizeof(reader->buffer), 0);
            if (received == 0) {
                if (i == 0) return RECEIVE_LINE_DISCONNECTED;
                break;
            }
            if (received < 0) {
                if (errno == EINTR) continue;
                return RECEIVE_LINE_ERROR;
            }
            reader->position = 0;
            reader->length = (size_t)received;
        }

        ch = reader->buffer[reader->position++];
        if (ch == '\r') continue;
        if (ch == '\n') break;

        if (i < buffer_size - 1) {
            buffer[i++] = ch;
        }
    }

    buffer[i] = '\0';
    return (int)i;
}
