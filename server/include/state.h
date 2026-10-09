#ifndef STATE_H
#define STATE_H

#include <stddef.h>

#include "server.h"

const char *status_to_string(game_status_t status);
int parse_int_range(const char *text, int minimum, int maximum, int *output_value);
int is_valid_nickname(const char *nickname);

Client *create_client(int fd);
void add_client(Client *client);
void remove_client_from_list(Client *client);

Client *find_client_by_nickname(const char *nickname);
Game *find_game_by_id(int id);
int is_client_participant(Game *game, Client *client);
Client *other_player(Game *game, Client *client);

void serialize_board(const connect_four_t *game, char *output, size_t output_size);
void clear_join_requests(Game *game);
void remove_client_from_requests(Client *client);
int has_pending_request(Game *game, Client *client);
Client *take_request_by_nickname(Game *game, const char *nickname);

void notify_non_participants(Game *game, const char *format, ...);
void send_board_and_turn(Game *game);
void reset_game_to_waiting(Game *game, Client *new_owner);
void finish_game_in_progress(Game *game, Client *winner, const char *reason);
void start_game(Game *game, Client *challenger);
void clean_up_disconnected_client(Client *client);

#endif
