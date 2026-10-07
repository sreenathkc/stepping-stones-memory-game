// The home screen: the badge board, which is also the level select. Level 1 is open from the
// start; clearing a level wins its badge (bronze, silver, gold, then rainbow, with the stars you
// earned underneath) and unlocks the next level with a little padlock-pop show. There are 50
// levels, 10 rows, and 4 rows fit on screen: the board scrolls smoothly so there's always a row
// showing past the cursor (so you can see there's more). Move with the d-pad, A plays, L1/R1
// switch difficulty, Start opens the menu.
#include "stones.h"
#include <math.h>
#include <stdio.h>

static int sel;                         // the badge the cursor is on (0-based level)
static int unlock_level, unlock_t;      // a level being unlocked right now (1-based), and its timer
static int wiggle_t;                    // a locked badge shakes when you press A on it
static long ticks;

static const SDL_Color INK = {232, 236, 246, 255}, DIM = {176, 186, 212, 255},
                       GOLD = {255, 196, 50, 255}, BG = {18, 24, 44, 255}, TEAL = {40, 200, 220, 255};

#define VIEW_ROWS 4                     // rows of badges that fit on screen
#define ROW_H 70
#define TOP_Y 145                       // centre of the first visible row
static int top;                         // first row in view (where we're scrolling to)
static float scroll;                    // the row actually at the top, gliding towards `top`

static int rows(void) { return (level_count(G.difficulty) + 4) / 5; }
static float badge_x(int i) { return SCREEN_W / 2 + (i % 5 - 2) * 100; }
static float badge_y(int i) { return TOP_Y + (i / 5 - scroll) * ROW_H; }

// keep a row visible past the cursor: on the last row in view, scroll down one (if there's more)
static void follow_cursor(bool snap) {
    int r = sel / 5, max_top = rows() - VIEW_ROWS < 0 ? 0 : rows() - VIEW_ROWS;
    if (snap) top = r - 2;
    else if (r > top + VIEW_ROWS - 2) top = r - (VIEW_ROWS - 2);
    else if (r < top + 1) top = r - 1;
    if (top > max_top) top = max_top;
    if (top < 0) top = 0;
    if (snap) scroll = top;
}

// tiers of five: bronze, silver, gold, then rainbow colours
static void tier_colors(int level, SDL_Color *face, SDL_Color *rim) {
    static const SDL_Color rainbow[5] = {{236, 84, 84, 255}, {245, 150, 50, 255}, {80, 190, 100, 255},
                                         {70, 150, 235, 255}, {170, 100, 230, 255}};
    int t = (level - 1) / 5;
    if (t == 0) { *face = (SDL_Color){214, 140, 80, 255}; *rim = (SDL_Color){140, 80, 40, 255}; }
    else if (t == 1) { *face = (SDL_Color){206, 214, 226, 255}; *rim = (SDL_Color){120, 130, 150, 255}; }
    else if (t == 2) { *face = (SDL_Color){244, 196, 60, 255}; *rim = (SDL_Color){170, 120, 20, 255}; }
    else {
        *face = rainbow[(level - 1) % 5];
        *rim = (SDL_Color){(Uint8)(face->r / 2), (Uint8)(face->g / 2), (Uint8)(face->b / 2), 255};
    }
}

static void draw_lock(float x, float y, float r, Uint8 alpha) {
    SDL_Color c = {150, 160, 190, alpha};
    ring(x, y - r * 0.16f, r * 0.27f, r * 0.09f, c);                   // shackle
    fill_rect(x - r * 0.36f, y - r * 0.1f, r * 0.72f, r * 0.55f, c);   // body
    fill_circle(x, y + r * 0.12f, r * 0.08f, (SDL_Color){30, 37, 62, alpha});
}

// a won badge (unlocked = true) or a locked one
void draw_badge(float x, float y, float r, int level, bool unlocked) {
    if (r < 1) return;
    if (!unlocked) {
        fill_circle(x, y, r, (SDL_Color){40, 48, 78, 255});
        fill_circle(x, y, r - 4, (SDL_Color){30, 37, 62, 255});
        draw_lock(x, y - r * 0.2f, r * 0.75f, 255);
        char n[8];
        snprintf(n, sizeof n, "%d", level);                     // which level this lock is
        text_ex(FONT_S, n, x, y + r * 0.5f, 'c', r / 30, DIM);
        return;
    }
    SDL_Color face, rim;
    tier_colors(level, &face, &rim);
    fill_circle(x, y, r, rim);
    fill_circle(x, y, r - 4, face);
    fill_circle(x - r * 0.3f, y - r * 0.35f, r * 0.18f, (SDL_Color){255, 255, 255, 120});   // shine
    char n[8];
    snprintf(n, sizeof n, "%d", level);
    text_ex(FONT_M, n, x, y - r * 0.12f, 'c', r / 30, (SDL_Color){50, 40, 40, 255});
}

// open but not won yet: a bright pulsing "play me" badge
static void draw_open(float x, float y, float r, int level) {
    float k = 1 + 0.06f * sinf(ticks * 0.2f);
    fill_circle(x, y, r * k, (SDL_Color){20, 120, 140, 255});
    fill_circle(x, y, (r - 4) * k, TEAL);
    fill_circle(x - r * 0.3f, y - r * 0.35f, r * 0.18f, (SDL_Color){255, 255, 255, 140});
    char n[8];
    snprintf(n, sizeof n, "%d", level);
    text_ex(FONT_M, n, x, y, 'c', k * r / 30, (SDL_Color){255, 255, 255, 255});
}

static int first_open(void) {
    int n = level_count(G.difficulty);
    for (int i = 0; i < n; i++) if (!G.stars[G.difficulty][i]) return i;
    return n - 1;
}

void home_show(int cleared) {
    g_screen = SCR_HOME;
    fx_clear();
    unlock_level = 0;
    if (cleared > 0 && cleared < level_count(G.difficulty)) {               // put the cursor on the new level and unlock it
        unlock_level = cleared + 1;
        unlock_t = 0;
        sel = cleared;
    } else if (G.level > 0) sel = G.level - 1;     // back from a level: stay on it
    else sel = first_open();
    follow_cursor(true);
}

void home_input(int a) {
    int col = sel % 5, n = level_count(G.difficulty);
    switch (a) {
    case IN_LEFT: if (col > 0) sel--; sound(SND_TICK); break;
    case IN_RIGHT: if (col < 4 && sel + 1 < n) sel++; sound(SND_TICK); break;
    case IN_UP: if (sel >= 5) sel -= 5; sound(SND_TICK); break;
    case IN_DOWN: if (sel + 5 < n) sel += 5; sound(SND_TICK); break;
    case IN_L1: case IN_R1:
        G.difficulty = (G.difficulty + (a == IN_R1 ? 1 : DIFF_COUNT - 1)) % DIFF_COUNT;
        sel = first_open();
        follow_cursor(true);
        unlock_level = 0;
        save_write();
        sound(SND_TICK);
        break;
    case IN_A:
        if (unlock_level && unlock_t < 40) break;         // let the unlock show finish
        if (level_unlocked(G.difficulty, sel + 1)) { sound(SND_GO); game_play(sel + 1); }
        else { sound(SND_LOCKED); wiggle_t = 12; }
        break;
    }
    follow_cursor(false);
}

void home_tick(void) {
    ticks++;
    scroll += (top - scroll) * 0.25f;                    // glide
    if (fabsf(top - scroll) < 0.01f) scroll = top;
    if (wiggle_t > 0) wiggle_t--;
    if (unlock_level) {
        unlock_t++;
        if (unlock_t == 25) {
            sound(SND_UNLOCK);
            fx_sparkle(badge_x(unlock_level - 1), badge_y(unlock_level - 1), 18);
        }
    }
    fx_tick();
}

void home_draw(void) {
    fill_rect(0, 0, SCREEN_W, SCREEN_H, BG);
    text(FONT_L, "stepping stones", SCREEN_W / 2 + 25, 28, 'c', GOLD);
    float hop = fabsf(sinf(ticks * 0.12f)) * 8;
    draw_buddy(SCREEN_W / 2 - 185, 30 - hop, 0.9f, true, ticks * 0.24f, 0);

    for (int d = 0; d < DIFF_COUNT; d++) {                     // difficulty tabs, L1 / R1 to switch
        float x = SCREEN_W / 2 + (d - 1) * 160;
        if (d == G.difficulty) fill_rect(x - 75, 61, 150, 36, (SDL_Color){64, 80, 130, 255});
        text(FONT_M, difficulty_name(d), x, 79, 'c', d == G.difficulty ? INK : DIM);
    }
    draw_button(40, 79, BTN_L1, 1);
    draw_button(SCREEN_W - 40, 79, BTN_R1, 1);

    int n = level_count(G.difficulty), got = 0;
    for (int i = 0; i < n; i++) if (G.stars[G.difficulty][i]) got++;
    clip(0, 104, SCREEN_W, 300);                               // the board's window
    for (int i = 0; i < n; i++) {
        int level = i + 1, stars = G.stars[G.difficulty][i];
        float x = badge_x(i), y = badge_y(i), r = 27;
        if (y < TOP_Y - ROW_H || y > TOP_Y + VIEW_ROWS * ROW_H) continue;   // out of view
        if (i == sel) {                                        // the cursor: a glowing gold ring
            float k = 0.5f + 0.5f * sinf(ticks * 0.25f);
            fill_circle(x, y, r + 9, (SDL_Color){255, 196, 50, (Uint8)(60 + 60 * k)});
            ring(x, y, r + 7, 3, GOLD);
        }
        if (level == unlock_level && unlock_t < 40) {          // the unlock show
            if (unlock_t < 25) {                               // locked, shaking harder and harder
                float shake = sinf(unlock_t * 2.2f) * unlock_t * 0.15f;
                draw_badge(x + shake, y, r, level, false);
            } else {                                           // the lock flies off, the badge pops open
                float u = (unlock_t - 25) / 15.0f;
                float k = u < 0.6f ? u / 0.6f * 1.2f : 1.2f - (u - 0.6f) / 0.4f * 0.2f;
                draw_open(x, y, r * k, level);
                draw_lock(x, y - u * 40, r, (Uint8)(255 * (1 - u)));
            }
            continue;
        }
        if (stars) {
            draw_badge(x, y, r, level, true);
            float sy = y + r * 0.8f;                           // stars sit on the badge's lower edge,
            fill_rect(x - 22, sy - 8, 44, 15, (SDL_Color){24, 28, 48, 255});   // on a dark strip
            fill_circle(x - 22, sy - 0.5f, 7.5f, (SDL_Color){24, 28, 48, 255});
            fill_circle(x + 22, sy - 0.5f, 7.5f, (SDL_Color){24, 28, 48, 255});
            draw_stars(x, sy, 6.5f, stars);
        } else if (level_unlocked(G.difficulty, level)) draw_open(x, y, r, level);
        else {
            float shake = i == sel && wiggle_t > 0 ? sinf(wiggle_t * 2.5f) * 4 : 0;
            draw_badge(x + shake, y, r, level, false);
        }
    }

    clip(0, 0, 0, 0);

    char buf[80];
    int stars = G.stars[G.difficulty][sel];
    if (!level_unlocked(G.difficulty, sel + 1)) {
        snprintf(buf, sizeof buf, "finish level %d first", sel);
        text(FONT_M, buf, SCREEN_W / 2, 428, 'c', DIM);
    } else {                                                   // "level 7   (A) play!"
        const char *what = !stars ? "play!" : stars < 3 ? "play again" : "perfect! play";
        snprintf(buf, sizeof buf, "level %d", sel + 1);
        float w1 = text_width(FONT_M, buf) + 24, w2 = button_width(BTN_A, 1) + 10 + text_width(FONT_M, what);
        float x0 = SCREEN_W / 2 - (w1 + w2) / 2;
        text(FONT_M, buf, x0, 428, 'l', INK);
        prompt(x0 + w1, 428, BTN_A, what, FONT_M, 'l', GOLD, true);
    }
    if (rows() > VIEW_ROWS) {                                  // scrollbar: where we are in the list
        float track_y = 112, track_h = 288, thumb_h = track_h * VIEW_ROWS / rows();
        float thumb_y = track_y + (track_h - thumb_h) * scroll / (rows() - VIEW_ROWS);
        fill_rect(SCREEN_W - 34, track_y, 8, track_h, (SDL_Color){40, 48, 78, 255});
        fill_rect(SCREEN_W - 34, thumb_y, 8, thumb_h, GOLD);
    }
    snprintf(buf, sizeof buf, "%d of %d badges", got, n);
    text(FONT_S, buf, 24, 462, 'l', DIM);
    prompt(SCREEN_W - 24, 462, BTN_START, "menu", FONT_S, 'r', DIM, false);
    fx_draw();
}
