# C Tetris (power-up twist) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a playable, real-time terminal Tetris in a single C file whose twist is a power-up system — falling special pieces (bomb, laser) plus a charge-based inventory (slow-mo, line-nuke, swap).

**Architecture:** One source file, `tetris.c`. All game state lives in a `Game` struct passed by pointer (no globals for mutable state). Pure logic (board, pieces, rotation, line clearing, power-ups, scoring, PRNG, high-score I/O) is separated from interactive I/O (keyboard, rendering, timing). Unit tests live in the same file behind `#ifdef UNIT_TEST` with their own `main()`, so the shipped game stays one file while logic is compile-tested. Interactive functions are excluded from the test build behind `#ifndef UNIT_TEST`.

**Tech Stack:** C11, C standard library only, plus two explicitly-allowed Windows headers: `conio.h` (`_kbhit`/`_getch` — non-blocking input) and `windows.h` (`Sleep` — CPU-friendly frame pacing). Build with MinGW-w64/`gcc`.

## Global Constraints

These apply to **every** task. Copy them verbatim into your mental checklist before each commit.

- Language: **C11**, a **single source file** `tetris.c`. No second `.c`/`.h` is shipped.
- **Only the C standard library**, with exactly two allowed non-standard headers: `conio.h` (for `_kbhit`, `_getch`) and `windows.h` (for `Sleep` only).
- **No GNU/compiler extensions.** Code must compile clean under `gcc -std=c11 -Wall -Wextra -pedantic` (system headers excepted).
- All mutable game state lives in `struct Game`, passed by pointer. No mutable globals.
- PRNG is a self-contained xorshift32 seeded from `Game.rng`, so tests are deterministic.
- Board is **10 wide × 20 tall** (`BOARD_W` = 10, `BOARD_H` = 20).
- Cell color ids: `1=I, 2=O, 3=T, 4=S, 5=Z, 6=J, 7=L`; `0` = empty.
- Power-up ids: `PU_SLOWMO=1, PU_NUKE=2, PU_SWAP=3`. Special piece kinds: `1=bomb, 2=laser`.
- Tunables are `#define`s near the top: `SPECIAL_CHANCE=8` (%), `CHARGE_PER_POWERUP=4`, `INVENTORY_SLOTS=3`, `SLOWMO_MS=8000`, `SLOWMO_FACTOR_NUM=5`, `SLOWMO_FACTOR_DEN=2`, `BOMB_RADIUS=1`, `START_INTERVAL_MS=800`, `MIN_INTERVAL_MS=80`, `LEVEL_STEP_MS=60`, `LINES_PER_LEVEL=10`, `FRAME_MS=16`.
- Build commands (used throughout):
  - Game: `gcc -std=c11 -O2 -Wall -Wextra -pedantic -o tetris tetris.c`
  - Tests: `gcc -std=c11 -DUNIT_TEST -Wall -Wextra -pedantic -o tetris_test tetris.c && ./tetris_test`
- Run the game in a VT-capable terminal (Windows Terminal). A `-DUSE_CLS` build is the colorless `system("cls")` fallback for non-VT consoles.

---

## File Structure

| File | Responsibility |
|---|---|
| `tetris.c` | The entire game **and** the embedded unit tests (behind `#ifdef UNIT_TEST`). |
| `README.md` | Build + run + controls (Task 15). |
| `highscore.txt` | Created at runtime by the game (`fopen`). Not committed. |

`tetris.c` is laid out top-to-bottom:
1. Includes + `#define` tunables.
2. Enums + `Piece` + `Game` structs.
3. Pure logic (always compiled): PRNG, bag, shapes, collision, movement, locking, line clear, scoring, power-ups, spawn, high-score I/O, `game_init`, `process_input`.
4. `#ifndef UNIT_TEST` block: interactive I/O (`read_key`, `sleep_ms`, rendering) + game `main()`.
5. `#ifdef UNIT_TEST` block: `CHECK` macro, test functions, test `main()`.

> **Why this is one file:** the user's hard constraint is a single file. The stb-style `#ifdef UNIT_TEST` pattern keeps tests and game in `tetris.c`; `-DUNIT_TEST` compiles the test `main`, the default build compiles the game `main`. Test code is compiled out of the release binary.

---

### Task 0: Scaffold the single file + test harness

**Files:**
- Create: `tetris.c`

**Interfaces:**
- Produces: the compile skeleton — includes, all tunable `#define`s, the `CHECK` test macro, an empty game `main()` (default build) and a test `main()` (`-DUNIT_TEST` build) that reports `0 failures`.

- [ ] **Step 1: Write the failing test**

Create `tetris.c` with the full scaffold below. The first test asserts a trivial truth so the harness itself is exercised.

```c
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
```

> **Structure invariant (do not break in later tasks):** the file ends with exactly two balanced, mutually-exclusive blocks — first `#ifndef UNIT_TEST { ... game main } #endif`, then `#ifdef UNIT_TEST { ... test main } #endif`. Tasks 12–14 only add functions *inside* the `#ifndef` block or replace the placeholder `main` body; they never edit an `#if*`/`#endif` line.

- [ ] **Step 2: Run the test build to verify the harness works**

Run: `gcc -std=c11 -DUNIT_TEST -Wall -Wextra -pedantic -o tetris_test tetris.c && ./tetris_test`
Expected: compiles clean; prints `1 checks, 0 failures`; exit 0.

> If `gcc` is missing, this is the moment to install it (see Task 15 / README). On Windows: `winget install BrechtSanders.WinLibs.POSIX.UCRT` or unzip **w64devkit** and put its `bin` on PATH, then re-run.

- [ ] **Step 3: Verify the game build also compiles**

Run: `gcc -std=c11 -O2 -Wall -Wextra -pedantic -o tetris tetris.c`
Expected: compiles clean, no warnings.

- [ ] **Step 4: Commit**

```bash
git add tetris.c
git commit -m "scaffold: single-file tetris with #ifdef UNIT_TEST test harness"
```

---

### Task 1: PRNG (xorshift32)

**Files:**
- Modify: `tetris.c` (add PRNG functions + a test)

**Interfaces:**
- Produces: `void rng_seed(Game*, uint32_t)`, `uint32_t rng_next(Game*)`, `int rng_range(Game*, int n)` returning a value in `[0, n)`.
- Consumes: `Game.rng`.

- [ ] **Step 1: Write the failing test**

Add this test function above `test_harness` and call it from `main` (test build):

```c
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
```

In test `main`, add `test_rng();` before the summary print.

- [ ] **Step 2: Run test build to verify it fails**

Run: `gcc -std=c11 -DUNIT_TEST -Wall -Wextra -pedantic -o tetris_test tetris.c && ./tetris_test`
Expected: FAIL — link error `undefined reference to 'rng_seed'` (functions not yet defined).

- [ ] **Step 3: Write minimal implementation**

Add the definitions in the always-compiled logic area (after the forward declarations, before `#ifdef UNIT_TEST`):

```c
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
```

- [ ] **Step 4: Run test build to verify it passes**

Run: `gcc -std=c11 -DUNIT_TEST -Wall -Wextra -pedantic -o tetris_test tetris.c && ./tetris_test`
Expected: PASS — `0 failures`.

- [ ] **Step 5: Commit**

```bash
git add tetris.c
git commit -m "feat: deterministic xorshift32 PRNG"
```

---

### Task 2: 7-bag piece generator

**Files:**
- Modify: `tetris.c`

**Interfaces:**
- Produces: `void bag_refill(Game*)` (fills `bag[0..6]` with a Fisher-Yates shuffle of 0..6, sets `bag_idx=0`), `int bag_pop(Game*)` (returns next type, refilling when exhausted).
- Consumes: `rng_range`, `Game.bag`, `Game.bag_idx`.
- Add forward declarations near the others: `void bag_refill(Game*); int bag_pop(Game*);`

- [ ] **Step 1: Write the failing test**

```c
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
```

Call `test_bag();` from test `main`.

- [ ] **Step 2: Run test build to verify it fails**

Run: `gcc -std=c11 -DUNIT_TEST -Wall -Wextra -pedantic -o tetris_test tetris.c && ./tetris_test`
Expected: FAIL — `undefined reference to 'bag_pop'`.

- [ ] **Step 3: Write minimal implementation**

```c
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
```

- [ ] **Step 4: Run test build to verify it passes**

Run: `gcc -std=c11 -DUNIT_TEST -Wall -Wextra -pedantic -o tetris_test tetris.c && ./tetris_test`
Expected: PASS — `0 failures`.

- [ ] **Step 5: Commit**

```bash
git add tetris.c
git commit -m "feat: 7-bag piece generator"
```

---

### Task 3: Tetromino shapes + rotation

**Files:**
- Modify: `tetris.c`

**Interfaces:**
- Produces: `void shape_cells(int type, int rot, int out[4][2])` — writes the 4 filled `(row,col)` offsets (within a 4x4 box) of tetromino `type` at rotation `rot`. Rotation is naive clockwise rotation of the base 4x4 grid (`new[r][c] = old[3-c][r]`).
- Consumes: `BASE_SHAPE` table.
- Add forward declaration: `void shape_cells(int type, int rot, int out[4][2]);`
- Test helper (add once, in the `#ifdef UNIT_TEST` block): `static int has_cell(int cells[4][2], int n, int r, int c)`.

- [ ] **Step 1: Write the failing test**

```c
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
```

Call `test_shapes();` from test `main`.

- [ ] **Step 2: Run test build to verify it fails**

Run: `gcc -std=c11 -DUNIT_TEST -Wall -Wextra -pedantic -o tetris_test tetris.c && ./tetris_test`
Expected: FAIL — `undefined reference to 'shape_cells'`.

- [ ] **Step 3: Write minimal implementation**

Add the base-shape table near the top (after the structs) and the function in the logic area:

```c
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
```

- [ ] **Step 4: Run test build to verify it passes**

Run: `gcc -std=c11 -DUNIT_TEST -Wall -Wextra -pedantic -o tetris_test tetris.c && ./tetris_test`
Expected: PASS — `0 failures`.

- [ ] **Step 5: Commit**

```bash
git add tetris.c
git commit -m "feat: tetromino base shapes + naive rotation"
```

---

### Task 4: Absolute cells + collision

**Files:**
- Modify: `tetris.c`

**Interfaces:**
- Produces:
  - `void piece_cells(const Piece *p, int out[4][2], int *count)` — absolute board cells; `count=4` for normal, `count=1` for special (the single cell at `(p->r+1, p->c+1)`).
  - `int collides(const Game *g, const Piece *p)` — 1 if any cell is out of horizontal bounds, below the floor, or overlaps a filled cell. Cells with `row < 0` (above the top) are allowed.
- Add forward declarations: `void piece_cells(const Piece*, int out[4][2], int*); int collides(const Game*, const Piece*);`

- [ ] **Step 1: Write the failing test**

```c
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
```

Call `test_collision();` from test `main`.

- [ ] **Step 2: Run test build to verify it fails**

Run: `gcc -std=c11 -DUNIT_TEST -Wall -Wextra -pedantic -o tetris_test tetris.c && ./tetris_test`
Expected: FAIL — `undefined reference to 'piece_cells'`.

- [ ] **Step 3: Write minimal implementation**

```c
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
```

- [ ] **Step 4: Run test build to verify it passes**

Run: `gcc -std=c11 -DUNIT_TEST -Wall -Wextra -pedantic -o tetris_test tetris.c && ./tetris_test`
Expected: PASS — `0 failures`.

- [ ] **Step 5: Commit**

```bash
git add tetris.c
git commit -m "feat: absolute piece cells + collision detection"
```

---

### Task 5: Movement + rotation with wall kicks

**Files:**
- Modify: `tetris.c`

**Interfaces:**
- Produces:
  - `int try_move(Game *g, int dr, int dc)` — move the current piece; returns 1 if it moved, 0 if blocked.
  - `int try_rotate(Game *g)` — rotate clockwise with wall kicks `{0,-1,+1,-2,+2}`; O (`type==1`) and specials do not rotate (return 0); returns 1 on success.
- Consumes: `collides`, `Game.cur`.
- Add forward declarations: `int try_move(Game*, int, int); int try_rotate(Game*);`

- [ ] **Step 1: Write the failing test**

```c
static void test_movement(void) {
    Game g;
    memset(&g, 0, sizeof g);
    g.cur.type = 2; g.cur.rot = 0; g.cur.r = 0; g.cur.c = 3; g.cur.special = 0;

    CHECK(try_move(&g, 0, -1));        /* left ok */
    CHECK(g.cur.c == 2);
    CHECK(try_move(&g, 0, 1));         /* right ok */
    CHECK(g.cur.c == 3);
    CHECK(try_move(&g, 1, 0));         /* down ok */
    CHECK(g.cur.r == 1);

    /* against the left wall, moving left fails and position is unchanged */
    g.cur.c = 0; g.cur.r = 0; g.cur.rot = 0;   /* T at col0: leftmost filled col is 0 */
    CHECK(!try_move(&g, 0, -1));
    CHECK(g.cur.c == 0);

    /* rotation changes rot (T is not O) */
    g.cur.type = 2; g.cur.c = 3; g.cur.r = 0; g.cur.rot = 0;
    CHECK(try_rotate(&g));
    CHECK(g.cur.rot == 1);

    /* O does not rotate */
    g.cur.type = 1; g.cur.rot = 0;
    CHECK(!try_rotate(&g));
    CHECK(g.cur.rot == 0);

    /* special does not rotate */
    g.cur.special = 1; g.cur.rot = 0;
    CHECK(!try_rotate(&g));
}
```

Call `test_movement();` from test `main`.

- [ ] **Step 2: Run test build to verify it fails**

Run: `gcc -std=c11 -DUNIT_TEST -Wall -Wextra -pedantic -o tetris_test tetris.c && ./tetris_test`
Expected: FAIL — `undefined reference to 'try_move'`.

- [ ] **Step 3: Write minimal implementation**

```c
int try_move(Game *g, int dr, int dc) {
    Piece p = g->cur;
    p.r += dr; p.c += dc;
    if (collides(g, &p)) return 0;
    g->cur = p;
    return 1;
}

int try_rotate(Game *g) {
    static const int kicks[5] = {0, -1, 1, -2, 2};
    int i;
    if (g->cur.special) return 0;      /* specials don't rotate */
    if (g->cur.type == 1) return 0;    /* O rotation is a no-op  */
    for (i = 0; i < 5; i++) {
        Piece p = g->cur;
        p.rot = (p.rot + 1) & 3;
        p.c += kicks[i];
        if (!collides(g, &p)) { g->cur = p; return 1; }
    }
    return 0;
}
```

- [ ] **Step 4: Run test build to verify it passes**

Run: `gcc -std=c11 -DUNIT_TEST -Wall -Wextra -pedantic -o tetris_test tetris.c && ./tetris_test`
Expected: PASS — `0 failures`.

- [ ] **Step 5: Commit**

```bash
git add tetris.c
git commit -m "feat: piece movement + rotation with wall kicks"
```

---

### Task 6: Locking, line clearing, scoring

**Files:**
- Modify: `tetris.c`

**Interfaces:**
- Produces:
  - `void place_piece(Game *g)` — write the current (normal) piece's cells into the grid using color `type+1`.
  - `int clear_lines(Game *g)` — remove full rows, shift everything above down, return number cleared.
  - `int score_for_lines(int n, int level)` — 0/40/100/300/1200 × `(level+1)`.
  - `void add_lines(Game *g, int n)` — update `score`, `lines`, and `level = lines / LINES_PER_LEVEL`.
- Add forward declarations: `void place_piece(Game*); int clear_lines(Game*); int score_for_lines(int,int); void add_lines(Game*,int);`

- [ ] **Step 1: Write the failing test**

```c
static void test_lines_scoring(void) {
    Game g;
    int c;
    memset(&g, 0, sizeof g);

    /* fill the bottom row except one cell, then place a piece to complete it */
    for (c = 0; c < BOARD_W - 1; c++) g.grid[BOARD_H - 1][c] = 1;
    CHECK(clear_lines(&g) == 0);                 /* not full yet */
    g.grid[BOARD_H - 1][BOARD_W - 1] = 1;        /* now full */
    CHECK(clear_lines(&g) == 1);
    /* row is gone (empty board again) */
    {
        int empty = 1, r;
        for (r = 0; r < BOARD_H; r++)
            for (c = 0; c < BOARD_W; c++)
                if (g.grid[r][c]) empty = 0;
        CHECK(empty);
    }

    /* two full rows clear together and content above shifts down */
    memset(&g, 0, sizeof g);
    for (c = 0; c < BOARD_W; c++) { g.grid[BOARD_H-1][c] = 1; g.grid[BOARD_H-2][c] = 1; }
    g.grid[BOARD_H-3][0] = 7;                     /* a marker above */
    CHECK(clear_lines(&g) == 2);
    CHECK(g.grid[BOARD_H-1][0] == 7);             /* marker fell to the bottom */

    /* scoring */
    CHECK(score_for_lines(1, 0) == 40);
    CHECK(score_for_lines(4, 0) == 1200);
    CHECK(score_for_lines(2, 1) == 200);         /* 100 * (1+1) */

    /* add_lines updates score/lines/level */
    memset(&g, 0, sizeof g);
    add_lines(&g, 4);
    CHECK(g.score == 1200 && g.lines == 4 && g.level == 0);
    add_lines(&g, 4);
    add_lines(&g, 4);
    CHECK(g.lines == 12 && g.level == 1);        /* 12/10 = 1 */
}
```

Call `test_lines_scoring();` from test `main`.

- [ ] **Step 2: Run test build to verify it fails**

Run: `gcc -std=c11 -DUNIT_TEST -Wall -Wextra -pedantic -o tetris_test tetris.c && ./tetris_test`
Expected: FAIL — `undefined reference to 'clear_lines'`.

- [ ] **Step 3: Write minimal implementation**

```c
void place_piece(Game *g) {
    int cells[4][2], n, i;
    int color = g->cur.type + 1;
    piece_cells(&g->cur, cells, &n);
    for (i = 0; i < n; i++) {
        int rr = cells[i][0], cc = cells[i][1];
        if (rr >= 0 && rr < BOARD_H && cc >= 0 && cc < BOARD_W)
            g->grid[rr][cc] = color;
    }
}

int clear_lines(Game *g) {
    int r, c, w, cleared = 0;
    for (r = BOARD_H - 1; r >= 0; r--) {
        int full = 1;
        for (c = 0; c < BOARD_W; c++)
            if (g->grid[r][c] == 0) { full = 0; break; }
        if (full) {
            int rr;
            for (rr = r; rr > 0; rr--)
                for (w = 0; w < BOARD_W; w++)
                    g->grid[rr][w] = g->grid[rr - 1][w];
            for (w = 0; w < BOARD_W; w++) g->grid[0][w] = 0;
            cleared++;
            r++;                 /* re-check the same row after the shift */
        }
    }
    return cleared;
}

int score_for_lines(int n, int level) {
    int base;
    switch (n) {
        case 1: base = 40;   break;
        case 2: base = 100;  break;
        case 3: base = 300;  break;
        case 4: base = 1200; break;
        default: base = 0;   break;
    }
    return base * (level + 1);
}

void add_lines(Game *g, int n) {
    if (n <= 0) return;
    g->score += score_for_lines(n, g->level);
    g->lines += n;
    g->level = g->lines / LINES_PER_LEVEL;
}
```

- [ ] **Step 4: Run test build to verify it passes**

Run: `gcc -std=c11 -DUNIT_TEST -Wall -Wextra -pedantic -o tetris_test tetris.c && ./tetris_test`
Expected: PASS — `0 failures`.

- [ ] **Step 5: Commit**

```bash
git add tetris.c
git commit -m "feat: locking, line clearing, scoring"
```

---

### Task 7: Spawn, game-over, gravity interval

**Files:**
- Modify: `tetris.c`

**Interfaces:**
- Produces:
  - `void spawn_piece(Game *g)` — set `cur` from `next_type` at `(r=0,c=3)`, roll a special with `SPECIAL_CHANCE`, advance `next_type = bag_pop`, set `game_over` if the new piece immediately collides.
  - `int gravity_interval_ms(const Game *g)` — `START_INTERVAL_MS - level*LEVEL_STEP_MS`, floored at `MIN_INTERVAL_MS`, multiplied by `SLOWMO_FACTOR_NUM/DEN` while slow-mo is active.
- Consumes: `bag_pop`, `collides`, `rng_range`.
- Add forward declarations: `void spawn_piece(Game*); int gravity_interval_ms(const Game*);`

- [ ] **Step 1: Write the failing test**

```c
static void test_spawn_gravity(void) {
    Game g;
    memset(&g, 0, sizeof g);
    rng_seed(&g, 7u);
    g.bag_idx = 7;
    g.next_type = bag_pop(&g);
    spawn_piece(&g);
    CHECK(!g.game_over);
    CHECK(g.cur.r == 0 && g.cur.c == 3);
    CHECK(g.cur.type >= 0 && g.cur.type < 7);

    /* fill the top rows so a fresh spawn collides -> game over */
    memset(&g.grid, 0, sizeof g.grid);
    {
        int r, c;
        for (r = 0; r < 4; r++)
            for (c = 0; c < BOARD_W; c++) g.grid[r][c] = 1;
    }
    g.game_over = 0;
    spawn_piece(&g);
    CHECK(g.game_over);

    /* gravity interval: faster with level, floored, slower under slow-mo */
    memset(&g, 0, sizeof g);
    g.level = 0; g.slowmo_ms_left = 0;
    CHECK(gravity_interval_ms(&g) == START_INTERVAL_MS);
    g.level = 100;                                  /* would go negative -> floored */
    CHECK(gravity_interval_ms(&g) == MIN_INTERVAL_MS);
    g.level = 0; g.slowmo_ms_left = 500;
    CHECK(gravity_interval_ms(&g) ==
          START_INTERVAL_MS * SLOWMO_FACTOR_NUM / SLOWMO_FACTOR_DEN);
}

/* The headline twist: spawn_piece must actually roll special pieces. */
static void test_special_spawn(void) {
    Game g;
    int i, saw_normal = 0, saw_special = 0, kinds_ok = 1;
    game_init(&g, 1234u);
    for (i = 0; i < 400; i++) {
        if (g.cur.special == 0) saw_normal = 1; else saw_special = 1;
        if (g.cur.special < 0 || g.cur.special > 2) kinds_ok = 0;
        g.game_over = 0;            /* board stays empty; keep spawning */
        spawn_piece(&g);
    }
    CHECK(saw_normal);              /* normal pieces appear            */
    CHECK(saw_special);             /* ...and special ones (the twist) */
    CHECK(kinds_ok);                /* special is only ever 0, 1, or 2 */
}
```

Call `test_spawn_gravity();` and `test_special_spawn();` from test `main`.

- [ ] **Step 2: Run test build to verify it fails**

Run: `gcc -std=c11 -DUNIT_TEST -Wall -Wextra -pedantic -o tetris_test tetris.c && ./tetris_test`
Expected: FAIL — `undefined reference to 'spawn_piece'`.

- [ ] **Step 3: Write minimal implementation**

```c
void spawn_piece(Game *g) {
    Piece p;
    p.type = g->next_type;
    p.rot = 0;
    p.r = 0; p.c = 3;
    p.special = 0;
    if (rng_range(g, 100) < SPECIAL_CHANCE)
        p.special = 1 + rng_range(g, 2);   /* 1 bomb, 2 laser */
    g->next_type = bag_pop(g);
    g->cur = p;
    if (collides(g, &g->cur)) g->game_over = 1;
}

int gravity_interval_ms(const Game *g) {
    int base = START_INTERVAL_MS - g->level * LEVEL_STEP_MS;
    if (base < MIN_INTERVAL_MS) base = MIN_INTERVAL_MS;
    if (g->slowmo_ms_left > 0)
        base = base * SLOWMO_FACTOR_NUM / SLOWMO_FACTOR_DEN;
    return base;
}
```

- [ ] **Step 4: Run test build to verify it passes**

Run: `gcc -std=c11 -DUNIT_TEST -Wall -Wextra -pedantic -o tetris_test tetris.c && ./tetris_test`
Expected: PASS — `0 failures`.

- [ ] **Step 5: Commit**

```bash
git add tetris.c
git commit -m "feat: spawning, game-over, gravity interval"
```

---

### Task 8: Power-up twist — special pieces (bomb, laser) + lock integration

**Files:**
- Modify: `tetris.c`

**Interfaces:**
- Produces:
  - `void apply_bomb(Game *g, int cr, int cc)` — clear the `3x3` (radius `BOMB_RADIUS`) around `(cr,cc)`, bounds-checked.
  - `void apply_laser(Game *g, int cr, int cc)` — clear the full row `cr` and full column `cc`.
  - `void lock_piece(Game *g)` — the integrator: for a special piece apply its effect at `(r+1,c+1)`; for a normal piece `place_piece`; then `clear_lines`, `add_lines`, `add_charge` (Task 9 defines `add_charge` — declare it now), then `spawn_piece`.
  - `void hard_drop(Game *g)` — move down until blocked, then `lock_piece`.
- Add forward declarations: `void apply_bomb(Game*,int,int); void apply_laser(Game*,int,int); void lock_piece(Game*); void hard_drop(Game*); void add_charge(Game*,int);`

> `add_charge` is implemented in Task 9; declaring it here lets `lock_piece` call it. Until Task 9 it would not link — so this task's "passes" step is reached only after Task 9. To keep each task independently green, **provide a temporary stub** `void add_charge(Game *g, int n) { (void)g; (void)n; }` in this task and replace it with the real body in Task 9.

- [ ] **Step 1: Write the failing test**

```c
static void test_special_pieces(void) {
    Game g;
    int r, c;

    /* bomb clears a 3x3 around its single cell (r+1,c+1) */
    memset(&g, 0, sizeof g);
    rng_seed(&g, 1u); g.bag_idx = 7; g.next_type = bag_pop(&g);
    for (r = 0; r < BOARD_H; r++)
        for (c = 0; c < BOARD_W; c++) g.grid[r][c] = 1;
    apply_bomb(&g, 10, 5);
    for (r = 9; r <= 11; r++)
        for (c = 4; c <= 6; c++) CHECK(g.grid[r][c] == 0);
    CHECK(g.grid[10][7] == 1);                  /* outside blast intact */

    /* laser clears full row + column */
    memset(&g, 0, sizeof g);
    for (r = 0; r < BOARD_H; r++)
        for (c = 0; c < BOARD_W; c++) g.grid[r][c] = 1;
    apply_laser(&g, 8, 3);
    for (c = 0; c < BOARD_W; c++) CHECK(g.grid[8][c] == 0);   /* row */
    for (r = 0; r < BOARD_H; r++) CHECK(g.grid[r][3] == 0);   /* col */
    CHECK(g.grid[0][0] == 1);                                 /* elsewhere intact */

    /* lock of a bomb piece detonates and then spawns the next piece */
    memset(&g, 0, sizeof g);
    rng_seed(&g, 2u); g.bag_idx = 7; g.next_type = bag_pop(&g);
    for (c = 0; c < BOARD_W; c++) g.grid[BOARD_H-1][c] = 1;   /* full bottom row */
    g.cur.special = 1; g.cur.type = 0; g.cur.rot = 0;
    g.cur.r = BOARD_H - 2; g.cur.c = 4;       /* single cell at (H-1, 5) */
    lock_piece(&g);
    CHECK(g.grid[BOARD_H-1][5] == 0);          /* blasted */
    CHECK(g.cur.r == 0 && g.cur.c == 3);       /* a fresh piece spawned at top (RNG-independent) */
}
```

Call `test_special_pieces();` from test `main`.

- [ ] **Step 2: Run test build to verify it fails**

Run: `gcc -std=c11 -DUNIT_TEST -Wall -Wextra -pedantic -o tetris_test tetris.c && ./tetris_test`
Expected: FAIL — `undefined reference to 'apply_bomb'`.

- [ ] **Step 3: Write minimal implementation**

```c
void apply_bomb(Game *g, int cr, int cc) {
    int r, c;
    for (r = cr - BOMB_RADIUS; r <= cr + BOMB_RADIUS; r++)
        for (c = cc - BOMB_RADIUS; c <= cc + BOMB_RADIUS; c++)
            if (r >= 0 && r < BOARD_H && c >= 0 && c < BOARD_W)
                g->grid[r][c] = 0;
}

void apply_laser(Game *g, int cr, int cc) {
    int i;
    if (cr >= 0 && cr < BOARD_H)
        for (i = 0; i < BOARD_W; i++) g->grid[cr][i] = 0;
    if (cc >= 0 && cc < BOARD_W)
        for (i = 0; i < BOARD_H; i++) g->grid[i][cc] = 0;
}

/* temporary stub — replaced in Task 9 */
void add_charge(Game *g, int n) { (void)g; (void)n; }

void lock_piece(Game *g) {
    int n_cleared;
    if (g->cur.special) {
        int cr = g->cur.r + 1, cc = g->cur.c + 1;
        if (g->cur.special == 1) apply_bomb(g, cr, cc);
        else                     apply_laser(g, cr, cc);
    } else {
        place_piece(g);
    }
    n_cleared = clear_lines(g);
    add_lines(g, n_cleared);
    add_charge(g, n_cleared);
    spawn_piece(g);
}

void hard_drop(Game *g) {
    while (try_move(g, 1, 0)) { /* fall until blocked */ }
    lock_piece(g);
}
```

- [ ] **Step 4: Run test build to verify it passes**

Run: `gcc -std=c11 -DUNIT_TEST -Wall -Wextra -pedantic -o tetris_test tetris.c && ./tetris_test`
Expected: PASS — `0 failures`.

- [ ] **Step 5: Commit**

```bash
git add tetris.c
git commit -m "feat: special pieces (bomb, laser) + lock integration"
```

---

### Task 9: Power-up twist — charge meter + inventory (slow-mo, nuke, swap)

**Files:**
- Modify: `tetris.c`

**Interfaces:**
- Produces:
  - `int inv_add(Game *g, int pu)` — append to inventory if room; return 1 if added.
  - `int grant_powerup(Game *g)` — add a random `PU_*` (1..3); return its id or 0 if full.
  - `void add_charge(Game *g, int lines_cleared)` — add charges; for each `CHARGE_PER_POWERUP` reached, grant a power-up. **Replaces the Task 8 stub.**
  - `void apply_nuke(Game *g)` — remove the bottom 2 rows, shift everything down.
  - `void apply_swap(Game *g)` — turn the current piece into an `I` (type 0, rot 0); keep position if it fits, else retry at `(0,3)`, else leave unchanged.
  - `void use_powerup(Game *g, int slot)` — apply and remove the inventory item in `slot` (0-based).
- Add forward declarations: `int inv_add(Game*,int); int grant_powerup(Game*); void apply_nuke(Game*); void apply_swap(Game*); void use_powerup(Game*,int);` (`add_charge` already declared in Task 8.)

- [ ] **Step 1: Replace the Task 8 stub**

Delete the temporary `void add_charge(Game *g, int n) { (void)g; (void)n; }` line from Task 8.

- [ ] **Step 2: Write the failing test**

```c
static void test_powerups(void) {
    Game g;
    int c;

    /* charge meter grants a power-up every CHARGE_PER_POWERUP lines */
    memset(&g, 0, sizeof g);
    rng_seed(&g, 3u);
    add_charge(&g, CHARGE_PER_POWERUP);     /* exactly one threshold */
    CHECK(g.inv_count == 1);
    CHECK(g.charge == 0);
    CHECK(g.inv[0] >= PU_SLOWMO && g.inv[0] <= PU_SWAP);

    /* inventory caps at INVENTORY_SLOTS */
    memset(&g, 0, sizeof g);
    rng_seed(&g, 3u);
    add_charge(&g, CHARGE_PER_POWERUP * 10);
    CHECK(g.inv_count == INVENTORY_SLOTS);

    /* slow-mo sets the timer */
    memset(&g, 0, sizeof g);
    g.inv_count = 1; g.inv[0] = PU_SLOWMO;
    use_powerup(&g, 0);
    CHECK(g.slowmo_ms_left == SLOWMO_MS);
    CHECK(g.inv_count == 0);

    /* nuke clears the bottom two rows and shifts down */
    memset(&g, 0, sizeof g);
    for (c = 0; c < BOARD_W; c++) {
        g.grid[BOARD_H-1][c] = 1; g.grid[BOARD_H-2][c] = 2;
    }
    g.grid[BOARD_H-3][0] = 7;
    g.inv_count = 1; g.inv[0] = PU_NUKE;
    use_powerup(&g, 0);
    CHECK(g.grid[BOARD_H-1][0] == 7);           /* marker dropped 2 rows */
    CHECK(g.grid[BOARD_H-2][1] == 0);

    /* swap turns the current piece into an I */
    memset(&g, 0, sizeof g);
    g.cur.type = 5; g.cur.rot = 0; g.cur.r = 0; g.cur.c = 3; g.cur.special = 0;
    g.inv_count = 1; g.inv[0] = PU_SWAP;
    use_powerup(&g, 0);
    CHECK(g.cur.type == 0);

    /* using an empty slot is a no-op */
    memset(&g, 0, sizeof g);
    use_powerup(&g, 0);
    CHECK(g.inv_count == 0);
}
```

Call `test_powerups();` from test `main`.

- [ ] **Step 3: Run test build to verify it fails**

Run: `gcc -std=c11 -DUNIT_TEST -Wall -Wextra -pedantic -o tetris_test tetris.c && ./tetris_test`
Expected: FAIL — `undefined reference to 'inv_add'` (and the stub removal makes `add_charge` undefined too).

- [ ] **Step 4: Write minimal implementation**

```c
int inv_add(Game *g, int pu) {
    if (g->inv_count >= INVENTORY_SLOTS) return 0;
    g->inv[g->inv_count++] = pu;
    return 1;
}

int grant_powerup(Game *g) {
    int pu = PU_SLOWMO + rng_range(g, 3);   /* 1..3 */
    return inv_add(g, pu) ? pu : 0;
}

void add_charge(Game *g, int lines_cleared) {
    if (lines_cleared <= 0) return;
    g->charge += lines_cleared;
    while (g->charge >= CHARGE_PER_POWERUP) {
        g->charge -= CHARGE_PER_POWERUP;
        grant_powerup(g);
    }
}

void apply_nuke(Game *g) {
    int r, c, k;
    for (k = 0; k < 2; k++) {
        for (r = BOARD_H - 1; r > 0; r--)
            for (c = 0; c < BOARD_W; c++)
                g->grid[r][c] = g->grid[r - 1][c];
        for (c = 0; c < BOARD_W; c++) g->grid[0][c] = 0;
    }
}

void apply_swap(Game *g) {
    Piece p = g->cur;
    if (p.special) return;
    p.type = 0; p.rot = 0;
    if (collides(g, &p)) {
        p.r = 0; p.c = 3;
        if (collides(g, &p)) return;   /* cannot swap right now */
    }
    g->cur = p;
}

void use_powerup(Game *g, int slot) {
    int pu, i;
    if (slot < 0 || slot >= g->inv_count) return;
    pu = g->inv[slot];
    for (i = slot; i < g->inv_count - 1; i++) g->inv[i] = g->inv[i + 1];
    g->inv_count--;
    g->inv[g->inv_count] = PU_NONE;
    switch (pu) {
        case PU_SLOWMO: g->slowmo_ms_left = SLOWMO_MS; break;
        case PU_NUKE:   apply_nuke(g);                 break;
        case PU_SWAP:   apply_swap(g);                 break;
        default: break;
    }
}
```

- [ ] **Step 5: Run test build to verify it passes**

Run: `gcc -std=c11 -DUNIT_TEST -Wall -Wextra -pedantic -o tetris_test tetris.c && ./tetris_test`
Expected: PASS — `0 failures`.

- [ ] **Step 6: Commit**

```bash
git add tetris.c
git commit -m "feat: charge meter + power-up inventory (slowmo, nuke, swap)"
```

---

### Task 10: High-score persistence

**Files:**
- Modify: `tetris.c`

**Interfaces:**
- Produces: `long load_highscore(const char *path)` (returns 0 if missing/malformed), `void save_highscore(const char *path, long score)`.
- Add forward declarations: `long load_highscore(const char*); void save_highscore(const char*, long);`

- [ ] **Step 1: Write the failing test**

```c
static void test_highscore(void) {
    const char *path = "test_hs.tmp";
    remove(path);
    CHECK(load_highscore(path) == 0);     /* missing file -> 0 */
    save_highscore(path, 4242);
    CHECK(load_highscore(path) == 4242);  /* round-trips */
    remove(path);
}
```

Call `test_highscore();` from test `main`.

- [ ] **Step 2: Run test build to verify it fails**

Run: `gcc -std=c11 -DUNIT_TEST -Wall -Wextra -pedantic -o tetris_test tetris.c && ./tetris_test`
Expected: FAIL — `undefined reference to 'load_highscore'`.

- [ ] **Step 3: Write minimal implementation**

```c
long load_highscore(const char *path) {
    FILE *f = fopen(path, "r");
    long v = 0;
    if (f) {
        if (fscanf(f, "%ld", &v) != 1) v = 0;
        fclose(f);
    }
    return v;
}

void save_highscore(const char *path, long score) {
    FILE *f = fopen(path, "w");
    if (f) {
        fprintf(f, "%ld\n", score);
        fclose(f);
    }
}
```

- [ ] **Step 4: Run test build to verify it passes**

Run: `gcc -std=c11 -DUNIT_TEST -Wall -Wextra -pedantic -o tetris_test tetris.c && ./tetris_test`
Expected: PASS — `0 failures`.

- [ ] **Step 5: Commit**

```bash
git add tetris.c
git commit -m "feat: high-score load/save"
```

---

### Task 11: Game init + input processing

**Files:**
- Modify: `tetris.c`

**Interfaces:**
- Produces:
  - `void game_init(Game *g, uint32_t seed)` — zero the board, seed RNG, set `bag_idx=7`, reset counters/inventory, prime `next_type = bag_pop`, then `spawn_piece`.
  - `void process_input(Game *g, int key)` — apply a `KEY_*` to the game (movement, rotate, soft/hard drop, pause toggle, fire power-ups). Ignores input when `game_over`; only `KEY_PAUSE` works while paused.
- Consumes: all earlier logic.
- Add forward declarations: `void game_init(Game*, uint32_t); void process_input(Game*, int);`

> Both functions are always compiled (used by tests **and** the game), so they live in the pure-logic area, not behind `#ifndef UNIT_TEST`.

- [ ] **Step 1: Write the failing test**

```c
static void test_init_input(void) {
    Game g;
    game_init(&g, 42u);
    CHECK(!g.game_over && !g.paused);
    CHECK(g.score == 0 && g.lines == 0 && g.level == 0);
    CHECK(g.inv_count == 0 && g.charge == 0);
    CHECK(g.cur.type >= 0 && g.cur.type < 7);

    /* left / right move the piece */
    {
        int c0 = g.cur.c;
        process_input(&g, KEY_LEFT);
        CHECK(g.cur.c == c0 - 1);
        process_input(&g, KEY_RIGHT);
        CHECK(g.cur.c == c0);
    }

    /* pause blocks movement; unpause restores it */
    process_input(&g, KEY_PAUSE);
    CHECK(g.paused);
    {
        int c0 = g.cur.c;
        process_input(&g, KEY_LEFT);
        CHECK(g.cur.c == c0);             /* ignored while paused */
    }
    process_input(&g, KEY_PAUSE);
    CHECK(!g.paused);

    /* soft-drop two rows, then hard drop must lock + respawn at the top */
    process_input(&g, KEY_DOWN);
    process_input(&g, KEY_DOWN);
    CHECK(g.cur.r == 2);
    process_input(&g, KEY_DROP);
    CHECK(g.cur.r == 0);             /* r went 2 -> 0, proving a respawn happened */

    /* firing an empty power-up slot is harmless */
    process_input(&g, KEY_PU1);
    CHECK(g.inv_count == 0);

    /* no input is processed after game over */
    g.game_over = 1;
    {
        int c0 = g.cur.c;
        process_input(&g, KEY_LEFT);
        CHECK(g.cur.c == c0);
    }
}
```

Call `test_init_input();` from test `main`.

- [ ] **Step 2: Run test build to verify it fails**

Run: `gcc -std=c11 -DUNIT_TEST -Wall -Wextra -pedantic -o tetris_test tetris.c && ./tetris_test`
Expected: FAIL — `undefined reference to 'game_init'`.

- [ ] **Step 3: Write minimal implementation**

```c
void game_init(Game *g, uint32_t seed) {
    int r, c, i;
    for (r = 0; r < BOARD_H; r++)
        for (c = 0; c < BOARD_W; c++) g->grid[r][c] = 0;
    rng_seed(g, seed);
    g->bag_idx = 7;
    g->charge = 0;
    g->inv_count = 0;
    for (i = 0; i < INVENTORY_SLOTS; i++) g->inv[i] = PU_NONE;
    g->score = 0; g->lines = 0; g->level = 0;
    g->slowmo_ms_left = 0;
    g->game_over = 0; g->paused = 0;
    g->highscore = 0;
    g->next_type = bag_pop(g);
    spawn_piece(g);
}

void process_input(Game *g, int key) {
    if (g->game_over) return;
    if (key == KEY_PAUSE) { g->paused = !g->paused; return; }
    if (g->paused) return;
    switch (key) {
        case KEY_LEFT:  try_move(g, 0, -1); break;
        case KEY_RIGHT: try_move(g, 0,  1); break;
        case KEY_DOWN:  if (!try_move(g, 1, 0)) lock_piece(g); break;
        case KEY_UP:    try_rotate(g); break;
        case KEY_DROP:  hard_drop(g);  break;
        case KEY_PU1:   use_powerup(g, 0); break;
        case KEY_PU2:   use_powerup(g, 1); break;
        case KEY_PU3:   use_powerup(g, 2); break;
        default: break;
    }
}
```

- [ ] **Step 4: Run test build to verify it passes**

Run: `gcc -std=c11 -DUNIT_TEST -Wall -Wextra -pedantic -o tetris_test tetris.c && ./tetris_test`
Expected: PASS — `0 failures`.

- [ ] **Step 5: Commit**

```bash
git add tetris.c
git commit -m "feat: game_init + process_input"
```

---

### Task 12: Keyboard reader (non-blocking) + frame pacing

**Files:**
- Modify: `tetris.c` (inside a new `#ifndef UNIT_TEST` block, before the game `main`)

**Interfaces:**
- Produces: `int read_key(void)` (non-blocking; maps arrow keys and WASD/space/p/q/1-3 to `KEY_*`; returns `KEY_NONE` if no key), `void sleep_ms(int ms)`.
- These are interactive and **not** unit-tested (no test build references them); they are compiled only in the game build to avoid unused-function warnings.

- [ ] **Step 1: Add the input functions**

Inside the existing `#ifndef UNIT_TEST` block (created in Task 0), add `sleep_ms` and `read_key` **above** the placeholder `int main`. Do not add a new `#ifndef`/`#endif` — the block already exists.

```c
void sleep_ms(int ms) {
    Sleep((DWORD)ms);
}

int read_key(void) {
    int ch;
    if (!_kbhit()) return KEY_NONE;
    ch = _getch();
    if (ch == 0 || ch == 224) {           /* extended (arrow) key prefix */
        int code = _getch();
        switch (code) {
            case 75: return KEY_LEFT;
            case 77: return KEY_RIGHT;
            case 72: return KEY_UP;
            case 80: return KEY_DOWN;
            default: return KEY_NONE;
        }
    }
    switch (ch) {
        case 'a': case 'A': return KEY_LEFT;
        case 'd': case 'D': return KEY_RIGHT;
        case 'w': case 'W': return KEY_UP;
        case 's': case 'S': return KEY_DOWN;
        case ' ':           return KEY_DROP;
        case 'p': case 'P': return KEY_PAUSE;
        case 'q': case 'Q': return KEY_QUIT;
        case '1':           return KEY_PU1;
        case '2':           return KEY_PU2;
        case '3':           return KEY_PU3;
        default:            return KEY_NONE;
    }
}
```

- [ ] **Step 2: Verify both builds still compile cleanly**

Run: `gcc -std=c11 -DUNIT_TEST -Wall -Wextra -pedantic -o tetris_test tetris.c && ./tetris_test`
Expected: PASS — `0 failures` (the `#ifndef UNIT_TEST` block is excluded from the test build).

Run: `gcc -std=c11 -O2 -Wall -Wextra -pedantic -o tetris tetris.c`
Expected: compiles clean. `read_key`/`sleep_ms` have external linkage, so being unused by the placeholder `main` does **not** trigger `-Wunused-function`.

- [ ] **Step 3: Commit**

```bash
git add tetris.c
git commit -m "feat: non-blocking keyboard reader + frame pacing"
```

---

### Task 13: Rendering (ANSI, with USE_CLS fallback)

**Files:**
- Modify: `tetris.c` (inside the `#ifndef UNIT_TEST` block, after `read_key`)

**Interfaces:**
- Produces: `void render_init(void)`, `void render_cleanup(void)`, `void render(const Game *g)`. Renders the board (with ghost + current piece overlay), header (score/level/lines), next-piece preview, charge meter, inventory, controls help, and pause/game-over overlays.
- Uses the `ESC(...)` macro so a `-DUSE_CLS` build is colorless and clears with `system("cls")`.

- [ ] **Step 1: Add the rendering code**

Add the macro block at the very top of the `#ifndef UNIT_TEST` section (above `sleep_ms`):

```c
#ifdef USE_CLS
  #define ESC(s) ""
  static void clr_home(void) { system("cls"); }
#else
  #define ESC(s) s
  static void clr_home(void) { fputs("\033[H", stdout); }
#endif

/* ANSI 256-color foreground per cell color id (1..7). */
static const char *CELL_COLOR[8] = {
    "",
    ESC("\033[38;5;51m"),    /* I cyan    */
    ESC("\033[38;5;226m"),   /* O yellow  */
    ESC("\033[38;5;201m"),   /* T magenta */
    ESC("\033[38;5;46m"),    /* S green   */
    ESC("\033[38;5;196m"),   /* Z red     */
    ESC("\033[38;5;33m"),    /* J blue    */
    ESC("\033[38;5;208m")    /* L orange  */
};
```

Then, after `read_key`, add the renderer:

```c
static void print_cell(int v) {
    switch (v) {
        case 0:  fputs("  ", stdout); break;                       /* empty */
        case 8:  fputs(ESC("\033[91m") "()" ESC("\033[0m"), stdout); break; /* bomb  */
        case 9:  fputs(ESC("\033[96m") "<>" ESC("\033[0m"), stdout); break; /* laser */
        case 10: fputs(ESC("\033[90m") "::" ESC("\033[0m"), stdout); break; /* ghost */
        default: printf("%s[]%s", CELL_COLOR[v], ESC("\033[0m")); break;
    }
}

static const char *PU_NAME(int pu) {
    switch (pu) {
        case PU_SLOWMO: return "Lassitas";
        case PU_NUKE:   return "Sortorlo";
        case PU_SWAP:   return "Csere";
        default:        return "-";
    }
}

void render_init(void) {
    fputs(ESC("\033[2J\033[?25l"), stdout);   /* clear screen + hide cursor */
}

void render_cleanup(void) {
    fputs(ESC("\033[?25h\033[0m"), stdout);   /* show cursor + reset */
    fputc('\n', stdout);
}

void render(const Game *g) {
    int buf[BOARD_H][BOARD_W];
    int cells[4][2], n, i, r, c;
    Piece ghost;

    /* base grid */
    for (r = 0; r < BOARD_H; r++)
        for (c = 0; c < BOARD_W; c++)
            buf[r][c] = g->grid[r][c];

    /* ghost: drop a copy until it would collide */
    ghost = g->cur;
    for (;;) {
        Piece t = ghost; t.r += 1;
        if (collides(g, &t)) break;
        ghost = t;
    }
    piece_cells(&ghost, cells, &n);
    for (i = 0; i < n; i++) {
        r = cells[i][0]; c = cells[i][1];
        if (r >= 0 && r < BOARD_H && c >= 0 && c < BOARD_W && buf[r][c] == 0)
            buf[r][c] = 10;
    }

    /* current piece on top */
    piece_cells(&g->cur, cells, &n);
    for (i = 0; i < n; i++) {
        r = cells[i][0]; c = cells[i][1];
        if (r >= 0 && r < BOARD_H && c >= 0 && c < BOARD_W) {
            if (g->cur.special == 1)      buf[r][c] = 8;
            else if (g->cur.special == 2) buf[r][c] = 9;
            else                          buf[r][c] = g->cur.type + 1;
        }
    }

    clr_home();
    printf("  TETRIS+   pont:%ld  csucs:%ld  szint:%d  sorok:%d" ESC("\033[K") "\n",
           g->score, (g->highscore > g->score ? g->highscore : g->score),
           g->level, g->lines);
    printf("  +"); for (c = 0; c < BOARD_W; c++) fputs("--", stdout);
    printf("+" ESC("\033[K") "\n");

    for (r = 0; r < BOARD_H; r++) {
        fputs("  |", stdout);
        for (c = 0; c < BOARD_W; c++) print_cell(buf[r][c]);
        fputs("|", stdout);

        /* side panel */
        if (r == 0) printf("   kovetkezo: %c", "IOTSZJL"[g->next_type]);
        else if (r == 2) printf("   toltes: %d/%d", g->charge, CHARGE_PER_POWERUP);
        else if (r == 4) printf("   keszlet:");
        else if (r == 5) printf("     1:%s", g->inv_count > 0 ? PU_NAME(g->inv[0]) : "-");
        else if (r == 6) printf("     2:%s", g->inv_count > 1 ? PU_NAME(g->inv[1]) : "-");
        else if (r == 7) printf("     3:%s", g->inv_count > 2 ? PU_NAME(g->inv[2]) : "-");
        else if (r == 9 && g->slowmo_ms_left > 0) printf("   [LASSITAS]");
        else if (r == 12) printf("   mozgas: <- -> / A D");
        else if (r == 13) printf("   forgat: ^ / W");
        else if (r == 14) printf("   ejt:    SPACE");
        else if (r == 15) printf("   power:  1 2 3");
        else if (r == 16) printf("   szunet: P   kilep: Q");
        printf(ESC("\033[K") "\n");
    }
    printf("  +"); for (c = 0; c < BOARD_W; c++) fputs("--", stdout);
    printf("+" ESC("\033[K") "\n");

    if (g->paused)    printf("  ** SZUNET (P a folytatashoz) **" ESC("\033[K") "\n");
    if (g->game_over) printf("  ** VEGE! pont: %ld - Q a kilepeshez **" ESC("\033[K") "\n",
                             g->score);
    fflush(stdout);
}
```

- [ ] **Step 2: Verify the test build still compiles**

Run: `gcc -std=c11 -DUNIT_TEST -Wall -Wextra -pedantic -o tetris_test tetris.c && ./tetris_test`
Expected: PASS — `0 failures`.

- [ ] **Step 3: Commit**

```bash
git add tetris.c
git commit -m "feat: ANSI renderer with USE_CLS fallback"
```

---

### Task 14: Game main loop

**Files:**
- Modify: `tetris.c` (the existing `#else` game `main` — replace the empty body)

**Interfaces:**
- Consumes: everything above.
- Produces: the real-time loop — read input, apply gravity on its interval, decrement slow-mo, render each frame, persist the high score once at game over, exit on `KEY_QUIT`.

- [ ] **Step 1: Replace the placeholder game `main`**

Inside the `#ifndef UNIT_TEST` block, replace the placeholder `int main(void) { return 0; }` (from Task 0, now sitting below `read_key` and the renderer) with the real loop. **Change only the function body — do not touch any `#ifndef`/`#ifdef`/`#else`/`#endif` line.**

```c
int main(void) {
    Game g;
    int running = 1, saved = 0;
    long elapsed = 0;

    game_init(&g, (uint32_t)time(NULL));
    g.highscore = load_highscore(HIGHSCORE_PATH);
    render_init();

    while (running) {
        int key = read_key();
        if (key == KEY_QUIT) {
            running = 0;
        } else if (key != KEY_NONE) {
            process_input(&g, key);
        }

        if (!g.paused && !g.game_over) {
            if (g.slowmo_ms_left > 0) {
                g.slowmo_ms_left -= FRAME_MS;
                if (g.slowmo_ms_left < 0) g.slowmo_ms_left = 0;
            }
            elapsed += FRAME_MS;
            if (elapsed >= gravity_interval_ms(&g)) {
                elapsed = 0;
                if (!try_move(&g, 1, 0)) lock_piece(&g);
            }
        }

        if (g.game_over && !saved) {
            if (g.score > g.highscore) {
                g.highscore = g.score;
                save_highscore(HIGHSCORE_PATH, g.highscore);
            }
            saved = 1;
        }

        render(&g);
        sleep_ms(FRAME_MS);
    }

    render_cleanup();
    return 0;
}
```

> Structure check: the file still has exactly two conditional blocks — `#ifndef UNIT_TEST { ...interactive..., int main } #endif` then `#ifdef UNIT_TEST { ...tests, int main } #endif` — each balanced, exactly one `main` compiled per configuration.

- [ ] **Step 2: Build the game and the tests**

Run: `gcc -std=c11 -O2 -Wall -Wextra -pedantic -o tetris tetris.c`
Expected: compiles clean, no warnings.

Run: `gcc -std=c11 -DUNIT_TEST -Wall -Wextra -pedantic -o tetris_test tetris.c && ./tetris_test`
Expected: PASS — `0 failures`.

- [ ] **Step 3: Manual smoke test**

Run `./tetris` in Windows Terminal. Verify:
- Board renders without flicker; piece falls; `←/→/↑/↓`, WASD, and Space work.
- Line clears increase score; charge meter fills; a power-up appears in the inventory and `1/2/3` fire it.
- A falling bomb `()` blasts a 3×3; a falling laser `<>` clears a row + column.
- `P` pauses; `Q` quits; game over shows; high score persists across runs.
- CPU stays low (Sleep-paced).

If colors/escapes show as garbage, rebuild with `-DUSE_CLS` and retry.

- [ ] **Step 4: Commit**

```bash
git add tetris.c
git commit -m "feat: real-time game main loop"
```

---

### Task 15: README + final verification

**Files:**
- Create: `README.md`

- [ ] **Step 1: Write the README**

```markdown
# TETRIS+ (C, power-up twist)

Egyfájlos Tetris C-ben, power-up csavarral: eső speciális elemek (bomba, lézer)
és sortörlésért töltődő készlet (lassítás, sortörlő, csere).

## Fordítás

MinGW-w64 / gcc szükséges. Ha nincs:
- `winget install BrechtSanders.WinLibs.POSIX.UCRT`  — vagy
- töltsd le a **w64devkit**-et, és tedd a `bin` mappáját a PATH-ra.

Játék:
```
gcc -std=c11 -O2 -Wall -Wextra -pedantic -o tetris tetris.c
```

Nem VT-képes (régi) konzolhoz, színek nélkül:
```
gcc -std=c11 -O2 -DUSE_CLS -o tetris tetris.c
```

Tesztek:
```
gcc -std=c11 -DUNIT_TEST -Wall -Wextra -pedantic -o tetris_test tetris.c
./tetris_test
```

## Vezérlés

| Billentyű | Hatás |
|---|---|
| ← → / A D | mozgás |
| ↑ / W | forgatás |
| ↓ / S | puha esés |
| Space | kemény dobás |
| 1 2 3 | power-up elsütése |
| P | szünet |
| Q | kilépés |

Futtasd Windows Terminalban a helyes ANSI-megjelenítéshez.
```

- [ ] **Step 2: Final full verification**

Run: `gcc -std=c11 -DUNIT_TEST -Wall -Wextra -pedantic -o tetris_test tetris.c && ./tetris_test`
Expected: PASS — `0 failures`.

Run: `gcc -std=c11 -O2 -Wall -Wextra -pedantic -o tetris tetris.c`
Expected: clean.

Run: `gcc -std=c11 -O2 -DUSE_CLS -Wall -Wextra -pedantic -o tetris_cls tetris.c`
Expected: clean (fallback build also compiles).

- [ ] **Step 3: Commit**

```bash
git add README.md
git commit -m "docs: README with build, run, controls"
```

---

## Self-Review (filled in by plan author)

**1. Spec coverage**

| Spec item | Task |
|---|---|
| Single file, std lib + conio.h + windows.h Sleep, no GNU ext | Global Constraints, Task 0, build flags everywhere |
| 10×20 board | Task 0 (`BOARD_W/H`) |
| 7 tetrominoes, 7-bag | Task 2, 3 |
| Rotation w/ wall kick, O no-op | Task 5 |
| Ghost + next preview | Task 13 |
| Lock + line clear + scoring + levels | Task 6 |
| Lock flash ("rövid villanás" before clear) | **Deferred** — see Known notes |
| Falling bomb (3×3) + laser (row+col) | Task 8 (+ spawn roll tested in Task 7 `test_special_spawn`) |
| Charge meter + inventory (slowmo, nuke, swap), fire 1/2/3 | Task 9, 11 |
| Controls (arrows+WASD, space, P, Q, 1/2/3) | Task 11, 12 |
| ANSI render + USE_CLS fallback | Task 13 |
| Sleep-paced loop, slow-mo timing | Task 14 |
| High-score file | Task 10, 14 |
| Build/run + README | Task 15 |
| Manual verification checklist | Task 14 Step 3, Task 15 |

Every spec item maps to a task **except** the lock-flash animation, which is explicitly deferred (see Known notes).

**2. Placeholder scan:** No TBD/TODO/"handle edge cases". The only stub is the deliberately-temporary `add_charge` in Task 8, explicitly replaced in Task 9 Step 1.

**3. Type consistency:** Names/signatures verified consistent across tasks: `clear_lines`, `apply_bomb`/`apply_laser`, `apply_nuke`/`apply_swap`, `use_powerup`, `add_charge`, `gravity_interval_ms`, `process_input`, `read_key`, `render`. Cell ids 1..7 and special codes 8 (bomb)/9 (laser)/10 (ghost) are used consistently in `render`. Power-up ids `PU_SLOWMO/NUKE/SWAP` consistent.

**Known quality notes (acceptable per spec "egyszerű forgatás"):**
- Naive 4×4 rotation makes non-I/O pieces drift slightly within their box; wall kicks keep play correct. O is a no-op to avoid the worst jiggle.
- `gravity_interval_ms` floors at `MIN_INTERVAL_MS` before applying slow-mo, so slow-mo at very high levels still slows relative to the floor.
- **Lock flash deferred:** the spec mentions a brief flash before rows clear (§4). It is presentation-only, real-time, and not exercisable by the unit harness, so it is out of scope for this plan — `clear_lines` removes rows immediately. It can be added later as a one-frame row highlight in the main loop. (Confirm with the user whether they want it.)

**4. Review:** This plan was adversarially reviewed by a 5-lens agent pass + adjudicator. One blocker (an `#ifdef`/`#endif` imbalance in the original Task 14 edit) was fixed by restructuring Tasks 0/12/14 into two balanced conditional blocks; two brittle test assertions and a missing spawn-special test were also fixed.
