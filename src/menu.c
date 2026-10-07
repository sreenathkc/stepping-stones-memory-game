// Start menu. On the home screen: difficulty, how to play, sounds, music, quit. During a
// level it also has "resume" and "back to levels".
#include "stones.h"
#include <stdio.h>
#include <string.h>

enum { MI_RESUME, MI_LEVELS, MI_DIFF, MI_HELP, MI_SOUND, MI_MUSIC, MI_QUIT };
bool g_menu_open;
char g_owner[40] = "";
static int items[8], nitems, sel;
static bool help;

void menu_load_owner(const char *dir) {               // "made for ..." name, easy to change
    char path[1100];
    snprintf(path, sizeof path, "%sowner.txt", dir);
    FILE *f = fopen(path, "r");
    if (!f) return;
    if (fgets(g_owner, sizeof g_owner, f)) g_owner[strcspn(g_owner, "\r\n")] = 0;
    fclose(f);
}

void menu_open(void) {
    nitems = 0;
    items[nitems++] = MI_RESUME;
    if (g_screen == SCR_GAME) items[nitems++] = MI_LEVELS;
    items[nitems++] = MI_DIFF;
    items[nitems++] = MI_HELP;
    items[nitems++] = MI_SOUND;
    items[nitems++] = MI_MUSIC;
#ifndef __EMSCRIPTEN__
    items[nitems++] = MI_QUIT;                         // (a web page can't quit)
#endif
    g_menu_open = true;
    sel = 0;
    help = false;
    sound(SND_TICK);
}

// returns false when the player chose "quit"
bool menu_input(int a, bool back) {
    if (help) { if (back || a == IN_A) help = false; return true; }
    if (back) { g_menu_open = false; return true; }
    if (a == IN_UP) { sel = (sel + nitems - 1) % nitems; sound(SND_TICK); }
    else if (a == IN_DOWN) { sel = (sel + 1) % nitems; sound(SND_TICK); }
    else if (a == IN_A || ((a == IN_LEFT || a == IN_RIGHT) && items[sel] == MI_DIFF)) {
        switch (items[sel]) {
        case MI_RESUME: g_menu_open = false; break;
        case MI_LEVELS: g_menu_open = false; home_show(0); break;
        case MI_DIFF:                                  // a new difficulty goes back to its badge board
            G.difficulty = (G.difficulty + (a == IN_LEFT ? DIFF_COUNT - 1 : 1)) % DIFF_COUNT;
            save_write();
            G.level = 0;
            home_show(0);
            if (nitems > 1 && items[1] == MI_LEVELS) { menu_open(); sel = 1; }   // rebuild without "back to levels"
            break;
        case MI_HELP: help = true; break;
        case MI_SOUND: g_sound_on = !g_sound_on; save_write(); break;
        case MI_MUSIC: g_music_on = !g_music_on; save_write(); break;
        case MI_QUIT: save_write(); return false;
        }
        sound(SND_TICK);
    }
    return true;
}

void menu_draw(void) {
    const SDL_Color INK = {232, 236, 246, 255}, GOLD = {255, 196, 50, 255}, DIM = {176, 186, 212, 255};
    fill_rect(0, 0, SCREEN_W, SCREEN_H, (SDL_Color){0, 0, 0, 150});
    fill_rect(70, 16, 500, 448, (SDL_Color){28, 36, 64, 255});
    if (help) {
        text(FONT_L, "how to play", 320, 52, 'c', GOLD);
        const char *lines[] = {
            "a safe path crosses the floor",
            "watch it light up green",
            "then walk it with the d-pad",
            "wrong tile? it breaks!",
            "you lose a heart",
            "no falls = 3 stars",
            "win badges, open new levels",
        };
        for (int i = 0; i < 7; i++) text(FONT_S, lines[i], 320, 102 + i * 38, 'c', INK);
        prompt(320, 386, BTN_A, "back", FONT_M, 'c', GOLD, true);
        char credit[96];
        if (g_owner[0]) snprintf(credit, sizeof credit, "made for %s - by casey", g_owner);
        else snprintf(credit, sizeof credit, "handheld game by casey");
        text(FONT_S, credit, 320, 436, 'c', DIM);
        return;
    }
    text(FONT_L, "menu", 320, 50, 'c', GOLD);
    for (int i = 0; i < nitems; i++) {
        char buf[48];
        switch (items[i]) {
        case MI_RESUME: snprintf(buf, sizeof buf, "resume"); break;
        case MI_LEVELS: snprintf(buf, sizeof buf, "back to levels"); break;
        case MI_DIFF: snprintf(buf, sizeof buf, "difficulty: %s", difficulty_name(G.difficulty)); break;
        case MI_HELP: snprintf(buf, sizeof buf, "how to play"); break;
        case MI_SOUND: snprintf(buf, sizeof buf, "sounds: %s", g_sound_on ? "on" : "off"); break;
        case MI_MUSIC: snprintf(buf, sizeof buf, "music: %s", g_music_on ? "on" : "off"); break;
        default: snprintf(buf, sizeof buf, "quit"); break;
        }
        float y = 104 + i * 42;
        if (i == sel) fill_rect(110, y - 20, 420, 40, (SDL_Color){64, 80, 130, 255});
        text(FONT_M, buf, 320, y, 'c', INK);
    }
    prompt(300, 425, BTN_A, "choose", FONT_S, 'r', GOLD, true);
    prompt(340, 425, BTN_START, "close", FONT_S, 'l', DIM, false);
}
