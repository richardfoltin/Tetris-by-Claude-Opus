/* tetris.c — single-file Tetris with a power-up twist.
 * Build game:  gcc -std=c11 -O2 -Wall -Wextra -pedantic -o tetris tetris.c
 * Build tests: gcc -std=c11 -DUNIT_TEST -Wall -Wextra -pedantic -o tetris_test tetris.c && ./tetris_test
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include <conio.h>    /* _kbhit, _getch  (Windows, allowed: non-blocking input) */
#include <windows.h>  /* Sleep            (Windows, allowed: frame pacing)      */

#define BOARD_W 10
#define BOARD_H 20

#define SPECIAL_CHANCE     8     /* percent chance a spawned piece is special   */
#define CHARGE_PER_POWERUP 4     /* line-clear charges needed for one power-up   */
#define INVENTORY_SLOTS    3
#define SLOWMO_MS          8000
#define SLOWMO_FACTOR_NUM  5     /* slow-mo multiplies fall interval by 5/2=2.5  */
#define SLOWMO_FACTOR_DEN  2
#define BOMB_RADIUS        1     /* 3x3 explosion                                */
#define START_INTERVAL_MS  800
#define MIN_INTERVAL_MS    80
#define LEVEL_STEP_MS      60
#define LINES_PER_LEVEL    10
#define FRAME_MS           16

#define HIGHSCORE_PATH "highscore.txt"

/* power-up ids */
enum { PU_NONE = 0, PU_SLOWMO = 1, PU_NUKE = 2, PU_SWAP = 3 };

/* key ids returned by read_key / fed to process_input */
enum {
    KEY_NONE = 0,
    KEY_LEFT, KEY_RIGHT, KEY_UP, KEY_DOWN,
    KEY_DROP, KEY_PAUSE, KEY_QUIT,
    KEY_PU1, KEY_PU2, KEY_PU3
};

typedef struct {
    int type;     /* 0..6 tetromino index; ignored when special != 0 */
    int rot;      /* 0..3 rotation state                             */
    int r, c;     /* row/col of the piece's 4x4 box on the board     */
    int special;  /* 0 normal, 1 bomb, 2 laser                       */
} Piece;

typedef struct {
    int grid[BOARD_H][BOARD_W];     /* 0 empty, else color id 1..7 */
    Piece cur;
    int bag[7];
    int bag_idx;                    /* next index into bag; 7 => refill */
    int next_type;                  /* preview: next normal tetromino   */
    int inv[INVENTORY_SLOTS];       /* 0 empty, else PU_* */
    int inv_count;
    int charge;
    long score;
    int lines;
    int level;
    int slowmo_ms_left;
    int game_over;
    int paused;
    long highscore;
    uint32_t rng;
} Game;

/* ===== forward declarations (always compiled) ===== */
void     rng_seed(Game *g, uint32_t seed);
uint32_t rng_next(Game *g);
int      rng_range(Game *g, int n);

/* (more declarations are added as tasks introduce functions) */

#ifndef UNIT_TEST
/* ===== interactive I/O + game main (game build only) =====
 * Task 12 adds read_key/sleep_ms here; Task 13 adds the renderer here;
 * both ABOVE the main() below. Task 14 replaces this placeholder main body. */
int main(void) {
    return 0;            /* placeholder; replaced by the game loop in Task 14 */
}
#endif /* !UNIT_TEST */

#ifdef UNIT_TEST
/* ---------------- test harness ---------------- */
static int g_tests = 0, g_fails = 0;
#define CHECK(cond) do {                                        \
    g_tests++;                                                  \
    if (!(cond)) { g_fails++;                                   \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);} \
} while (0)

static void test_harness(void) {
    CHECK(1 == 1);
}

int main(void) {
    test_harness();
    printf("\n%d checks, %d failures\n", g_tests, g_fails);
    return g_fails ? 1 : 0;
}
#endif /* UNIT_TEST */
