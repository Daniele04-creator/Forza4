#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "commands.h"
#include "net.h"
#include "state.h"

typedef struct {
    int id;
    game_status_t status;
    char owner[MAX_NICKNAME + 1];
    char p1[MAX_NICKNAME + 1];
    char p2[MAX_NICKNAME + 1];
} game_row_t;

static void command_login(Client *c, char *nick) {
    size_t nick_len;
    char nickname[MAX_NICKNAME + 1];

    if (!nick) {
        send_to_client(c, "ERR LOGIN richiede un nickname");
        return;
    }

    nick_len = strlen(nick);
    if (nick_len > MAX_NICKNAME) {
        send_to_client(c, "ERR nickname troppo lungo (max %d)", MAX_NICKNAME);
        return;
    }
    if (!is_valid_nickname(nick)) {
        send_to_client(c, "ERR nickname con caratteri non validi (consentiti: a-z A-Z 0-9 _ -)");
        return;
    }

    pthread_mutex_lock(&g_mutex);
    if (c->authenticated) {
        pthread_mutex_unlock(&g_mutex);
        send_to_client(c, "ERR sei gia autenticato");
        return;
    }
    if (find_client_by_nickname(nick)) {
        pthread_mutex_unlock(&g_mutex);
        send_to_client(c, "ERR nickname gia in uso");
        return;
    }

    c->authenticated = 1;
    strncpy(c->nickname, nick, MAX_NICKNAME);
    c->nickname[MAX_NICKNAME] = '\0';
    (void)snprintf(nickname, sizeof(nickname), "%s", c->nickname);
    pthread_mutex_unlock(&g_mutex);

    send_to_client(c, "OK autenticato come %s", nickname);
}

static void command_list(Client *c) {
    game_row_t *game_list;
    size_t cap = 8;
    size_t game_count = 0;
    Game *p;
    size_t i;
    size_t j;

    game_list = (game_row_t *)calloc(cap, sizeof(game_row_t));
    if (!game_list) {
        send_to_client(c, "ERR allocazione interna");
        return;
    }

    pthread_mutex_lock(&g_mutex);
    p = g_games;
    while (p) {
        if (game_count == cap) {
            size_t new_cap = cap * 2;
            game_row_t *tmp = (game_row_t *)realloc(game_list, new_cap * sizeof(game_row_t));
            if (!tmp) {
                pthread_mutex_unlock(&g_mutex);
                free(game_list);
                send_to_client(c, "ERR allocazione interna");
                return;
            }
            game_list = tmp;
            cap = new_cap;
        }

        game_list[game_count].id = p->id;
        game_list[game_count].status = p->status;
        {
            Client *client_roles[3] = {p->owner, p->player1, p->player2};
            char *nickname_destination[3] = {game_list[game_count].owner, game_list[game_count].p1, game_list[game_count].p2};
            for (j = 0; j < 3; j++) {
                if (client_roles[j] && client_roles[j]->connected && client_roles[j]->authenticated) {
                    (void)snprintf(nickname_destination[j], MAX_NICKNAME + 1, "%s", client_roles[j]->nickname);
                } else {
                    strcpy(nickname_destination[j], "-");
                }
            }
        }
        game_count++;
        p = p->next;
    }
    pthread_mutex_unlock(&g_mutex);

    for (i = 0; i < game_count; i++) {
        send_to_client(c, "Partita %d %s creata da=%s P1=%s P2=%s",
                    game_list[i].id, status_to_string(game_list[i].status), game_list[i].owner, game_list[i].p1, game_list[i].p2);
    }
    free(game_list);
}

static void command_create(Client *c) {
    Game *p;

    pthread_mutex_lock(&g_mutex);
    if (c->active_game) {
        int active_game_id = c->active_game->id;
        pthread_mutex_unlock(&g_mutex);
        send_to_client(c, "ERR sei gia in una partita attiva %d", active_game_id);
        return;
    }

    p = (Game *)calloc(1, sizeof(Game));
    if (!p) {
        pthread_mutex_unlock(&g_mutex);
        send_to_client(c, "ERR allocazione interna");
        return;
    }

    p->id = g_next_game_id++;
    p->status = GAME_WAITING;
    p->owner = c;
    p->current_turn = 1;
    initialize_connect_four(&p->board);

    p->next = g_games;
    g_games = p;

    send_to_client(c, "OK partita %d creata", p->id);
    notify_non_participants(p, "Partita in attesa %d di %s", p->id, c->nickname);
    pthread_mutex_unlock(&g_mutex);
}

static void command_join(Client *c, char *sid) {
    int id;
    Game *p;
    JoinRequest *jr;

    if (!sid) {
        send_to_client(c, "ERR JOIN richiede un id");
        return;
    }
    if (!parse_int_range(sid, 1, INT_MAX, &id)) {
        send_to_client(c, "ERR id partita non valido");
        return;
    }

    pthread_mutex_lock(&g_mutex);
    if (c->active_game) {
        int active_game_id = c->active_game->id;
        pthread_mutex_unlock(&g_mutex);
        send_to_client(c, "ERR sei gia in una partita attiva %d", active_game_id);
        return;
    }

    p = find_game_by_id(id);
    if (!p) {
        pthread_mutex_unlock(&g_mutex);
        send_to_client(c, "ERR partita non trovata");
        return;
    }

    if (p->status != GAME_WAITING) {
        game_status_t status = p->status;
        pthread_mutex_unlock(&g_mutex);
        send_to_client(c, "ERR la partita %d e %s e non e accessibile", id, status_to_string(status));
        return;
    }

    if (p->owner == c) {
        pthread_mutex_unlock(&g_mutex);
        send_to_client(c, "ERR non puoi unirti alla tua partita in attesa");
        return;
    }

    if (has_pending_request(p, c)) {
        pthread_mutex_unlock(&g_mutex);
        send_to_client(c, "ERR richiesta di join gia pendente");
        return;
    }

    jr = (JoinRequest *)calloc(1, sizeof(JoinRequest));
    if (!jr) {
        pthread_mutex_unlock(&g_mutex);
        send_to_client(c, "ERR allocazione interna");
        return;
    }
    jr->client = c;
    jr->next = p->requests;
    p->requests = jr;

    send_to_client(p->owner, "Richiesta di join per la partita %d da %s", p->id, c->nickname);
    pthread_mutex_unlock(&g_mutex);
    send_to_client(c, "OK richiesta di join inviata per la partita %d", id);
}

static void command_accept(Client *c, char *sid, char *nick) {
    int id;
    Game *p;
    Client *challenger;
    JoinRequest *jr;

    if (!sid || !nick) {
        send_to_client(c, "ERR ACCEPT richiede <id> <nickname>");
        return;
    }
    if (!parse_int_range(sid, 1, INT_MAX, &id)) {
        send_to_client(c, "ERR id partita non valido");
        return;
    }

    pthread_mutex_lock(&g_mutex);
    p = find_game_by_id(id);
    if (!p) {
        pthread_mutex_unlock(&g_mutex);
        send_to_client(c, "ERR partita non trovata");
        return;
    }
    if (p->owner != c) {
        pthread_mutex_unlock(&g_mutex);
        send_to_client(c, "ERR solo il proprietario puo usare ACCEPT");
        return;
    }
    if (p->status != GAME_WAITING) {
        pthread_mutex_unlock(&g_mutex);
        send_to_client(c, "ERR la partita non e in stato WAITING");
        return;
    }

    challenger = NULL;
    jr = p->requests;
    while (jr) {
        if (jr->client && strcmp(jr->client->nickname, nick) == 0) {
            challenger = jr->client;
            break;
        }
        jr = jr->next;
    }
    if (!challenger) {
        pthread_mutex_unlock(&g_mutex);
        send_to_client(c, "ERR nessuna richiesta pendente da %s", nick);
        return;
    }

    if (!challenger->connected) {
        (void)take_request_by_nickname(p, nick);
        pthread_mutex_unlock(&g_mutex);
        send_to_client(c, "ERR %s disconnesso", nick);
        return;
    }

    if (c->active_game) {
        int active_game_id = c->active_game->id;
        pthread_mutex_unlock(&g_mutex);
        send_to_client(c, "ERR impossibile avviare: stai gia giocando la partita %d", active_game_id);
        return;
    }
    if (challenger->active_game) {
        char challenger_nickname[MAX_NICKNAME + 1];
        (void)snprintf(challenger_nickname, sizeof(challenger_nickname), "%s", challenger->nickname);
        pthread_mutex_unlock(&g_mutex);
        send_to_client(c, "ERR impossibile avviare: %s e gia in una partita attiva", challenger_nickname);
        return;
    }

    start_game(p, challenger);
    send_to_client(c, "OK accettato %s per la partita %d", challenger->nickname, p->id);
    send_to_client(challenger, "Join Accettata %d", p->id);
    pthread_mutex_unlock(&g_mutex);
}

static void command_reject(Client *c, char *sid, char *nick) {
    int id;
    Game *p;
    Client *target;

    if (!sid || !nick) {
        send_to_client(c, "ERR REJECT richiede <id> <nickname>");
        return;
    }
    if (!parse_int_range(sid, 1, INT_MAX, &id)) {
        send_to_client(c, "ERR id partita non valido");
        return;
    }

    pthread_mutex_lock(&g_mutex);
    p = find_game_by_id(id);
    if (!p) {
        pthread_mutex_unlock(&g_mutex);
        send_to_client(c, "ERR partita non trovata");
        return;
    }
    if (p->owner != c || p->status != GAME_WAITING) {
        pthread_mutex_unlock(&g_mutex);
        send_to_client(c, "ERR solo il proprietario di una partita WAITING puo usare REJECT");
        return;
    }

    target = take_request_by_nickname(p, nick);
    if (!target) {
        pthread_mutex_unlock(&g_mutex);
        send_to_client(c, "ERR nessuna richiesta pendente da %s", nick);
        return;
    }

    send_to_client(c, "OK rifiutato %s per la partita %d", nick, id);
    send_to_client(target, "Richiesta di join per la partita %d rifiutata", id);
    pthread_mutex_unlock(&g_mutex);
}

static void command_move(Client *c, char *scol) {
    int column, piece, row;
    Game *p;

    if (!scol) {
        send_to_client(c, "ERR MOVE richiede una colonna tra 0 e 6");
        return;
    }
    if (!parse_int_range(scol, 0, COLUMNS - 1, &column)) {
        send_to_client(c, "ERR colonna non valida (atteso 0-6)");
        return;
    }

    pthread_mutex_lock(&g_mutex);
    p = c->active_game;
    if (!p || p->status != GAME_IN_PROGRESS || !is_client_participant(p, c)) {
        pthread_mutex_unlock(&g_mutex);
        send_to_client(c, "ERR nessuna partita attiva IN_PROGRESS");
        return;
    }

    piece = (p->player1 == c) ? 1 : 2;
    if (piece != p->current_turn) {
        pthread_mutex_unlock(&g_mutex);
        send_to_client(c, "ERR non e il tuo turno");
        return;
    }

    row = apply_move(&p->board, column, piece);
    if (row < 0) {
        pthread_mutex_unlock(&g_mutex);
        send_to_client(c, "ERR mossa non valida");
        return;
    }

    if (is_winner(&p->board, row, column, piece)) {
        finish_game_in_progress(p, c, "PARTITA_CONCLUSA");
        pthread_mutex_unlock(&g_mutex);
        return;
    }

    if (is_draw(&p->board)) {
        finish_game_in_progress(p, NULL, "PARTITA_CONCLUSA");
        pthread_mutex_unlock(&g_mutex);
        return;
    }

    p->current_turn = (p->current_turn == 1) ? 2 : 1;
    send_board_and_turn(p);
    pthread_mutex_unlock(&g_mutex);

    send_to_client(c, "OK mossa accettata");
}

static void command_state(Client *c) {
    Game *p;
    char compact_board[128];

    pthread_mutex_lock(&g_mutex);
    p = c->active_game;
    if (!p) {
        pthread_mutex_unlock(&g_mutex);
        send_to_client(c, "ERR nessuna partita attiva");
        return;
    }

    serialize_board(&p->board, compact_board, sizeof(compact_board));
    send_to_client(c, "stato partita %d %s tabellone=%s", p->id, status_to_string(p->status), compact_board);
    pthread_mutex_unlock(&g_mutex);
}

static void command_leave(Client *c) {
    Game *p;
    Client *winner;

    pthread_mutex_lock(&g_mutex);
    p = c->active_game;
    if (!p || p->status != GAME_IN_PROGRESS) {
        pthread_mutex_unlock(&g_mutex);
        send_to_client(c, "ERR nessuna partita attiva da abbandonare");
        return;
    }

    winner = other_player(p, c);
    finish_game_in_progress(p, winner, "ABBANDONO");
    pthread_mutex_unlock(&g_mutex);

    send_to_client(c, "OK partita abbandonata");
}

static void command_rematch(Client *c, char *sid) {
    int id = 0;
    Game *p;
    Client *opponent;

    if (sid && !parse_int_range(sid, 1, INT_MAX, &id)) {
        send_to_client(c, "ERR id partita non valido");
        return;
    }

    pthread_mutex_lock(&g_mutex);
    if (c->active_game) {
        int active_game_id = c->active_game->id;
        pthread_mutex_unlock(&g_mutex);
        send_to_client(c, "ERR sei gia in una partita attiva %d", active_game_id);
        return;
    }

    p = g_games;
    while (p) {
        if (p->status == GAME_FINISHED && is_client_participant(p, c) &&
            (!sid || p->id == id)) {
            break;
        }
        p = p->next;
    }
    if (!p) {
        pthread_mutex_unlock(&g_mutex);
        send_to_client(c, "ERR nessuna partita terminata per REMATCH");
        return;
    }

    opponent = other_player(p, c);

    if (!p->winner && is_draw(&p->board)) {
        int *requested;
        Client *player1;
        Client *player2;
        Client *owner;

        if (opponent && opponent->active_game) {
            pthread_mutex_unlock(&g_mutex);
            send_to_client(c, "ERR impossibile riaprire: l'altro giocatore e gia in una partita attiva");
            return;
        }

        if (!c->connected || !p->player1 || !p->player2 ||
            !p->player1->connected || !p->player2->connected) {
            p->draw_rematch_requested1 = 0;
            p->draw_rematch_requested2 = 0;
            pthread_mutex_unlock(&g_mutex);
            send_to_client(c, "ERR rematch dopo pareggio richiede entrambi i giocatori connessi");
            return;
        }

        requested = (p->player1 == c) ? &p->draw_rematch_requested1 : &p->draw_rematch_requested2;
        if (*requested) {
            send_to_client(c, "OK consenso rematch gia registrato per la partita %d", p->id);
            pthread_mutex_unlock(&g_mutex);
            return;
        }
        *requested = 1;

        if (!p->draw_rematch_requested1 || !p->draw_rematch_requested2) {
            send_to_client(c, "OK consenso rematch registrato per la partita %d; in attesa dell'altro giocatore", p->id);
            send_to_client(opponent, "Richiesta di rematch dopo pareggio per la partita %d da %s; usa REMATCH %d per accettare",
                           p->id, c->nickname, p->id);
            pthread_mutex_unlock(&g_mutex);
            return;
        }

        player1 = p->player1;
        player2 = p->player2;
        owner = p->owner ? p->owner : player1;
        reset_game_to_waiting(p, owner);
        send_to_client(player1, "OK rematch aperta sulla partita %d dopo il consenso di entrambi (proprietario=%s)", p->id, owner->nickname);
        send_to_client(player2, "OK rematch aperta sulla partita %d dopo il consenso di entrambi (proprietario=%s)", p->id, owner->nickname);
        notify_non_participants(p, "Partita in attesa %d di %s", p->id, owner->nickname);
        pthread_mutex_unlock(&g_mutex);
        return;
    }

    if (p->winner != c) {
        pthread_mutex_unlock(&g_mutex);
        send_to_client(c, "ERR solo il vincitore puo richiedere REMATCH");
        return;
    }

    if (opponent) {
        send_to_client(opponent, "Sei stato rimosso dalla partita %d per il rematch del vincitore", p->id);
    }

    reset_game_to_waiting(p, c);
    send_to_client(c, "OK rematch aperta sulla partita %d (sei il proprietario)", p->id);
    notify_non_participants(p, "Partita in attesa %d di %s", p->id, c->nickname);
    pthread_mutex_unlock(&g_mutex);
}

void *thread_client(void *arg) {
    Client *c = (Client *)arg;
    char line[MAX_LINE];
    LineReader reader = {.fd = c->fd};

    send_to_client(c, "OK connesso. Usa LOGIN <nickname>");

    while (c->connected) {
        int n = receive_from_client(&reader, line, sizeof(line));
        char *cmd, *a1, *a2;
        char *parse_state = NULL;

        if (!c->connected || n == RECEIVE_LINE_DISCONNECTED || n == RECEIVE_LINE_ERROR) break;
        if (n == 0) continue;

        cmd = strtok_r(line, " ", &parse_state);
        if (!cmd) continue;

        if (strcmp(cmd, "LOGIN") == 0) {
            a1 = strtok_r(NULL, " ", &parse_state);
            command_login(c, a1);
            continue;
        }

        if (!c->authenticated) {
            send_to_client(c, "ERR devi prima fare LOGIN");
            continue;
        }

        if (strcmp(cmd, "LIST") == 0) {
            command_list(c);
        } else if (strcmp(cmd, "CREATE") == 0) {
            command_create(c);
        } else if (strcmp(cmd, "JOIN") == 0) {
            a1 = strtok_r(NULL, " ", &parse_state);
            command_join(c, a1);
        } else if (strcmp(cmd, "ACCEPT") == 0) {
            a1 = strtok_r(NULL, " ", &parse_state);
            a2 = strtok_r(NULL, " ", &parse_state);
            command_accept(c, a1, a2);
        } else if (strcmp(cmd, "REJECT") == 0) {
            a1 = strtok_r(NULL, " ", &parse_state);
            a2 = strtok_r(NULL, " ", &parse_state);
            command_reject(c, a1, a2);
        } else if (strcmp(cmd, "MOVE") == 0) {
            a1 = strtok_r(NULL, " ", &parse_state);
            command_move(c, a1);
        } else if (strcmp(cmd, "STATE") == 0) {
            command_state(c);
        } else if (strcmp(cmd, "LEAVE") == 0) {
            command_leave(c);
        } else if (strcmp(cmd, "REMATCH") == 0) {
            a1 = strtok_r(NULL, " ", &parse_state);
            command_rematch(c, a1);
        } else if (strcmp(cmd, "QUIT") == 0) {
            send_to_client(c, "OK arrivederci");
            break;
        } else {
            send_to_client(c, "ERR comando sconosciuto");
        }
    }

    clean_up_disconnected_client(c);
    return NULL;
}
