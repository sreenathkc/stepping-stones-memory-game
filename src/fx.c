// Little effects: confetti (spinning paper bits with gravity), sparkles (stars that fly out and
// fade), the round buddy the player walks as, and star rows for scores.
#include "stones.h"
#include <math.h>
#include <stdlib.h>

#define MAXP 400

typedef struct {
    float x, y, vx, vy, rot, vr, size;
    SDL_Color c;
    int life, max_life;
    bool star;
} Part;
static Part parts[MAXP];

static const SDL_Color PARTY[6] = {{255, 70, 90, 255}, {255, 190, 40, 255}, {70, 220, 110, 255},
                                   {60, 170, 255, 255}, {200, 100, 255, 255}, {255, 120, 200, 255}};

static float frand(float a, float b) { return a + (b - a) * (float)rand() / (float)RAND_MAX; }

static Part *spawn(void) {
    for (int i = 0; i < MAXP; i++) if (parts[i].life <= 0) return &parts[i];
    return &parts[rand() % MAXP];                       // full: reuse one
}

// a burst of confetti from (x, y); power = how fast it shoots out
void fx_confetti(float x, float y, int n, float power) {
    for (int i = 0; i < n; i++) {
        Part *p = spawn();
        float a = frand(0, 2 * (float)M_PI), sp = frand(0.3f, 1.0f) * power;
        *p = (Part){x, y, cosf(a) * sp, sinf(a) * sp - power * 0.6f, frand(0, 6), frand(-0.3f, 0.3f),
                    frand(6, 11), PARTY[rand() % 6], 0, 0, false};
        p->life = p->max_life = (int)frand(50, 90);
    }
}

void fx_sparkle(float x, float y, int n) {
    for (int i = 0; i < n; i++) {
        Part *p = spawn();
        float a = frand(0, 2 * (float)M_PI), sp = frand(1.5f, 4.5f);
        *p = (Part){x, y, cosf(a) * sp, sinf(a) * sp, 0, 0, frand(4, 8),
                    i % 2 ? (SDL_Color){255, 240, 140, 255} : (SDL_Color){255, 255, 255, 255}, 0, 0, true};
        p->life = p->max_life = (int)frand(18, 30);
    }
}

void fx_tick(void) {
    for (int i = 0; i < MAXP; i++) {
        Part *p = &parts[i];
        if (p->life <= 0) continue;
        p->life--;
        p->x += p->vx;
        p->y += p->vy;
        if (p->star) { p->vx *= 0.9f; p->vy *= 0.9f; }
        else { p->vy += 0.25f; p->vx *= 0.98f; if (p->vy > 3) p->vy = 3; p->rot += p->vr; }   // flutter down
    }
}

void fx_draw(void) {
    for (int i = 0; i < MAXP; i++) {
        Part *p = &parts[i];
        if (p->life <= 0) continue;
        SDL_Color c = p->c;
        float fade = (float)p->life / p->max_life;
        if (fade < 0.3f) c.a = (Uint8)(255 * fade / 0.3f);
        if (p->star) fill_star(p->x, p->y, p->size * (0.5f + fade * 0.5f), c);
        else fill_quad(p->x, p->y, p->size, p->size * 0.55f * fabsf(cosf(p->rot * 2)) + 1, p->rot, c);
    }
}

void fx_clear(void) { for (int i = 0; i < MAXP; i++) parts[i].life = 0; }

// the player: a round orange buddy with big eyes and two little legs (happy = ^ ^ eyes and a
// smile). The legs swing opposite ways while walking; squash makes it wider and shorter.
void draw_buddy(float x, float y, float s, bool happy, float walk, float squash) {
    const SDL_Color RIM = {196, 86, 30, 255}, BODY = {255, 132, 56, 255}, CHEEK = {255, 160, 150, 255},
                    EYE = {255, 255, 255, 255}, PUPIL = {30, 30, 40, 255}, LEG = {150, 60, 20, 255},
                    SHOE = {70, 90, 200, 255};
    float sx = s * (1 + squash * 0.25f), sy = s * (1 - squash * 0.2f);
    float by = y + squash * 3 * s;                       // body sinks a little when squashed
    for (int side = -1; side <= 1; side += 2) {          // legs first, the body covers the hips
        float a = sinf(walk) * 0.7f * side;
        float hx = x + side * 6 * sx, hy = by + 10 * sy, L = 13 * s * (1 - squash * 0.3f);
        float fx = hx + sinf(a) * L, fy = hy + cosf(a) * L - fmaxf(0, -sinf(walk) * side) * 4 * s;   // the swinging leg lifts
        fill_quad((hx + fx) / 2, (hy + fy) / 2, 4.5f * s, L, -a, LEG);
        fill_ellipse(fx + side * 2 * s, fy + 1 * s, 5 * s, 3.5f * s, SHOE);
    }
    fill_ellipse(x, by, 16 * sx, 16 * sy, RIM);
    fill_ellipse(x, by - 1 * sy, 14.5f * sx, 14.5f * sy, BODY);
    y = by;
    fill_circle(x - 5 * s, y - 7 * s, 4 * s, (SDL_Color){255, 190, 140, 255});      // shine
    fill_circle(x - 9 * s, y + 4 * s, 3 * s, CHEEK);
    fill_circle(x + 9 * s, y + 4 * s, 3 * s, CHEEK);
    if (happy) {                                        // ^ ^ eyes and an open smile
        fill_quad(x - 7 * s, y - 3 * s, 7 * s, 2.4f * s, -0.6f, PUPIL);
        fill_quad(x - 3.5f * s, y - 3 * s, 7 * s, 2.4f * s, 0.6f, PUPIL);
        fill_quad(x + 3.5f * s, y - 3 * s, 7 * s, 2.4f * s, -0.6f, PUPIL);
        fill_quad(x + 7 * s, y - 3 * s, 7 * s, 2.4f * s, 0.6f, PUPIL);
        fill_circle(x, y + 5 * s, 4 * s, (SDL_Color){140, 40, 30, 255});
        fill_rect(x - 4.5f * s, y + 0.5f * s, 9 * s, 4.5f * s, BODY);
    } else {
        fill_circle(x - 5 * s, y - 3 * s, 4 * s, EYE);
        fill_circle(x + 5 * s, y - 3 * s, 4 * s, EYE);
        fill_circle(x - 4.5f * s, y - 3.5f * s, 2 * s, PUPIL);
        fill_circle(x + 5.5f * s, y - 3.5f * s, 2 * s, PUPIL);
    }
}

void draw_stars(float x, float y, float r, int earned) {
    for (int i = 0; i < 3; i++) {
        float sx = x + (i - 1) * r * 2.3f, sy = y - (i == 1 ? r * 0.35f : 0);    // middle one higher
        fill_star(sx, sy, r + 1.5f, (SDL_Color){120, 80, 10, 255});
        fill_star(sx, sy, r, i < earned ? (SDL_Color){255, 210, 50, 255} : (SDL_Color){58, 66, 100, 255});
    }
}

// ---- button pictures ----------------------------------------------------------------------------
static void pill(float x, float y, float w, float h, SDL_Color c) {
    fill_rect(x - w / 2 + h / 2, y - h / 2, w - h, h, c);
    fill_circle(x - w / 2 + h / 2, y, h / 2, c);
    fill_circle(x + w / 2 - h / 2, y, h / 2, c);
}

float button_width(int btn, float s) {
    switch (btn) {
    case BTN_A: case BTN_B: return 38 * s;
    case BTN_START: return 92 * s;
    case BTN_DPAD: return 40 * s;
    default: return 56 * s;                              // L1 / R1
    }
}

void draw_button(float x, float y, int btn, float s) {
    const SDL_Color WHITE = {255, 255, 255, 255}, SHADOW = {0, 0, 0, 90};
    switch (btn) {
    case BTN_A: case BTN_B: {
        SDL_Color face = btn == BTN_A ? (SDL_Color){60, 190, 100, 255} : (SDL_Color){230, 80, 80, 255};
        SDL_Color rim = btn == BTN_A ? (SDL_Color){30, 120, 60, 255} : (SDL_Color){140, 40, 40, 255};
        fill_circle(x, y + 3 * s, 19 * s, SHADOW);
        fill_circle(x, y, 19 * s, rim);
        fill_circle(x, y - 1.5f * s, 16.5f * s, face);
        text_ex(FONT_M, btn == BTN_A ? "A" : "B", x, y - 1 * s, 'c', s * 0.85f, WHITE);
        break;
    }
    case BTN_START:
        pill(x, y + 3 * s, 92 * s, 30 * s, SHADOW);
        pill(x, y, 92 * s, 30 * s, (SDL_Color){70, 80, 110, 255});
        pill(x, y - 1.5f * s, 88 * s, 26 * s, (SDL_Color){110, 122, 160, 255});
        text_ex(FONT_S, "START", x, y - 1 * s, 'c', s * 0.7f, WHITE);
        break;
    case BTN_L1: case BTN_R1:
        pill(x, y + 3 * s, 56 * s, 30 * s, SHADOW);
        pill(x, y, 56 * s, 30 * s, (SDL_Color){70, 80, 110, 255});
        pill(x, y - 1.5f * s, 52 * s, 26 * s, (SDL_Color){110, 122, 160, 255});
        text_ex(FONT_S, btn == BTN_L1 ? "L1" : "R1", x, y - 1 * s, 'c', s * 0.8f, WHITE);
        break;
    case BTN_DPAD: {
        SDL_Color c = {110, 122, 160, 255}, d = {60, 68, 96, 255};
        fill_rect(x - 7 * s, y - 20 * s, 14 * s, 40 * s, d);
        fill_rect(x - 20 * s, y - 7 * s, 40 * s, 14 * s, d);
        fill_rect(x - 5.5f * s, y - 18.5f * s, 11 * s, 37 * s, c);
        fill_rect(x - 18.5f * s, y - 5.5f * s, 37 * s, 11 * s, c);
        fill_circle(x, y, 3 * s, d);
        break;
    }
    }
}

float prompt(float x, float y, int btn, const char *label, int font, char align, SDL_Color c, bool pulse) {
    float s = pulse ? 1 + 0.08f * sinf(SDL_GetTicks() * 0.008f) : 1;
    float bw = button_width(btn, 1), gap = 10, w = bw + gap + text_width(font, label);
    float left = align == 'c' ? x - w / 2 : align == 'r' ? x - w : x;
    draw_button(left + bw / 2, y, btn, s);
    text(font, label, left + bw + gap, y, 'l', c);
    return w;
}
