// Game rules, the hidden path, drawing and saving.
//
// A level: the floor is 8 rows deep and 5 (easy) or 8 (medium, hard) tiles wide. A safe path
// runs from the start pad (bottom) to the finish pad (top), moving up, left or right one tile
// at a time. It lights up tile by tile with a rising tune (PH_SHOW), then hides (PH_PLAY).
// Step on a tile that isn't on the path and it gives way (PH_FALL): you lose a heart, that
// tile stays a hole, and you start again from the start pad. Reach the far side to clear the
// level (PH_CLEAR): 3 stars with no falls, 2 with one, else 1. Clearing a level unlocks the
// next one on the home screen. As the levels go up, paths get longer, are shown for less time,
// you get fewer hearts, and later on paths may step backwards (down) too - all at a pace set
// by the difficulty (DIFFS). Levels are just these rules: nothing is stored per level.
#include "stones.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
// In a browser the files vanish when the page closes, so save.txt is also kept in the
// browser's localStorage: copied there after every save, and back before loading.
EM_JS(void, web_persist, (const char *path), {
    try { localStorage.setItem("stepping-stones-save", FS.readFile(UTF8ToString(path), {encoding: "utf8"})); } catch (e) {}
});
EM_JS(void, web_restore, (const char *path), {
    try { var s = localStorage.getItem("stepping-stones-save"); if (s) FS.writeFile(UTF8ToString(path), s); } catch (e) {}
});
#endif

Game G;
int g_screen = SCR_HOME;

#define HOP_TICKS 7             // one step takes about 0.23 s
#define FALL_TICKS 40
#define LAND_TICKS 6            // squash after landing

// screen layout: the tile grid sits in the middle, pads above and below, panels at the sides
#define TILE 40
#define PITCH 45                // tile + 5 px gap
#define GY0 58
#define LX 66                   // centre of the left panel
#define RX (SCREEN_W - 66)      // centre of the right panel

typedef struct {
    const char *name;
    int levels, cols;
    int len0, len_num, len_den, len_cap, slack;   // path tiles: len0 + (level-1)*num/den, up to cap; +slack allowed
    int show0, show_min, show_drop;               // ticks to memorize at level 1, the least, the drop per level (x10)
    int lives, lives_min, lives_every;            // hearts at level 1, the fewest, levels per heart lost
    int down_from;                                // from this level the path may also step down (backwards)
    bool reshow;                                  // show the path again (quickly) after a fall
} Diff;
// Easy is for little ones: a 5-wide floor, level 1 nearly straight up (8 tiles) shown for
// 7 seconds with 5 hearts. Over its 50 levels the path grows a tile every 3 levels, the time
// shrinks a little, hearts drop to 4 at level 21 and 3 at level 36, and from level 26 the path
// can wiggle backwards. Medium and hard (50 levels each too) use the full 8-wide floor: medium
// grows a tile every 2 levels from 11 (cap 28), wiggles backwards from level 11 and drops to
// 2 hearts at level 36; hard starts at 14 tiles (cap 32), wiggles from level 6 and drops to
// 2 hearts at level 21.
static const Diff DIFFS[DIFF_COUNT] = {
    {"easy",   50, 5, 8,  1, 3, 20, 1, 210, 105, 35, 5, 3, 15, 26, true},
    {"medium", 50, 8, 11, 1, 2, 28, 2, 165, 75,  30, 3, 2, 30, 11, false},
    {"hard",   50, 8, 14, 1, 2, 32, 3, 135, 54,  30, 3, 2, 15, 6,  false},
};
#define D (&DIFFS[G.difficulty])

const char *difficulty_name(int d) { return DIFFS[d].name; }
int level_count(int d) { return DIFFS[d].levels; }

static const SDL_Color BG = {18, 24, 44, 255}, PIT = {8, 10, 22, 255},
                       TILE_C = {204, 213, 228, 255}, TILE_TOP = {236, 241, 249, 255},
                       TILE_EDGE = {118, 128, 152, 255},
                       LIT = {60, 255, 120, 255}, LIT_TOP = {200, 255, 215, 255}, LIT_EDGE = {20, 150, 70, 255},
                       FOUND = {140, 240, 170, 255}, FOUND_TOP = {215, 255, 225, 255}, FOUND_EDGE = {60, 150, 90, 255},
                       RED = {240, 70, 70, 255}, GOLD = {255, 196, 50, 255}, GOLD_DARK = {178, 126, 22, 255},
                       WHITE = {255, 255, 255, 255}, INK = {232, 236, 246, 255}, DIM = {176, 186, 212, 255},
                       PINK = {255, 110, 150, 255};

static char save_path[1100];
static int land_t;              // squash countdown after a landing
static int reveal_shown;        // SHOW: how many path tiles have lit so far

bool level_unlocked(int d, int level) { return level == 1 || (level <= DIFFS[d].levels && G.stars[d][level - 2] > 0); }

// ---- the hidden path ----------------------------------------------------------------------------
// A random depth-first search: from a tile, try up/left/right in random order. A new tile may
// not touch any earlier path tile except the one we came from, so the path never runs beside
// itself and there's only one way to walk it. Paths shorter or longer than wanted are rejected.
static bool used[ROWS][MAX_COLS];
static int want_min, want_max;
static bool allow_down;
static long budget;

static bool touches_path(int c, int r, int from_c, int from_r) {
    static const int dc[4] = {0, 0, -1, 1}, dr[4] = {-1, 1, 0, 0};
    for (int i = 0; i < 4; i++) {
        int nc = c + dc[i], nr = r + dr[i];
        if (nc < 0 || nc >= G.cols || nr < 0 || nr >= ROWS) continue;
        if (nc == from_c && nr == from_r) continue;
        if (used[nr][nc]) return true;
    }
    return false;
}

static bool dfs(int c, int r) {
    if (--budget < 0) return false;
    int len = G.path_len;
    if (r == 0) return len >= want_min;                  // reached the far row
    if (len + r > want_max) return false;                // can't get there in time
    int dirs[4][2] = {{0, -1}, {-1, 0}, {1, 0}, {0, 1}};  // up, left, right, (down)
    int nd = allow_down ? 4 : 3;
    for (int i = nd - 1; i > 0; i--) {                   // shuffle
        int j = rand() % (i + 1), t0 = dirs[i][0], t1 = dirs[i][1];
        dirs[i][0] = dirs[j][0]; dirs[i][1] = dirs[j][1];
        dirs[j][0] = t0; dirs[j][1] = t1;
    }
    for (int i = 0; i < nd; i++) {
        int nc = c + dirs[i][0], nr = r + dirs[i][1];
        if (nc < 0 || nc >= G.cols || nr < 0 || nr >= ROWS || used[nr][nc] || touches_path(nc, nr, c, r)) continue;
        used[nr][nc] = true;
        G.path_c[G.path_len] = nc;
        G.path_r[G.path_len] = nr;
        G.path_len++;
        if (dfs(nc, nr)) return true;
        G.path_len--;
        used[nr][nc] = false;
    }
    return false;
}

static void make_path(int level) {
    want_min = D->len0 + (level - 1) * D->len_num / D->len_den;
    if (want_min > D->len_cap) want_min = D->len_cap;
    want_max = want_min + D->slack + (level > 20 ? 1 : 0);
    allow_down = level >= D->down_from;
    for (int attempt = 0;; attempt++) {
        memset(used, 0, sizeof used);
        int c = rand() % G.cols;
        G.path_len = 1;
        G.path_c[0] = c;
        G.path_r[0] = ROWS - 1;
        used[ROWS - 1][c] = true;
        budget = 200000;
        if (dfs(c, ROWS - 1)) break;
        if (attempt % 10 == 9 && want_min > ROWS) want_min--;   // too fussy: settle for shorter
    }
    memset(G.safe, 0, sizeof G.safe);
    for (int i = 0; i < G.path_len; i++) G.safe[G.path_r[i]][G.path_c[i]] = true;
}

// ---- levels -------------------------------------------------------------------------------------
static void set_phase(int p) { G.phase = p; G.t = 0; }

static void start_show(int reveal_step, int hold) {
    G.reveal_step = reveal_step;
    G.show_total = G.path_len * reveal_step + 10 + hold;
    reveal_shown = 0;
    set_phase(PH_SHOW);
}

void game_play(int level) {
    G.level = level;
    G.cols = D->cols;
    make_path(level);
    memset(G.hole, 0, sizeof G.hole);
    memset(G.stepped, 0, sizeof G.stepped);
    G.nstepped = 0;
    G.pc = G.cols / 2;
    G.pr = ROWS;
    G.hop = 0;
    G.lives = D->lives - (level - 6) / D->lives_every;    // levels 1-5 keep them all; then one fewer every lives_every levels
    if (G.lives < D->lives_min) G.lives = D->lives_min;
    G.max_lives = G.lives;
    G.falls = 0;
    fx_clear();
    // time to look once it's all lit: a bit more for longer paths, a bit less each level
    int t = D->show0 + (G.path_len - D->len0) * 5 - (level - 1) * D->show_drop / 10;
    start_show(G.difficulty == DIFF_EASY ? 4 : 3, t < D->show_min ? D->show_min : t > 240 ? 240 : t);
    g_screen = SCR_GAME;
}

static void back_to_start(void) {
    memset(G.stepped, 0, sizeof G.stepped);
    G.nstepped = 0;
    G.pr = ROWS;                                          // same column, on the start pad
    G.hop = 0;
    if (D->reshow) start_show(2, 30);                    // easy: a quick reminder
    else set_phase(PH_PLAY);
}

// ---- the celebration: the rainbow wave runs along the whole path first, then the score card
static int card_t0(void) { return G.path_len * 2 + 25; }        // when the card appears
static int stars_t0(void) { return card_t0() + 25; }            // stars, 15 ticks apart
static int badge_t0(void) { return stars_t0() + 50; }
static int ready_t(void) { return badge_t0(); }                  // A goes home from here

// ---- input --------------------------------------------------------------------------------------
static void move(int dc, int dr) {
    int nc = G.pc + dc, nr = G.pr + dr;
    if (nc < 0 || nc >= G.cols || nr < 0 || nr > ROWS) return;
    if (nr < ROWS && G.hole[nr][nc]) { sound(SND_LOCKED); return; }   // a known hole: don't step in
    G.from_c = G.pc;
    G.from_r = G.pr;
    G.pc = nc;
    G.pr = nr;
    G.hop = HOP_TICKS;
    sound(SND_STEP);
}

void game_input(int a) {
    switch (G.phase) {
    case PH_PLAY:
        if (G.hop > 0) return;                            // one step at a time
        if (a == IN_LEFT) move(-1, 0);
        else if (a == IN_RIGHT) move(1, 0);
        else if (a == IN_UP) move(0, -1);
        else if (a == IN_DOWN) move(0, 1);
        break;
    case PH_CLEAR:
        if (G.t >= ready_t() && (a == IN_A || a == IN_B)) home_show(G.new_badge ? G.level : 0);
        break;
    case PH_OVER:
        if (G.t < 30) return;
        if (a == IN_A) game_play(G.level);
        else if (a == IN_B) home_show(0);
        break;
    }
}

// ---- per tick -----------------------------------------------------------------------------------
static void land(void) {
    sound(SND_LAND);
    land_t = LAND_TICKS;
    if (G.pr == ROWS || G.pr < 0) return;                 // on a pad
    if (!G.safe[G.pr][G.pc]) {                            // wrong tile: it gives way
        sound(SND_CRACK);
        G.falls++;
        set_phase(PH_FALL);
        return;
    }
    if (!G.stepped[G.pr][G.pc]) { G.stepped[G.pr][G.pc] = true; note(G.nstepped++); }
    int last = G.path_len - 1;
    if (G.pc == G.path_c[last] && G.pr == G.path_r[last]) {   // the far side!
        G.from_c = G.pc;
        G.from_r = G.pr;
        G.pr = -1;                                        // hop onto the finish pad
        G.hop = HOP_TICKS + 3;
        G.stars_won = G.falls == 0 ? 3 : G.falls == 1 ? 2 : 1;
        unsigned char *s = &G.stars[G.difficulty][G.level - 1];
        G.new_badge = *s == 0;
        if (G.stars_won > *s) *s = (unsigned char)G.stars_won;
        save_write();
        sound(SND_WIN);
        set_phase(PH_CLEAR);
        fx_confetti(SCREEN_W / 2, 40, 90, 8);
    }
}

void game_tick(void) {
    G.ticks++;
    G.t++;
    if (land_t > 0) land_t--;
    if (G.hop > 0 && --G.hop == 0) {
        if (G.phase == PH_PLAY) land();
        else if (G.phase == PH_CLEAR) { sound(SND_LAND); land_t = LAND_TICKS; }   // onto the finish pad
    }
    fx_tick();
    switch (G.phase) {
    case PH_SHOW: {
        int lit = G.t / G.reveal_step + 1;               // light the path one tile at a time
        if (lit > G.path_len) lit = G.path_len;
        while (reveal_shown < lit) note(reveal_shown++);
        int left = G.show_total - G.t, reveal_end = G.path_len * G.reveal_step + 10;
        if (G.t > reveal_end && left > 0 && left % TICK_HZ == 0) sound(SND_COUNTDOWN);
        if (left <= 0) { sound(SND_GO); set_phase(PH_PLAY); }
        break;
    }
    case PH_FALL:
        if (G.t == 8) sound(SND_FALL);
        if (G.t >= FALL_TICKS) {
            G.hole[G.pr][G.pc] = true;
            if (--G.lives <= 0) { sound(SND_OVER); set_phase(PH_OVER); }
            else back_to_start();
        }
        break;
    case PH_CLEAR:
        if (G.t % 18 == 0 && G.t < badge_t0() + 20) fx_confetti(80 + rand() % (SCREEN_W - 160), 20, 25, 5);
        for (int i = 0; i < 3; i++)
            if (G.t == stars_t0() + i * 15 && i < G.stars_won) {
                sound(SND_STAR1 + i);
                fx_sparkle(SCREEN_W / 2 + (i - 1) * 56, 225, 12);
            }
        if (G.t == badge_t0() && G.new_badge) { sound(SND_BADGE); fx_sparkle(SCREEN_W / 2, 300, 20); }
        break;
    }
}

// ---- drawing ------------------------------------------------------------------------------------
static float shake_x, shake_y;
static int grid_w(void) { return G.cols * PITCH - (PITCH - TILE); }
static float gx0(void) { return (SCREEN_W - grid_w()) / 2.0f; }
static float tile_x(int c) { return gx0() + c * PITCH + shake_x; }
static float tile_y(int r) {
    return (r >= ROWS ? GY0 + ROWS * PITCH + 4 : r < 0 ? GY0 - PITCH + 2 : GY0 + r * PITCH) + shake_y;
}

static SDL_Color scale_c(SDL_Color c, float k) {
    float r = c.r * k, g = c.g * k, b = c.b * k;
    return (SDL_Color){(Uint8)(r > 255 ? 255 : r), (Uint8)(g > 255 ? 255 : g), (Uint8)(b > 255 ? 255 : b), c.a};
}

static SDL_Color hue(float h) {                           // rainbow colour, h in 0..360
    h = fmodf(h, 360) / 60;
    float x = 1 - fabsf(fmodf(h, 2) - 1);
    float r = 0, g = 0, b = 0;
    if (h < 1) { r = 1; g = x; } else if (h < 2) { r = x; g = 1; } else if (h < 3) { g = 1; b = x; }
    else if (h < 4) { g = x; b = 1; } else if (h < 5) { r = x; b = 1; } else { r = 1; b = x; }
    return (SDL_Color){(Uint8)(80 + r * 175), (Uint8)(80 + g * 175), (Uint8)(80 + b * 175), 255};
}

// a raised tile: darker edge below, lighter strip on top; size != TILE scales it about its centre
static void draw_tile(float x, float y, float size, SDL_Color face, SDL_Color top, SDL_Color edge) {
    float inset = (TILE - size) / 2;
    x += inset;
    y += inset;
    fill_rect(x, y + 4, size, size, edge);
    fill_rect(x, y, size, size, face);
    fill_rect(x, y, size, size * 0.18f, top);
}

static void draw_pads(void) {
    float x = gx0() - 6 + shake_x, w = grid_w() + 12, yf = tile_y(-1), ys = tile_y(ROWS);
    // finish: a chequered flag strip
    fill_rect(x, yf + 4, w, 30, (SDL_Color){60, 60, 70, 255});
    for (int i = 0; i * 15 < w; i++)
        for (int j = 0; j < 2; j++) {
            float cw = fminf(15, w - i * 15);
            fill_rect(x + i * 15, yf + j * 15, cw, 15, (i + j) % 2 ? (SDL_Color){30, 30, 40, 255} : WHITE);
        }
    // start: gold
    fill_rect(x, ys + 4, w, 30, GOLD_DARK);
    fill_rect(x, ys, w, 30, GOLD);
    text(FONT_S, "start", x + w / 2, ys + 15, 'c', GOLD_DARK);
}

static void draw_player(void) {
    // where the player is drawn: sliding from the previous tile during a hop
    float x1 = tile_x(G.pc) + TILE / 2.0f, y1 = tile_y(G.pr) + TILE / 2.0f - 4;
    float x = x1, y = y1, lift = 0, walk = 0;
    if (G.hop > 0) {
        int total = G.pr < 0 && G.phase == PH_CLEAR ? HOP_TICKS + 3 : HOP_TICKS;
        float t = 1.0f - (float)G.hop / total;
        float x0 = tile_x(G.from_c) + TILE / 2.0f, y0 = tile_y(G.from_r) + TILE / 2.0f - 4;
        x = x0 + (x1 - x0) * t;
        y = y0 + (y1 - y0) * t;
        lift = sinf(t * (float)M_PI) * 12;                // a little jump
        walk = t * 2 * (float)M_PI;                       // one full stride per step
    } else if (G.phase == PH_CLEAR) {                     // happy bouncing on the finish
        lift = fabsf(sinf(G.t * 0.2f)) * 14;
        walk = G.t * 0.4f;
    } else lift = sinf(G.ticks * 0.12f) * 1.2f;           // breathing while standing
    float s = 0.85f, squash = land_t > 0 ? 0.7f * land_t / LAND_TICKS : 0;
    if (G.phase == PH_FALL) {                             // shrink away into the pit, legs kicking
        float t = (float)G.t / FALL_TICKS;
        float k = t < 0.2f ? 1 : 1 - (t - 0.2f) / 0.8f;
        if (k <= 0.02f) return;
        s *= k;
        y += (1 - k) * 10;
        walk = G.t * 0.9f;
        squash = 0;
    }
    draw_buddy(x, y - lift, s, G.phase == PH_CLEAR, walk, squash);
}

static void draw_hearts(float cx, float y) {
    int n = G.max_lives;
    for (int i = 0; i < n; i++) {
        float hx = cx + (i - (n - 1) / 2.0f) * 24;
        if (i < G.lives) {
            fill_circle(hx - 4, y - 2, 6, RED);
            fill_circle(hx + 4, y - 2, 6, RED);
            fill_rect(hx - 6, y - 2, 12, 6, RED);
            fill_circle(hx, y + 5, 4.5f, RED);
        } else ring(hx, y, 8, 2, DIM);
    }
}

static float pop(int t, int start) {                      // 0 -> overshoot -> 1, for things popping in
    float k = (t - start) / 8.0f;
    if (k <= 0) return 0;
    if (k < 1) return 1.3f * k;
    if (k < 1.5f) return 1.3f - (k - 1) * 0.6f;
    return 1;
}

void game_draw(void) {
    shake_x = shake_y = 0;
    if (G.phase == PH_FALL && G.t < 12) {                 // the floor jolts when a tile breaks
        float k = 1 - G.t / 12.0f;
        shake_x = sinf(G.t * 2.7f) * 4 * k;
        shake_y = cosf(G.t * 3.1f) * 3 * k;
    }
    fill_rect(0, 0, SCREEN_W, SCREEN_H, BG);
    fill_rect(gx0() - 10 + shake_x, GY0 - 6 + shake_y, grid_w() + 20, ROWS * PITCH + 6, PIT);   // the pit
    draw_pads();

    // which path tiles are lit, and how bright (a gentle pulse)
    int lit = 0;
    if (G.phase == PH_SHOW) { lit = G.t / G.reveal_step + 1; if (lit > G.path_len) lit = G.path_len; }
    if (G.phase == PH_OVER) lit = G.path_len;
    float pulse = 0.88f + 0.12f * sinf(G.ticks * 0.3f);
    int order[ROWS][MAX_COLS];                            // position of each tile along the path
    memset(order, -1, sizeof order);
    for (int i = 0; i < G.path_len; i++) order[G.path_r[i]][G.path_c[i]] = i;

    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < G.cols; c++) {
            float x = tile_x(c), y = tile_y(r);
            int i = order[r][c];
            bool here = c == G.pc && r == G.pr;
            if (G.hole[r][c]) {                           // an open hole
                fill_rect(x + 3, y + 3, TILE - 6, TILE - 6, (SDL_Color){30, 34, 56, 255});
                continue;
            }
            if (G.phase == PH_FALL && here) {             // the cracking tile drops away
                float t = (float)G.t / FALL_TICKS, size = TILE * (1 - t);
                if (size > 2) draw_tile(x, y + t * 16, size, RED, (SDL_Color){255, 150, 150, 255}, (SDL_Color){120, 30, 30, 255});
                continue;
            }
            if (i >= 0 && i < lit) {                      // lit: bright, glowing, pops in when it lights
                float age = G.phase == PH_SHOW ? G.t - i * G.reveal_step : 99;
                float size = age < 6 ? TILE + 8 - age * 8 / 6 : TILE;
                fill_rect(x - 4, y - 4, TILE + 8, TILE + 8, (SDL_Color){90, 255, 150, 70});
                draw_tile(x, y, size, scale_c(LIT, pulse), LIT_TOP, LIT_EDGE);
            } else if (G.phase == PH_CLEAR && i >= 0 && G.t >= i * 2) {    // rainbow wave along the path
                SDL_Color h = hue(i * 28 + G.t * 6);
                draw_tile(x, y, TILE, h, scale_c(h, 1.25f), scale_c(h, 0.55f));
            } else if (G.stepped[r][c]) draw_tile(x, y, TILE, FOUND, FOUND_TOP, FOUND_EDGE);
            else draw_tile(x, y, TILE, TILE_C, TILE_TOP, TILE_EDGE);
        }
    if (G.phase != PH_OVER) draw_player();                // (out of hearts: it fell in)

    // left panel: level, hearts, difficulty, best stars
    char buf[64];
    text(FONT_S, "level", LX, 80, 'c', DIM);
    snprintf(buf, sizeof buf, "%d", G.level);
    text(FONT_XL, buf, LX, 135, 'c', INK);
    draw_hearts(LX, 205);
    text(FONT_S, D->name, LX, 250, 'c', DIM);
    int best = G.stars[G.difficulty][G.level - 1];
    if (best) draw_stars(LX, 295, 10, best);

    // right panel: what's happening now
    switch (G.phase) {
    case PH_SHOW: {
        int reveal_end = G.path_len * G.reveal_step + 10;
        text(FONT_L, "watch!", RX, 160, 'c', LIT_TOP);
        if (G.t > reveal_end) {
            snprintf(buf, sizeof buf, "%d", (G.show_total - G.t + TICK_HZ - 1) / TICK_HZ);
            text_ex(FONT_XL, buf, RX, 245, 'c', 1 + 0.15f * ((G.show_total - G.t) % TICK_HZ) / TICK_HZ, INK);
        }
        break;
    }
    case PH_PLAY:
        if (G.t < TICK_HZ) text_ex(FONT_XL, "go!", RX, 200, 'c', pop(G.t, 0), GOLD);
        else {                                            // which buttons walk: the d-pad
            draw_button(RX, 185, BTN_DPAD, 1.5f);
            text(FONT_L, "walk!", RX, 250, 'c', INK);
        }
        break;
    case PH_FALL: text_ex(FONT_L, "oops!", RX, 200, 'c', pop(G.t, 0), RED); break;
    default: break;
    }
    draw_button(RX, 410, BTN_START, 1);
    text(FONT_S, "menu", RX, 448, 'c', DIM);

    if (G.phase == PH_CLEAR && G.t >= card_t0()) {        // the celebration card, once the wave is done
        float k0 = fminf(1, (G.t - card_t0()) / 6.0f);       // fades in
        fill_rect(130, 120, 380, 260, (SDL_Color){10, 14, 30, (Uint8)(215 * k0)});
        text_ex(FONT_L, "level clear!", SCREEN_W / 2, 160, 'c', pop(G.t, card_t0() + 4), GOLD);
        for (int i = 0; i < 3; i++) {
            float k = pop(G.t, stars_t0() + i * 15);
            if (k <= 0) continue;
            float sx = SCREEN_W / 2 + (i - 1) * 56, sy = 225 - (i == 1 ? 8 : 0);
            fill_star(sx, sy, 24 * k, (SDL_Color){120, 80, 10, 255});
            fill_star(sx, sy, 21 * k, i < G.stars_won ? (SDL_Color){255, 210, 50, 255} : (SDL_Color){58, 66, 100, 255});
        }
        if (G.new_badge) {
            float k = pop(G.t, badge_t0());
            if (k > 0) {
                draw_badge(SCREEN_W / 2 - 70, 293, 30 * k, G.level, true);
                if (G.t > badge_t0() + 10) text(FONT_M, "new badge!", SCREEN_W / 2 - 25, 293, 'l', PINK);
            }
        }
        if (G.t >= ready_t()) prompt(SCREEN_W / 2, 350, BTN_A, "next", FONT_M, 'c', INK, true);
    }
    if (G.phase == PH_OVER) {
        fill_rect(120, 130, 400, 230, (SDL_Color){10, 14, 30, 225});
        text(FONT_L, "so close!", SCREEN_W / 2, 170, 'c', PINK);
        draw_buddy(SCREEN_W / 2, 235, 1.4f, false, 0, 0);
        if (G.t >= 30) {
            prompt(SCREEN_W / 2 - 15, 320, BTN_A, "again", FONT_M, 'r', INK, true);
            prompt(SCREEN_W / 2 + 25, 320, BTN_B, "levels", FONT_M, 'l', INK, false);
        }
    }
    fx_draw();
}

// ---- saving -------------------------------------------------------------------------------------
void save_init(const char *dir) {                         // "" = no saving (tests)
    if (dir[0]) snprintf(save_path, sizeof save_path, "%ssave.txt", dir); else save_path[0] = 0;
}

void save_write(void) {
    if (!save_path[0]) return;
    FILE *f = fopen(save_path, "w");
    if (!f) return;
    fprintf(f, "difficulty %d\nsound %d\nmusic %d\n", G.difficulty, g_sound_on ? 1 : 0, g_music_on ? 1 : 0);
    for (int d = 0; d < DIFF_COUNT; d++) {
        fprintf(f, "stars_%s", DIFFS[d].name);
        for (int i = 0; i < DIFFS[d].levels; i++) fprintf(f, " %d", G.stars[d][i]);
        fprintf(f, "\n");
    }
    fclose(f);
#ifdef __EMSCRIPTEN__
    web_persist(save_path);
#endif
}

void save_load(void) {
    if (!save_path[0]) return;
#ifdef __EMSCRIPTEN__
    web_restore(save_path);
#endif
    FILE *f = fopen(save_path, "r");
    if (!f) return;
    char line[512], key[32];
    while (fgets(line, sizeof line, f)) {
        int n = 0, v;
        if (sscanf(line, "%31s%n", key, &n) != 1) continue;
        char *rest = line + n;
        if (!strcmp(key, "sound") && sscanf(rest, "%d", &v) == 1) g_sound_on = v != 0;
        else if (!strcmp(key, "music") && sscanf(rest, "%d", &v) == 1) g_music_on = v != 0;
        else if (!strcmp(key, "difficulty") && sscanf(rest, "%d", &v) == 1 && v >= 0 && v < DIFF_COUNT) G.difficulty = v;
        for (int d = 0; d < DIFF_COUNT; d++) {
            char k[32];
            snprintf(k, sizeof k, "stars_%s", DIFFS[d].name);
            if (!strcmp(key, k))
                for (int i = 0; i < DIFFS[d].levels && sscanf(rest, "%d%n", &v, &n) == 1; i++, rest += n)
                    G.stars[d][i] = (unsigned char)(v < 0 ? 0 : v > 3 ? 3 : v);
            snprintf(k, sizeof k, "best_%s", DIFFS[d].name);      // older saves: levels reached
            if (!strcmp(key, k) && sscanf(rest, "%d", &v) == 1)
                for (int i = 0; i < v && i < DIFFS[d].levels; i++) if (!G.stars[d][i]) G.stars[d][i] = 1;
        }
    }
    fclose(f);
}
