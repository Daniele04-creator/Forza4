#ifndef GAME_H
#define GAME_H

#define ROWS 6
#define COLUMNS 7

typedef enum {
    GAME_WAITING = 0,
    GAME_IN_PROGRESS = 1,
    GAME_FINISHED = 2
} game_status_t;

typedef struct {
    int board[ROWS][COLUMNS];
    int moves;
} connect_four_t;

void initialize_connect_four(connect_four_t *game);
int apply_move(connect_four_t *game, int column, int player);
int is_winner(const connect_four_t *game, int row, int column, int player);
int is_draw(const connect_four_t *game);

#endif
