#include <assert.h>
#include <stdio.h>
#include "game.h"

static void assert_empty(const connect_four_t *game) {
    assert(game->moves == 0);
    assert(!is_draw(game));
    for (int row = 0; row < ROWS; row++) {
        for (int column = 0; column < COLUMNS; column++) {
            assert(game->board[row][column] == 0);
        }
    }
}

int main(void) {
    connect_four_t game;
    initialize_connect_four(&game);
    assert_empty(&game);
    assert(apply_move(NULL, 0, 1) == -1);
    assert(apply_move(&game, -1, 1) == -1);
    assert(apply_move(&game, COLUMNS, 1) == -1);
    assert(apply_move(&game, 0, 0) == -1);
    assert(apply_move(&game, 0, 3) == -1);
    assert_empty(&game);
    for (int row = ROWS - 1; row >= 0; row--) {
        int player = row % 2 + 1;
        assert(apply_move(&game, 0, player) == row);
        assert(game.board[row][0] == player);
    }
    assert(apply_move(&game, 0, 1) == -1);
    assert(game.moves == ROWS);

    const int directions[4][2] = {{0, 1}, {1, 0}, {1, 1}, {1, -1}};
    for (int player = 1; player <= 2; player++) {
        for (int direction = 0; direction < 4; direction++) {
            initialize_connect_four(&game);
            int start_column = direction == 3 ? 4 : 1;
            for (int i = 0; i < 4; i++) {
                int row = 1 + i * directions[direction][0];
                int column = start_column + i * directions[direction][1];
                game.board[row][column] = player;
                assert(is_winner(&game, row, column, player) == (i == 3));
            }
            for (int i = 0; i < 4; i++) {
                int row = 1 + i * directions[direction][0];
                int column = start_column + i * directions[direction][1];
                assert(is_winner(&game, row, column, player));
                assert(!is_winner(&game, row, column, 3 - player));
            }
        }
    }
    assert(!is_winner(NULL, 0, 0, 1));
    assert(!is_winner(&game, -1, 0, 1));
    assert(!is_winner(&game, 0, COLUMNS, 1));
    assert(!is_winner(&game, 0, 0, 0));
    assert(!is_draw(NULL));
    initialize_connect_four(&game);
    for (int column = 0; column < COLUMNS; column++) {
        for (int i = 0; i < ROWS; i++) {
            assert(!is_draw(&game));
            assert(apply_move(&game, column, (column + i) % 2 + 1) >= 0);
        }
    }
    assert(is_draw(&game));
    initialize_connect_four(&game);
    assert_empty(&game);
    puts("PASS game rules: gravity, full columns, invalid moves, both players, four win directions, draw, reset");
    return 0;
}
