// Stepping Stones — entry point: window, controls, the 30 Hz loop, headless test mode.
//
// Normal run:  ./stones
// Test run:    ./stones --headless --seed 1 --script "-:130 U:7 L:7" --shots 10,140 --out DIR
//   Tokens: KEYS:TICKS (keys L R U D A B, M = menu, pressed on the first tick, then TICKS
//   ticks pass; "-" = no key; Q/W = L1/R1). The game starts on the home screen (A plays the
//   selected level). "P" steps along the real path (one step). Saves and sound are
//   off in test runs.
#include "stones.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

static SDL_GameController *pads[4];

static void pads_open(void) {
    for (int i = 0; i < SDL_NumJoysticks() && i < 4; i++)
        if (!pads[i] && SDL_IsGameController(i)) pads[i] = SDL_GameControllerOpen(i);
}

static bool pad_down(SDL_GameControllerButton b) {
    for (int i = 0; i < 4; i++) if (pads[i] && SDL_GameControllerGetButton(pads[i], b)) return true;
    return false;
}

static void press(int action, bool back) {
    if (g_menu_open) { if (!menu_input(action, back)) { SDL_Event q = {.type = SDL_QUIT}; SDL_PushEvent(&q); } return; }
    if (g_screen == SCR_HOME) home_input(action); else game_input(action);
}

static void tick(void) {
    if (g_screen == SCR_HOME) home_tick(); else game_tick();
}

// test aid: take the next step along the real path
static void step_on_path(void) {
    for (int i = 0; i < G.path_len; i++) {
        if (G.stepped[G.path_r[i]][G.path_c[i]]) continue;
        int c = G.path_c[i], r = G.path_r[i];
        if (G.pr != ROWS && r < G.pr) game_input(IN_UP);
        else if (G.pr != ROWS && r > G.pr) game_input(IN_DOWN);
        else if (c < G.pc) game_input(IN_LEFT);
        else if (c > G.pc) game_input(IN_RIGHT);
        else game_input(IN_UP);
        return;
    }
}

static void draw_all(SDL_Renderer *ren) {
    SDL_SetRenderDrawColor(ren, 18, 24, 44, 255);
    SDL_RenderClear(ren);
    if (g_screen == SCR_HOME) home_draw(); else game_draw();
    if (g_menu_open) menu_draw();
}

#ifdef __EMSCRIPTEN__
// the web page's on-screen buttons (phones and tablets) call this: 0-7 = IN_* actions, 8 = START
EMSCRIPTEN_KEEPALIVE void web_button(int b) {
    if (b == 8) { if (g_menu_open) g_menu_open = false; else menu_open(); }
    else if (b >= 0 && b < 8) press(b, b == IN_B);
}
#endif

// one pass of the real-time loop: read buttons/keys, run the 30 Hz game ticks that are due, draw
static SDL_Renderer *g_ren;
static Uint64 last;
static double acc;
static bool running = true;

static void frame(void) {
    const double step = 1.0 / TICK_HZ;
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) running = false;
        if (e.type == SDL_CONTROLLERDEVICEADDED) pads_open();
        if (e.type == SDL_CONTROLLERBUTTONDOWN) {
            if (pad_down(SDL_CONTROLLER_BUTTON_START) && pad_down(SDL_CONTROLLER_BUTTON_BACK)) running = false;
            switch (e.cbutton.button) {
            case SDL_CONTROLLER_BUTTON_DPAD_LEFT: press(IN_LEFT, false); break;
            case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: press(IN_RIGHT, false); break;
            case SDL_CONTROLLER_BUTTON_DPAD_UP: press(IN_UP, false); break;
            case SDL_CONTROLLER_BUTTON_DPAD_DOWN: press(IN_DOWN, false); break;
            case SDL_CONTROLLER_BUTTON_A: press(IN_A, false); break;
            case SDL_CONTROLLER_BUTTON_B: press(IN_B, true); break;
            case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: press(IN_L1, false); break;
            case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: press(IN_R1, false); break;
            case SDL_CONTROLLER_BUTTON_START:
                if (g_menu_open) g_menu_open = false; else menu_open();
                break;
            }
        }
        // keyboard (computer / browser): letters match the button pictures - A, B, L, R -
        // arrows are the d-pad, Enter or Esc is START
        if (e.type == SDL_KEYDOWN && !e.key.repeat) {
            SDL_Keycode k = e.key.keysym.sym;
            if (k == SDLK_RETURN || k == SDLK_ESCAPE) { if (g_menu_open) g_menu_open = false; else menu_open(); }
            else if (k == SDLK_l) press(IN_L1, false);
            else if (k == SDLK_r) press(IN_R1, false);
            else if (k == SDLK_LEFT) press(IN_LEFT, false);
            else if (k == SDLK_RIGHT) press(IN_RIGHT, false);
            else if (k == SDLK_UP) press(IN_UP, false);
            else if (k == SDLK_DOWN) press(IN_DOWN, false);
            else if (k == SDLK_a || k == SDLK_SPACE) press(IN_A, false);
            else if (k == SDLK_b || k == SDLK_BACKSPACE) press(IN_B, true);
        }
    }
#ifndef __EMSCRIPTEN__
    if (!running) return;
#endif
    Uint64 now = SDL_GetPerformanceCounter();
    acc += (double)(now - last) / SDL_GetPerformanceFrequency();
    last = now;
    if (acc > 0.25) acc = 0.25;
    while (acc >= step) { if (!g_menu_open) tick(); acc -= step; }
    draw_all(g_ren);
    SDL_RenderPresent(g_ren);
}

int main(int argc, char **argv) {
    bool headless = false;
    const char *script = NULL, *shots = "", *outdir = ".";
    unsigned seed = (unsigned)time(NULL);
    int start_level = 0, start_diff = -1;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--headless")) headless = true;
        else if (!strcmp(argv[i], "--script") && i + 1 < argc) script = argv[++i];
        else if (!strcmp(argv[i], "--shots") && i + 1 < argc) shots = argv[++i];
        else if (!strcmp(argv[i], "--out") && i + 1 < argc) outdir = argv[++i];
        else if (!strcmp(argv[i], "--seed") && i + 1 < argc) seed = (unsigned)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--level") && i + 1 < argc) start_level = atoi(argv[++i]);   // test aid
        else if (!strcmp(argv[i], "--diff") && i + 1 < argc) start_diff = atoi(argv[++i]);
    }
    srand(seed);
    if (headless) SDL_SetHint(SDL_HINT_VIDEODRIVER, "dummy");
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | (headless ? 0 : SDL_INIT_AUDIO)) != 0) {
        SDL_Log("SDL_Init: %s", SDL_GetError());
        return 1;
    }
    SDL_Window *win = NULL;
    SDL_Renderer *ren;
    SDL_Surface *canvas = NULL;
    if (headless) {
        canvas = SDL_CreateRGBSurfaceWithFormat(0, SCREEN_W, SCREEN_H, 32, SDL_PIXELFORMAT_ARGB8888);
        ren = SDL_CreateSoftwareRenderer(canvas);
    } else {
#ifdef __EMSCRIPTEN__
        Uint32 wflags = 0;                                   // the page scales the canvas itself
#else
        Uint32 wflags = SDL_WINDOW_FULLSCREEN_DESKTOP;
#endif
        win = SDL_CreateWindow("Stepping Stones", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, SCREEN_W, SCREEN_H, wflags);
        if (!win) { SDL_Log("window: %s", SDL_GetError()); return 1; }
        ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (!ren) ren = SDL_CreateRenderer(win, -1, 0);
        SDL_RenderSetLogicalSize(ren, SCREEN_W, SCREEN_H);
        SDL_ShowCursor(SDL_DISABLE);
    }
    if (!ren) { SDL_Log("renderer: %s", SDL_GetError()); return 1; }

    // everything lives next to the program, so the folder can be copied anywhere
    char base[1024], dir[1100], path[1200];
    char *bp = SDL_GetBasePath();
    snprintf(base, sizeof base, "%s", bp ? bp : "./");
    SDL_free(bp);
    snprintf(dir, sizeof dir, "%sassets/", base);
    snprintf(path, sizeof path, "%sFredoka-SemiBold.ttf", dir);
    if (!draw_init(ren, path)) return 1;
    menu_load_owner(dir);
    pads_open();
    if (!headless) {
        save_init(base);
        save_load();
        audio_init();
        snprintf(path, sizeof path, "%swelcome.wav", dir);
        sfx_play_file(path);
    }
    if (headless && getenv("STONES_SAVEDIR")) {           // test aid: load a save (progress, stars)
        snprintf(path, sizeof path, "%s/", getenv("STONES_SAVEDIR"));
        save_init(path);
        save_load();
        save_init("");                                    // ...but never write it
    }
    home_show(0);
    if (headless && start_diff >= 0) G.difficulty = start_diff;
    if (headless && start_level > 0) game_play(start_level);

    if (headless) {
        char buf[4096];
        snprintf(buf, sizeof buf, "%s", script ? script : "-:10");
        int frame = 0;
        for (char *tok = strtok(buf, " "); tok; tok = strtok(NULL, " ")) {
            int ticks = 1;
            char *colon = strchr(tok, ':');
            if (colon) { *colon = 0; ticks = atoi(colon + 1); }
            for (char *k = tok; *k; k++) {
                switch (*k) {
                case 'L': press(IN_LEFT, false); break;
                case 'R': press(IN_RIGHT, false); break;
                case 'U': press(IN_UP, false); break;
                case 'D': press(IN_DOWN, false); break;
                case 'A': press(IN_A, false); break;
                case 'B': press(IN_B, true); break;
                case 'M': menu_open(); break;
                case 'Q': press(IN_L1, false); break;
                case 'W': press(IN_R1, false); break;
                case 'P': step_on_path(); break;
                }
            }
            for (int t = 0; t < ticks; t++) {
                if (!g_menu_open) tick();
                frame++;
                char want[16], list[1100];
                snprintf(want, sizeof want, ",%d,", frame);
                snprintf(list, sizeof list, ",%s,", shots);
                if (strstr(list, want)) {
                    draw_all(ren);
                    snprintf(path, sizeof path, "%s/shot_%04d.bmp", outdir, frame);
                    SDL_SaveBMP(canvas, path);
                }
            }
        }
        printf("screen=%d level=%d len=%d lives=%d phase=%d t=%d player=%d,%d path=", g_screen, G.level, G.path_len, G.lives, G.phase, G.t, G.pc, G.pr);
        for (int i = 0; i < G.path_len; i++) printf("%d,%d ", G.path_c[i], G.path_r[i]);
        printf("\n");
        SDL_Quit();
        return 0;
    }

    g_ren = ren;
    last = SDL_GetPerformanceCounter();
#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop(frame, 0, 1);              // the browser calls frame() every screen refresh
#else
    while (running) frame();
#endif
    save_write();
    SDL_Quit();
    return 0;
}
