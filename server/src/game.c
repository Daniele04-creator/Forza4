#include "game.h"

void initialize_connect_four(connect_four_t *game) {
    int row, column;
    for (row = 0; row < ROWS; row++) {
        for (column = 0; column < COLUMNS; column++) {
            game->board[row][column] = 0;
        }
    }
    game->moves = 0;
}

int apply_move(connect_four_t *game, int column, int player) {
    int row;
    if (!game || column < 0 || column >= COLUMNS || (player != 1 && player != 2)) {
        return -1;
    }
    for (row = ROWS - 1; row >= 0; row--) {
        if (game->board[row][column] == 0) {
            game->board[row][column] = player;
            game->moves++;
            return row;
        }
    }
    return -1;
}

static int count_direction(const connect_four_t *game, int row, int column, int row_delta, int column_delta, int player) {
    int current_row = row + row_delta;
    int current_column = column + column_delta;
    int c = 0;
    while (current_row >= 0 && current_row < ROWS &&
           current_column >= 0 && current_column < COLUMNS &&
           game->board[current_row][current_column] == player) {
        c++;
        current_row += row_delta;
        current_column += column_delta;
    }
    return c;
}

int is_winner(const connect_four_t *game, int row, int column, int player) {
    int total;
    if (!game || row < 0 || row >= ROWS || column < 0 || column >= COLUMNS || (player != 1 && player != 2)) {
        return 0;
    }
    if (game->board[row][column] != player) {
        return 0;
    }

    total = 1 + count_direction(game, row, column, 0, 1, player) +
             count_direction(game, row, column, 0, -1, player);
    if (total >= 4) return 1;

    total = 1 + count_direction(game, row, column, 1, 0, player) +
             count_direction(game, row, column, -1, 0, player);
    if (total >= 4) return 1;

    total = 1 + count_direction(game, row, column, 1, 1, player) +
             count_direction(game, row, column, -1, -1, player);
    if (total >= 4) return 1;

    total = 1 + count_direction(game, row, column, 1, -1, player) +
             count_direction(game, row, column, -1, 1, player);
    if (total >= 4) return 1;

    return 0;
}


int is_draw(const connect_four_t *game) {
    return game && game->moves >= (ROWS * COLUMNS);
}
