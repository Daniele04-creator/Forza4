#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "net.h"

static size_t receive_calls;
static size_t receive_limit;
static int interrupt_next_receive;

ssize_t __real_recv(int fd, void *buffer, size_t length, int flags);

ssize_t __wrap_recv(int fd, void *buffer, size_t length, int flags) {
    receive_calls++;
    if (interrupt_next_receive) {
        interrupt_next_receive = 0;
        errno = EINTR;
        return -1;
    }
    if (receive_limit > 0 && length > receive_limit) length = receive_limit;
    return __real_recv(fd, buffer, length, flags);
}

static void write_bytes(int fd, const char *buffer, size_t length) {
    size_t sent = 0;

    while (sent < length) {
        ssize_t count = send(fd, buffer + sent, length - sent, 0);
        assert(count > 0);
        sent += (size_t)count;
    }
}

static void expect_line(LineReader *reader, const char *expected) {
    char buffer[MAX_LINE];

    assert(receive_from_client(reader, buffer, sizeof(buffer)) == (int)strlen(expected));
    assert(strcmp(buffer, expected) == 0);
}

static void test_buffered_lines(void) {
    int sockets[2];
    const char *burst = "LOGIN alice\r\nLIST\n\nSTATE\n";

    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    LineReader reader = {.fd = sockets[0]};
    write_bytes(sockets[1], burst, strlen(burst));
    receive_calls = 0;
    expect_line(&reader, "LOGIN alice");
    expect_line(&reader, "LIST");
    expect_line(&reader, "");
    expect_line(&reader, "STATE");
    assert(receive_calls <= 2);
    printf("PASS buffered burst: %zu bytes, 4 lines, %zu recv calls\n", strlen(burst), receive_calls);
    assert(close(sockets[0]) == 0);
    assert(close(sockets[1]) == 0);
}

static void test_fragmented_lines(void) {
    int sockets[2];
    const char *message = "LO\rGIN bob\r\nMOVE 3\n";

    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    LineReader reader = {.fd = sockets[0]};
    write_bytes(sockets[1], message, strlen(message));
    /* Small reads force a line to span several recv calls. */
    receive_limit = 3;
    interrupt_next_receive = 1;
    expect_line(&reader, "LOGIN bob");
    expect_line(&reader, "MOVE 3");
    receive_limit = 0;
    assert(close(sockets[0]) == 0);
    assert(close(sockets[1]) == 0);
}

static void test_long_line(void) {
    int sockets[2];
    char message[8198];
    char output[MAX_LINE];

    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    LineReader reader = {.fd = sockets[0]};
    memset(message, 'x', 8192);
    memcpy(message + 8192, "\nNEXT\n", 6);
    write_bytes(sockets[1], message, sizeof(message));
    assert(receive_from_client(&reader, output, sizeof(output)) == MAX_LINE - 1);
    for (size_t i = 0; i < MAX_LINE - 1; i++) assert(output[i] == 'x');
    assert(output[MAX_LINE - 1] == '\0');
    expect_line(&reader, "NEXT");
    assert(close(sockets[0]) == 0);
    assert(close(sockets[1]) == 0);
}

static void test_eof_and_errors(void) {
    int sockets[2];
    char output[MAX_LINE];

    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    LineReader reader = {.fd = sockets[0]};
    write_bytes(sockets[1], "LAST\r", 5);
    assert(close(sockets[1]) == 0);
    expect_line(&reader, "LAST");
    assert(receive_from_client(&reader, output, sizeof(output)) == RECEIVE_LINE_DISCONNECTED);
    assert(close(sockets[0]) == 0);
    assert(receive_from_client(&reader, output, sizeof(output)) == RECEIVE_LINE_ERROR);

    assert(receive_from_client(NULL, output, sizeof(output)) == RECEIVE_LINE_ERROR);
    assert(receive_from_client(&reader, NULL, sizeof(output)) == RECEIVE_LINE_ERROR);
    assert(receive_from_client(&reader, output, 0) == RECEIVE_LINE_ERROR);
    assert(receive_from_client(&reader, output, 1) == RECEIVE_LINE_ERROR);
    reader.fd = -1;
    assert(receive_from_client(&reader, output, sizeof(output)) == RECEIVE_LINE_ERROR);
}

static void test_socket_writes(void) {
    int sockets[2];
    Client client;
    char buffer[64];
    const char *expected = "Test message 42\n";
    size_t received = 0;

    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    memset(&client, 0, sizeof(client));
    client.fd = sockets[0];
    atomic_init(&client.connected, 1);
    assert(pthread_mutex_init(&client.send_mutex, NULL) == 0);

    send_to_client(&client, "Test message %d", 42);
    while (received < strlen(expected)) {
        ssize_t count = recv(sockets[1], buffer + received, sizeof(buffer) - received, 0);
        assert(count > 0);
        received += (size_t)count;
    }
    assert(received == strlen(expected));
    assert(memcmp(buffer, expected, received) == 0);
    assert(atomic_load(&client.connected) == 1);

    assert(close(sockets[1]) == 0);
    /* A write to this socket deterministically raises EPIPE. Without
       MSG_NOSIGNAL the test process itself would be killed by SIGPIPE. */
    send_to_client(&client, "Write after peer close");
    assert(atomic_load(&client.connected) == 0);
    send_to_client(&client, "Further writes are ignored");
    assert(close(sockets[0]) == 0);
    assert(pthread_mutex_destroy(&client.send_mutex) == 0);
    puts("PASS socket writes: formatted newline and closed peer without SIGPIPE termination");
}

int main(void) {
    test_buffered_lines();
    test_fragmented_lines();
    test_long_line();
    test_eof_and_errors();
    test_socket_writes();
    puts("PASS socket reads: fragments, EINTR, CRLF, empty lines, truncation, EOF and errors");
    return 0;
}
