// Stepping Stones — a memory path game for handhelds (and the web), inspired by a famous
// YouTube tile-pattern challenge: a hidden safe path crosses a floor of tiles. It lights up for
// a few seconds, then you walk it from memory.
#ifndef STONES_H
#define STONES_H

#include <SDL.h>
#include <stdbool.h>

#define SCREEN_W 640
#define SCREEN_H 480
#define TICK_HZ 30
#define MAX_COLS 8              // the floor is 5 wide on easy, 8 on medium and hard
#define ROWS 8
#define MAX_PATH (MAX_COLS * ROWS)
#define MAX_LEVELS 50           // levels per difficulty (easy 50, others 20); each one earns a badge

// difficulty (menu / L1 R1 on the home screen, easy by default): floor width, how long paths
// start and grow, how long they're shown, hearts, and whether the path is shown again after a fall
enum { DIFF_EASY, DIFF_MEDIUM, DIFF_HARD, DIFF_COUNT };
const char *difficulty_name(int d);
int level_count(int d);

// ---- draw.c ----
enum { FONT_S, FONT_M, FONT_L, FONT_XL, FONT_COUNT };      // 26, 34, 46, 72 px
bool draw_init(SDL_Renderer *r, const char *font_path);
void fill_circle(float x, float y, float radius, SDL_Color c);
void fill_ellipse(float x, float y, float rx, float ry, SDL_Color c);
void fill_rect(float x, float y, float w, float h, SDL_Color c);
void ring(float x, float y, float radius, float thickness, SDL_Color c);
void fill_star(float x, float y, float r, SDL_Color c);
void fill_quad(float x, float y, float w, float h, float angle, SDL_Color c);
void clip(int x, int y, int w, int h);      // limit drawing to a rectangle; w = 0 turns it off
// align: 'l' left, 'c' centre, 'r' right; returns the text width
float text(int font, const char *s, float x, float y, char align, SDL_Color c);
float text_ex(int font, const char *s, float x, float y, char align, float scale, SDL_Color c);
float text_width(int font, const char *s);

// ---- fx.c: confetti, sparkles, the little buddy ----
void fx_confetti(float x, float y, int n, float power);
void fx_sparkle(float x, float y, int n);
void fx_tick(void);
void fx_draw(void);
void fx_clear(void);
// walk: leg swing phase (radians, 0 = standing still); squash: >0 flattens (landing)
void draw_buddy(float x, float y, float scale, bool happy, float walk, float squash);
void draw_stars(float x, float y, float r, int earned);    // a row of 3 stars, `earned` filled
// Button pictures, so even a non-reader can match them to the handheld's buttons:
// a green round A, a red round B, a START pill, L1 / R1 shoulder tabs, a d-pad cross.
enum { BTN_A, BTN_B, BTN_START, BTN_L1, BTN_R1, BTN_DPAD };
float button_width(int btn, float scale);
void draw_button(float x, float y, int btn, float scale);  // centred on (x, y)
// a button picture followed by a word, e.g. (A) play!; align 'l' 'c' 'r'; pulse = gently throb
float prompt(float x, float y, int btn, const char *label, int font, char align, SDL_Color c, bool pulse);

// ---- audio.c ----
enum {
    SND_TICK, SND_STEP, SND_LAND, SND_COUNTDOWN, SND_GO, SND_CRACK, SND_FALL, SND_WIN, SND_STAR1,
    SND_STAR2, SND_STAR3, SND_BADGE, SND_UNLOCK, SND_LOCKED, SND_OVER,
    SND_NOTE0,                  // SND_NOTE0 + i: bell notes up a happy scale (path tiles)
    SND_COUNT = SND_NOTE0 + 16
};
bool audio_init(void);
void sound(int id);
void note(int i);               // i-th note of the scale (stays on the top note past the end)
void sfx_play_file(const char *path);       // welcome greeting (any WAV format)
extern bool g_sound_on, g_music_on;

// ---- game.c ----
// Phases of one level:
//   SHOW  the safe path lights up tile by tile, then a countdown
//   PLAY  path hidden, walk it; a wrong tile cracks
//   FALL  you drop through a wrong tile (then back to the start, or out of hearts)
//   CLEAR you reached the far side: confetti, stars, badge; A goes back home
//   OVER  out of hearts: A tries again, B goes home
enum { PH_SHOW, PH_PLAY, PH_FALL, PH_CLEAR, PH_OVER };

typedef struct {
    // the level
    int level, cols;
    int path_c[MAX_PATH], path_r[MAX_PATH], path_len;
    bool safe[ROWS][MAX_COLS];      // on the path
    bool hole[ROWS][MAX_COLS];      // a wrong tile you fell through (stays open this level)
    bool stepped[ROWS][MAX_COLS];   // safe tiles found on this attempt
    int nstepped;
    // the player: row ROWS = start pad (bottom), row -1 = finish pad (top)
    int pc, pr;
    int from_c, from_r, hop;        // hop animation: ticks left, from the previous tile
    int lives, max_lives, falls, stars_won;
    bool new_badge;
    // timing
    int phase, t;                   // t = ticks since the phase began
    int reveal_step, show_total;    // SHOW: ticks per lit tile, ticks in all
    long ticks;
    // settings and progress (saved)
    int difficulty;
    unsigned char stars[DIFF_COUNT][MAX_LEVELS];   // 0 = not cleared yet, else 1..3
} Game;
extern Game G;

enum { IN_LEFT, IN_RIGHT, IN_UP, IN_DOWN, IN_A, IN_B, IN_L1, IN_R1 };
enum { SCR_HOME, SCR_GAME };
extern int g_screen;
bool level_unlocked(int diff, int level);   // level 1 always; others once the one before is cleared
void game_play(int level);
void game_input(int action);
void game_tick(void);
void game_draw(void);
void save_init(const char *dir);
void save_write(void);
void save_load(void);

// ---- home.c: the badge board / level select ----
void home_show(int cleared_level);          // go home; 0, or the level just cleared (unlock show)
void home_input(int action);
void home_tick(void);
void home_draw(void);
void draw_badge(float x, float y, float r, int level, bool unlocked);

// ---- menu.c ----
extern char g_owner[40];                    // who the game is made for (assets/owner.txt)
extern bool g_menu_open;
void menu_load_owner(const char *assets_dir);
void menu_open(void);
bool menu_input(int action, bool back);     // false = quit
void menu_draw(void);

#endif
