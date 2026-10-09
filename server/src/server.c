#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "commands.h"
#include "state.h"

int main(void) {
    int listen_socket;
    struct sockaddr_in server_address;

    listen_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_socket < 0) {
        perror("socket");
        return 1;
    }

    {
        int reuse_option = 1;
        if (setsockopt(listen_socket, SOL_SOCKET, SO_REUSEADDR, &reuse_option, sizeof(reuse_option)) < 0) {
            perror("setsockopt(SO_REUSEADDR)");
            close(listen_socket);
            return 1;
        }
    }

    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(SERVER_PORT);
    server_address.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(listen_socket, (struct sockaddr *)&server_address, sizeof(server_address)) < 0) {
        perror("bind");
        close(listen_socket);
        return 1;
    }

    if (listen(listen_socket, LISTEN_BACKLOG) < 0) {
        perror("listen");
        close(listen_socket);
        return 1;
    }

    printf("Server in ascolto su 0.0.0.0:%d\n", SERVER_PORT);

    while (1) {
        int client_socket;
        struct sockaddr_in client_address;
        socklen_t client_address_length = sizeof(client_address);
        Client *client;
        pthread_t client_thread;

        client_socket = accept(listen_socket, (struct sockaddr *)&client_address, &client_address_length);
        if (client_socket < 0) {
            if (errno == EINTR) continue;
            perror("accept");
            continue;
        }

        client = create_client(client_socket);
        if (!client) {
            close(client_socket);
            continue;
        }

        add_client(client);

        if (pthread_create(&client_thread, NULL, thread_client, client) != 0) {
            remove_client_from_list(client);
            close(client_socket);
            pthread_mutex_destroy(&client->send_mutex);
            free(client);
            continue;
        }
        pthread_detach(client_thread);
    }

    close(listen_socket);
    return 0;
}
