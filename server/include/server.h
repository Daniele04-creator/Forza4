#ifndef SERVER_H
#define SERVER_H

#include <pthread.h>
#include <stdatomic.h>

#include "game.h"

#define SERVER_PORT 5555
#define LISTEN_BACKLOG 32
#define MAX_LINE 512
#define MAX_NICKNAME 31

typedef struct Client Client;
typedef struct Game Game;

typedef struct JoinRequest {
    Client *client;
    struct JoinRequest *next;
} JoinRequest;

struct Game {
    int id;
    game_status_t status;
    connect_four_t board;
    Client *owner;
    Client *player1;
    Client *player2;
    Client *winner;
    int current_turn;
    int draw_rematch_requested1;
    int draw_rematch_requested2;

    JoinRequest *requests;

    struct Game *next;
};

struct Client {
    int fd;
    atomic_int connected;
    int authenticated;
    char nickname[MAX_NICKNAME + 1];
    Game *active_game;
    pthread_mutex_t send_mutex;
    struct Client *next;
};

extern Client *g_clients;
extern Game *g_games;
extern int g_next_game_id;
extern pthread_mutex_t g_mutex;

#endif
