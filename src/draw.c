// Drawing helpers: text with a TrueType font (stb_truetype bakes each size into a texture
// once) and simple filled shapes built from triangles.
#include "stones.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

static SDL_Renderer *R;
static const float SIZES[FONT_COUNT] = {26, 34, 46, 72};   // big enough to read at arm's length

typedef struct {
    SDL_Texture *tex;
    stbtt_bakedchar ch[96];     // ASCII 32..127
    float size, ascent;
} Font;
static Font fonts[FONT_COUNT];

bool draw_init(SDL_Renderer *r, const char *font_path) {
    R = r;
    FILE *f = fopen(font_path, "rb");
    if (!f) { SDL_Log("cannot open %s", font_path); return false; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *ttf = malloc(n);
    fread(ttf, 1, n, f);
    fclose(f);
    stbtt_fontinfo info;
    stbtt_InitFont(&info, ttf, 0);
    int asc, desc, gap;
    stbtt_GetFontVMetrics(&info, &asc, &desc, &gap);
    for (int i = 0; i < FONT_COUNT; i++) {
        const int W = 1024, H = SIZES[i] > 40 ? 1024 : 512;   // the glyph sheet must fit all 96 characters
        unsigned char *mono = malloc(W * H);
        stbtt_BakeFontBitmap(ttf, 0, SIZES[i], mono, W, H, 32, 96, fonts[i].ch);
        // white glyphs with the coverage as alpha, so they can be tinted with SDL colour mod
        Uint32 *rgba = malloc(W * H * 4);
        for (int p = 0; p < W * H; p++) rgba[p] = ((Uint32)mono[p] << 24) | 0x00FFFFFF;
        SDL_Texture *t = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, W, H);
        SDL_UpdateTexture(t, NULL, rgba, W * 4);
        SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
        fonts[i].tex = t;
        fonts[i].size = SIZES[i];
        fonts[i].ascent = asc * stbtt_ScaleForPixelHeight(&info, SIZES[i]);
        free(mono);
        free(rgba);
    }
    free(ttf);
    return true;
}

float text_width(int font, const char *s) {
    float w = 0;
    for (; *s; s++) {
        int c = (unsigned char)*s;
        if (c < 32 || c > 127) continue;
        w += fonts[font].ch[c - 32].xadvance;
    }
    return w;
}

// y is the vertical centre of the text line; scale grows the text around that point
float text_ex(int font, const char *s, float x, float y, char align, float scale, SDL_Color c) {
    Font *fn = &fonts[font];
    float w = text_width(font, s) * scale;
    if (align == 'c') x -= w / 2;
    else if (align == 'r') x -= w;
    float base = y + fn->ascent * 0.38f * scale;              // roughly centre caps on y
    SDL_SetTextureColorMod(fn->tex, c.r, c.g, c.b);
    SDL_SetTextureAlphaMod(fn->tex, c.a);
    float cx = x;
    for (; *s; s++) {
        int ch = (unsigned char)*s;
        if (ch < 32 || ch > 127) continue;
        stbtt_bakedchar *b = &fn->ch[ch - 32];
        SDL_Rect src = {b->x0, b->y0, b->x1 - b->x0, b->y1 - b->y0};
        SDL_FRect dst = {cx + b->xoff * scale, base + b->yoff * scale, src.w * scale, src.h * scale};
        SDL_RenderCopyF(R, fn->tex, &src, &dst);
        cx += b->xadvance * scale;
    }
    return w;
}

float text(int font, const char *s, float x, float y, char align, SDL_Color c) {
    return text_ex(font, s, x, y, align, 1, c);
}

// a five-pointed star, point up
void fill_star(float x, float y, float r, SDL_Color c) {
    SDL_FPoint p[10];
    for (int i = 0; i < 10; i++) {
        float a = (float)(-M_PI / 2 + i * M_PI / 5), rr = i % 2 ? r * 0.45f : r;
        p[i] = (SDL_FPoint){x + cosf(a) * rr, y + sinf(a) * rr};
    }
    SDL_Vertex v[30];
    for (int i = 0; i < 10; i++) {
        v[i * 3] = (SDL_Vertex){{x, y}, c, {0, 0}};
        v[i * 3 + 1] = (SDL_Vertex){p[i], c, {0, 0}};
        v[i * 3 + 2] = (SDL_Vertex){p[(i + 1) % 10], c, {0, 0}};
    }
    SDL_SetRenderDrawBlendMode(R, SDL_BLENDMODE_BLEND);
    SDL_RenderGeometry(R, NULL, v, 30, NULL, 0);
}

// a w x h rectangle centred on (x, y), turned by angle (radians) - confetti
void fill_quad(float x, float y, float w, float h, float angle, SDL_Color c) {
    float ca = cosf(angle), sa = sinf(angle), hw = w / 2, hh = h / 2;
    SDL_FPoint q[4] = {{x - hw * ca + hh * sa, y - hw * sa - hh * ca}, {x + hw * ca + hh * sa, y + hw * sa - hh * ca},
                       {x + hw * ca - hh * sa, y + hw * sa + hh * ca}, {x - hw * ca - hh * sa, y - hw * sa + hh * ca}};
    SDL_Vertex v[6] = {{q[0], c, {0, 0}}, {q[1], c, {0, 0}}, {q[2], c, {0, 0}},
                       {q[0], c, {0, 0}}, {q[2], c, {0, 0}}, {q[3], c, {0, 0}}};
    SDL_SetRenderDrawBlendMode(R, SDL_BLENDMODE_BLEND);
    SDL_RenderGeometry(R, NULL, v, 6, NULL, 0);
}

void fill_ellipse(float x, float y, float rx, float ry, SDL_Color c) {
    enum { SEG = 48 };
    SDL_Vertex v[SEG * 3];
    for (int i = 0; i < SEG; i++) {
        float a0 = (float)(2 * M_PI * i / SEG), a1 = (float)(2 * M_PI * (i + 1) / SEG);
        v[i * 3] = (SDL_Vertex){{x, y}, c, {0, 0}};
        v[i * 3 + 1] = (SDL_Vertex){{x + cosf(a0) * rx, y + sinf(a0) * ry}, c, {0, 0}};
        v[i * 3 + 2] = (SDL_Vertex){{x + cosf(a1) * rx, y + sinf(a1) * ry}, c, {0, 0}};
    }
    SDL_SetRenderDrawBlendMode(R, SDL_BLENDMODE_BLEND);
    SDL_RenderGeometry(R, NULL, v, SEG * 3, NULL, 0);
}

void fill_circle(float x, float y, float radius, SDL_Color c) { fill_ellipse(x, y, radius, radius, c); }

void ring(float x, float y, float radius, float t, SDL_Color c) {
    enum { SEG = 48 };
    SDL_Vertex v[SEG * 6];
    for (int i = 0; i < SEG; i++) {
        float a0 = (float)(2 * M_PI * i / SEG), a1 = (float)(2 * M_PI * (i + 1) / SEG);
        SDL_FPoint o0 = {x + cosf(a0) * radius, y + sinf(a0) * radius}, o1 = {x + cosf(a1) * radius, y + sinf(a1) * radius};
        SDL_FPoint i0 = {x + cosf(a0) * (radius - t), y + sinf(a0) * (radius - t)}, i1 = {x + cosf(a1) * (radius - t), y + sinf(a1) * (radius - t)};
        v[i * 6 + 0] = (SDL_Vertex){o0, c, {0, 0}};
        v[i * 6 + 1] = (SDL_Vertex){o1, c, {0, 0}};
        v[i * 6 + 2] = (SDL_Vertex){i0, c, {0, 0}};
        v[i * 6 + 3] = (SDL_Vertex){i0, c, {0, 0}};
        v[i * 6 + 4] = (SDL_Vertex){o1, c, {0, 0}};
        v[i * 6 + 5] = (SDL_Vertex){i1, c, {0, 0}};
    }
    SDL_SetRenderDrawBlendMode(R, SDL_BLENDMODE_BLEND);
    SDL_RenderGeometry(R, NULL, v, SEG * 6, NULL, 0);
}

void fill_rect(float x, float y, float w, float h, SDL_Color c) {
    SDL_SetRenderDrawBlendMode(R, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(R, c.r, c.g, c.b, c.a);
    SDL_FRect rc = {x, y, w, h};
    SDL_RenderFillRectF(R, &rc);
}


// only draw inside this rectangle (w = 0: everywhere again)
void clip(int x, int y, int w, int h) {
    SDL_Rect rc = {x, y, w, h};
    SDL_RenderSetClipRect(R, w > 0 ? &rc : NULL);
}
