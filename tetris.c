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
void     bag_refill(Game *g);
int      bag_pop(Game *g);
void     shape_cells(int type, int rot, int out[4][2]);
void     piece_cells(const Piece *p, int out[4][2], int *count);
int      collides(const Game *g, const Piece *p);

/* (more declarations are added as tasks introduce functions) */

/* ===== PRNG (xorshift32) ===== */
void rng_seed(Game *g, uint32_t seed) {
    g->rng = seed ? seed : 0xC0FFEEu;   /* avoid the zero fixed point */
}

uint32_t rng_next(Game *g) {
    uint32_t x = g->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    g->rng = x;
    return x;
}

int rng_range(Game *g, int n) {
    if (n <= 0) return 0;
    return (int)(rng_next(g) % (uint32_t)n);
}

/* ===== 7-bag piece generator ===== */
void bag_refill(Game *g) {
    int i;
    for (i = 0; i < 7; i++) g->bag[i] = i;
    for (i = 6; i > 0; i--) {                 /* Fisher-Yates */
        int j = rng_range(g, i + 1);
        int tmp = g->bag[i]; g->bag[i] = g->bag[j]; g->bag[j] = tmp;
    }
    g->bag_idx = 0;
}

int bag_pop(Game *g) {
    if (g->bag_idx >= 7) bag_refill(g);
    return g->bag[g->bag_idx++];
}

/* ===== Tetromino shapes + rotation ===== */

/* Base spawn orientations in a 4x4 box, row-major, '#' = filled.
 * Order matches color ids: I,O,T,S,Z,J,L (index 0..6). */
static const char *BASE_SHAPE[7] = {
    "...." "####" "...." "....",   /* I */
    ".##." ".##." "...." "....",   /* O */
    ".#.." "###." "...." "....",   /* T */
    ".##." "##.." "...." "....",   /* S */
    "##.." ".##." "...." "....",   /* Z */
    "#..." "###." "...." "....",   /* J */
    "..#." "###." "...." "...."    /* L */
};

void shape_cells(int type, int rot, int out[4][2]) {
    int grid[4][4], r, c, k, n = 0;
    const char *s = BASE_SHAPE[type];
    for (r = 0; r < 4; r++)
        for (c = 0; c < 4; c++)
            grid[r][c] = (s[r * 4 + c] == '#');
    for (k = 0; k < (rot & 3); k++) {       /* clockwise rotation */
        int tmp[4][4];
        for (r = 0; r < 4; r++)
            for (c = 0; c < 4; c++)
                tmp[r][c] = grid[3 - c][r];
        for (r = 0; r < 4; r++)
            for (c = 0; c < 4; c++)
                grid[r][c] = tmp[r][c];
    }
    for (r = 0; r < 4; r++)
        for (c = 0; c < 4; c++)
            if (grid[r][c]) { out[n][0] = r; out[n][1] = c; n++; }
}

/* ===== Absolute cells + collision ===== */
void piece_cells(const Piece *p, int out[4][2], int *count) {
    if (p->special) {
        out[0][0] = p->r + 1;
        out[0][1] = p->c + 1;
        *count = 1;
    } else {
        int cells[4][2], i;
        shape_cells(p->type, p->rot, cells);
        for (i = 0; i < 4; i++) {
            out[i][0] = p->r + cells[i][0];
            out[i][1] = p->c + cells[i][1];
        }
        *count = 4;
    }
}

int collides(const Game *g, const Piece *p) {
    int cells[4][2], n, i;
    piece_cells(p, cells, &n);
    for (i = 0; i < n; i++) {
        int rr = cells[i][0], cc = cells[i][1];
        if (cc < 0 || cc >= BOARD_W) return 1;
        if (rr >= BOARD_H)           return 1;
        if (rr >= 0 && g->grid[rr][cc] != 0) return 1;
    }
    return 0;
}

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

static void test_bag(void) {
    Game g;
    int seen[7];
    int i, t;
    rng_seed(&g, 99u);
    g.bag_idx = 7;                 /* force refill on first pop */
    for (i = 0; i < 7; i++) seen[i] = 0;
    for (i = 0; i < 7; i++) {
        t = bag_pop(&g);
        CHECK(t >= 0 && t < 7);
        seen[t]++;
    }
    for (i = 0; i < 7; i++) CHECK(seen[i] == 1);   /* every piece exactly once */

    /* second bag also contains all 7 */
    for (i = 0; i < 7; i++) seen[i] = 0;
    for (i = 0; i < 7; i++) seen[bag_pop(&g)]++;
    for (i = 0; i < 7; i++) CHECK(seen[i] == 1);
}

static void test_rng(void) {
    Game a, b;
    rng_seed(&a, 12345u);
    rng_seed(&b, 12345u);
    /* deterministic: same seed -> same stream */
    CHECK(rng_next(&a) == rng_next(&b));
    CHECK(rng_next(&a) == rng_next(&b));
    /* range is bounded */
    {
        int i, ok = 1;
        for (i = 0; i < 1000; i++) {
            int v = rng_range(&a, 7);
            if (v < 0 || v >= 7) ok = 0;
        }
        CHECK(ok);
    }
    /* seed 0 must not lock the generator at 0 */
    rng_seed(&a, 0u);
    CHECK(rng_next(&a) != 0u);
}

static int has_cell(int cells[4][2], int n, int r, int c) {
    int i;
    for (i = 0; i < n; i++) if (cells[i][0] == r && cells[i][1] == c) return 1;
    return 0;
}

static void test_shapes(void) {
    int cells[4][2];

    /* I spawn: row 1, cols 0..3 */
    shape_cells(0, 0, cells);
    CHECK(has_cell(cells, 4, 1, 0) && has_cell(cells, 4, 1, 1) &&
          has_cell(cells, 4, 1, 2) && has_cell(cells, 4, 1, 3));

    /* I rotated once: column 2, rows 0..3 */
    shape_cells(0, 1, cells);
    CHECK(has_cell(cells, 4, 0, 2) && has_cell(cells, 4, 1, 2) &&
          has_cell(cells, 4, 2, 2) && has_cell(cells, 4, 3, 2));

    /* O spawn: 2x2 at rows 0..1, cols 1..2 */
    shape_cells(1, 0, cells);
    CHECK(has_cell(cells, 4, 0, 1) && has_cell(cells, 4, 0, 2) &&
          has_cell(cells, 4, 1, 1) && has_cell(cells, 4, 1, 2));

    /* T spawn: (0,1),(1,0),(1,1),(1,2) */
    shape_cells(2, 0, cells);
    CHECK(has_cell(cells, 4, 0, 1) && has_cell(cells, 4, 1, 0) &&
          has_cell(cells, 4, 1, 1) && has_cell(cells, 4, 1, 2));

    /* rot is taken mod 4: rot 4 == rot 0 for any piece */
    {
        int a[4][2], b[4][2], i, same = 1;
        shape_cells(5, 0, a);
        shape_cells(5, 4, b);
        for (i = 0; i < 4; i++)
            if (!has_cell(b, 4, a[i][0], a[i][1])) same = 0;
        CHECK(same);
    }
}

static void test_collision(void) {
    Game g;
    Piece p;
    memset(&g, 0, sizeof g);

    /* normal I at spawn (r=0,c=3) does not collide on an empty board */
    p.type = 0; p.rot = 0; p.r = 0; p.c = 3; p.special = 0;
    CHECK(!collides(&g, &p));

    /* push it off the right edge */
    p.c = BOARD_W;
    CHECK(collides(&g, &p));

    /* below the floor collides */
    p.c = 3; p.r = BOARD_H;
    CHECK(collides(&g, &p));

    /* overlap with a filled cell collides */
    p.r = 0; p.c = 3;
    g.grid[1][3] = 5;          /* I at rot0 fills row1 cols3..6 */
    CHECK(collides(&g, &p));

    /* special piece occupies a single cell at (r+1,c+1) */
    {
        int cells[4][2], n;
        p.special = 1; p.r = 4; p.c = 4;
        piece_cells(&p, cells, &n);
        CHECK(n == 1);
        CHECK(cells[0][0] == 5 && cells[0][1] == 5);
    }
}

static void test_harness(void) {
    CHECK(1 == 1);
}

int main(void) {
    test_bag();
    test_rng();
    test_shapes();
    test_collision();
    test_harness();
    printf("\n%d checks, %d failures\n", g_tests, g_fails);
    return g_fails ? 1 : 0;
}
#endif /* UNIT_TEST */
