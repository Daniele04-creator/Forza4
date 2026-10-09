#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "net.h"
#include "state.h"

Client *g_clients = NULL;
Game *g_games = NULL;
int g_next_game_id = 1;
pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;

const char *status_to_string(game_status_t status) {
    switch (status) {
        case GAME_WAITING: return "In Attesa";
        case GAME_IN_PROGRESS: return "In Corso";
        case GAME_FINISHED: return "Finita";
        default: return "Sconosciuto";
    }
}

int parse_int_range(const char *text, int minimum, int maximum, int *output_value) {
    long value;
    char *end;

    if (!text || !output_value || *text == '\0') return 0;

    errno = 0;
    value = strtol(text, &end, 10);
    if (errno == ERANGE || end == text || *end != '\0') return 0;
    if (value < minimum || value > maximum) return 0;

    *output_value = (int)value;
    return 1;
}

int is_valid_nickname(const char *nickname) {
    size_t i;

    if (!nickname || *nickname == '\0') return 0;

    for (i = 0; nickname[i] != '\0'; i++) {
        unsigned char ch = (unsigned char)nickname[i];
        if (!isalnum(ch) && ch != '_' && ch != '-') {
            return 0;
        }
    }
    return 1;
}

Client *create_client(int fd) {
    Client *client = (Client *)calloc(1, sizeof(Client));
    if (!client) return NULL;

    client->fd = fd;
    atomic_init(&client->connected, 1);
    if (pthread_mutex_init(&client->send_mutex, NULL) != 0) {
        free(client);
        return NULL;
    }
    return client;
}

void add_client(Client *client) {
    if (!client) return;

    pthread_mutex_lock(&g_mutex);
    client->next = g_clients;
    g_clients = client;
    pthread_mutex_unlock(&g_mutex);
}

void remove_client_from_list(Client *client) {
    if (!client) return;

    pthread_mutex_lock(&g_mutex);
    if (g_clients == client) {
        g_clients = client->next;
    } else {
        Client *previous = g_clients;
        while (previous && previous->next != client) previous = previous->next;
        if (previous) previous->next = client->next;
    }
    pthread_mutex_unlock(&g_mutex);
}

Client *find_client_by_nickname(const char *nickname) {
    Client *client = g_clients;
    while (client) {
        if (client->connected && client->authenticated && strcmp(client->nickname, nickname) == 0) {
            return client;
        }
        client = client->next;
    }
    return NULL;
}

Game *find_game_by_id(int id) {
    Game *game = g_games;
    while (game) {
        if (game->id == id) return game;
        game = game->next;
    }
    return NULL;
}

int is_client_participant(Game *game, Client *client) {
    return game && client && (game->player1 == client || game->player2 == client);
}

Client *other_player(Game *game, Client *client) {
    if (!game || !client) return NULL;
    if (game->player1 == client) return game->player2;
    if (game->player2 == client) return game->player1;
    return NULL;
}

void serialize_board(const connect_four_t *game, char *output, size_t output_size) {
    size_t pos = 0;
    int row, column;

    for (row = 0; row < ROWS; row++) {
        for (column = 0; column < COLUMNS; column++) {
            char symbol = '.';
            if (game->board[row][column] == 1) symbol = 'X';
            if (game->board[row][column] == 2) symbol = 'O';
            if (pos + 1 < output_size) output[pos++] = symbol;
        }
        if (row != ROWS - 1 && pos + 1 < output_size) output[pos++] = '/';
    }
    output[pos < output_size ? pos : output_size - 1] = '\0';
}

void clear_join_requests(Game *game) {
    JoinRequest *request = game->requests;
    while (request) {
        JoinRequest *temporary = request;
        request = request->next;
        free(temporary);
    }
    game->requests = NULL;
}

void remove_client_from_requests(Client *client) {
    Game *game = g_games;
    while (game) {
        JoinRequest *previous = NULL, *current = game->requests;
        while (current) {
            if (current->client == client) {
                if (previous) previous->next = current->next;
                else game->requests = current->next;
                free(current);
                if (previous) current = previous->next;
                else current = game->requests;
                continue;
            }
            previous = current;
            current = current->next;
        }
        game = game->next;
    }
}

int has_pending_request(Game *game, Client *client) {
    JoinRequest *request = game->requests;
    while (request) {
        if (request->client == client) return 1;
        request = request->next;
    }
    return 0;
}

Client *take_request_by_nickname(Game *game, const char *nickname) {
    JoinRequest *previous = NULL, *current = game->requests;
    while (current) {
        if (current->client && strcmp(current->client->nickname, nickname) == 0) {
            Client *requested_client = current->client;
            if (previous) previous->next = current->next;
            else game->requests = current->next;
            free(current);
            return requested_client;
        }
        previous = current;
        current = current->next;
    }
    return NULL;
}

void notify_non_participants(Game *game, const char *format, ...) {
    char message[512];
    va_list arguments;
    Client *client;

    if (!game || !format) return;

    va_start(arguments, format);
    vsnprintf(message, sizeof(message), format, arguments);
    va_end(arguments);

    client = g_clients;
    while (client) {
        if (client->connected &&
            client != game->owner &&
            client != game->player1 &&
            client != game->player2) {
            send_to_client(client, "%s", message);
        }
        client = client->next;
    }
}

void send_board_and_turn(Game *game) {
    char board[128];
    const char *turn_nickname;
    serialize_board(&game->board, board, sizeof(board));
    turn_nickname = (game->current_turn == 1 && game->player1) ? game->player1->nickname :
                    (game->current_turn == 2 && game->player2) ? game->player2->nickname : "-";

    if (game->player1) send_to_client(game->player1, "Tabellone %d %s turno di %s", game->id, board, turn_nickname);
    if (game->player2) send_to_client(game->player2, "Tabellone %d %s turno di %s", game->id, board, turn_nickname);
}

void reset_game_to_waiting(Game *game, Client *new_owner) {
    game->status = GAME_WAITING;
    initialize_connect_four(&game->board);
    game->player1 = NULL;
    game->player2 = NULL;
    game->winner = NULL;
    game->current_turn = 1;
    game->draw_rematch_requested1 = 0;
    game->draw_rematch_requested2 = 0;

    clear_join_requests(game);
    game->owner = new_owner;
}

static const char *reason_to_string(const char *reason) {
    if (!reason) return "motivo sconosciuto";
    if (strcmp(reason, "PARTITA_CONCLUSA") == 0) return "partita conclusa regolarmente";
    if (strcmp(reason, "ABBANDONO") == 0) return "abbandono";
    if (strcmp(reason, "DISCONNESSIONE") == 0) return "disconnessione";
    return reason;
}

void finish_game_in_progress(Game *game, Client *winner, const char *reason) {
    Client *player1 = game->player1;
    Client *player2 = game->player2;
    const char *reason_text = reason_to_string(reason);

    game->status = GAME_FINISHED;
    game->winner = winner;
    game->current_turn = 0;
    game->draw_rematch_requested1 = 0;
    game->draw_rematch_requested2 = 0;

    send_board_and_turn(game);

    if (player1) {
        player1->active_game = NULL;
    }
    if (player2) {
        player2->active_game = NULL;
    }

    if (!winner) {
        if (player1) send_to_client(player1, "La partita %d e' finita in pareggio. Motivo: %s", game->id, reason_text);
        if (player2) send_to_client(player2, "La partita %d e' finita in pareggio. Motivo: %s", game->id, reason_text);
    } else {
        Client *loser = (winner == player1) ? player2 : player1;
        send_to_client(winner, "Hai vinto la partita %d. Motivo: %s", game->id, reason_text);
        if (loser) send_to_client(loser, "Hai perso la partita %d. Motivo: %s", game->id, reason_text);
    }

    notify_non_participants(game, "Partita finita %d", game->id);
}

void start_game(Game *game, Client *challenger) {
    Game *finished_game;

    for (finished_game = g_games; finished_game; finished_game = finished_game->next) {
        if (is_client_participant(finished_game, game->owner) ||
            is_client_participant(finished_game, challenger)) {
            finished_game->draw_rematch_requested1 = 0;
            finished_game->draw_rematch_requested2 = 0;
        }
    }

    game->status = GAME_IN_PROGRESS;
    initialize_connect_four(&game->board);
    game->player1 = game->owner;
    game->player2 = challenger;
    game->winner = NULL;
    game->current_turn = 1;

    game->player1->active_game = game;
    game->player2->active_game = game;

    clear_join_requests(game);

    send_to_client(game->player1, "Partita iniziata %d tu=X avversario=O", game->id);
    send_to_client(game->player2, "Partita iniziata %d tu=O avversario=X", game->id);
    notify_non_participants(game, "Partita in corso %d", game->id);
    send_board_and_turn(game);
}

void clean_up_disconnected_client(Client *client) {
    Game *game, *previous, *next_game;

    pthread_mutex_lock(&g_mutex);

    client->connected = 0;
    remove_client_from_requests(client);

    if (client->active_game && client->active_game->status == GAME_IN_PROGRESS) {
        Client *winner = other_player(client->active_game, client);
        finish_game_in_progress(client->active_game, winner, "DISCONNESSIONE");
    }

    previous = NULL;
    game = g_games;
    while (game) {
        int remove_waiting_game;
        next_game = game->next;
        remove_waiting_game = (game->status == GAME_WAITING && game->owner == client);

        if (remove_waiting_game) {
            if (previous) previous->next = next_game;
            else g_games = next_game;
            clear_join_requests(game);
            free(game);
            game = next_game;
            continue;
        }

        if (game->owner == client) game->owner = NULL;
        if (is_client_participant(game, client)) {
            game->draw_rematch_requested1 = 0;
            game->draw_rematch_requested2 = 0;
            if (game->player1 == client) game->player1 = NULL;
            if (game->player2 == client) game->player2 = NULL;
        }
        if (game->winner == client) game->winner = NULL;

        previous = game;
        game = next_game;
    }

    if (g_clients == client) g_clients = client->next;
    else {
        Client *previous_client = g_clients;
        while (previous_client && previous_client->next != client) previous_client = previous_client->next;
        if (previous_client) previous_client->next = client->next;
    }

    pthread_mutex_unlock(&g_mutex);

    close(client->fd);
    pthread_mutex_destroy(&client->send_mutex);
    free(client);
}
