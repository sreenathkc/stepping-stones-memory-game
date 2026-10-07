// Sound, all synthesized at startup (no sound files except the welcome greeting).
//
// Each effect is built from "tones" added together into a buffer. A tone has a wave shape
// (sine, triangle, soft square, bell, noise), a pitch that can glide from f0 to f1 (slide
// whistles, boings), an envelope (quick attack, then it dies away at `decay` per second) and
// optional vibrato (the wobble in the sad trombone). The background music is made the same
// way: a bass line, chord arpeggios and soft hi-hats over C - Am - F - G, looped.
// A tiny mixer adds the playing effects and the music together.
#include "stones.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RATE 48000
#define VOICES 10
#define PI2 6.2831853f

typedef struct { Sint16 *data; Uint32 len; } Buf;
static Buf sfx[SND_COUNT], file_buf, music;
static struct { const Buf *b; Uint32 pos; } voice[VOICES];
static Uint32 music_pos, music_wait;     // music starts after the welcome greeting
static SDL_AudioDeviceID dev;
bool g_sound_on = true, g_music_on = true;

enum { SINE, TRI, SQUARE, BELL, NOISE };

// ---- building sounds ----------------------------------------------------------------------------
static float *acc;                       // float workspace while a sound is built
static Uint32 acc_len;

static void begin(float ms) {
    acc_len = (Uint32)(ms * RATE / 1000);
    acc = calloc(acc_len, sizeof(float));
}

static Buf finish(float volume) {
    Buf b = {malloc(acc_len * sizeof(Sint16)), acc_len};
    for (Uint32 i = 0; i < acc_len; i++) {
        float s = tanhf(acc[i] * volume) * 30000;       // tanh: soft limit instead of harsh clipping
        b.data[i] = (Sint16)s;
    }
    free(acc);
    return b;
}

static float wave(int w, float ph) {
    float x = ph - floorf(ph);                           // 0..1 through one cycle
    switch (w) {
    case TRI: return 4 * fabsf(x - 0.5f) - 1;
    case SQUARE: return 0.8f * (sinf(PI2 * x) + sinf(3 * PI2 * x) / 3 + sinf(5 * PI2 * x) / 5);
    default: return sinf(PI2 * x);
    }
}

// add a tone at `at` ms, lasting `dur` ms, gliding f0 -> f1 Hz
static void tone(float at, float dur, float f0, float f1, int w, float vol, float decay, float vibrato) {
    Uint32 start = (Uint32)(at * RATE / 1000), n = (Uint32)(dur * RATE / 1000);
    float ph = 0, ph2 = 0, lp = 0;
    for (Uint32 k = 0; k < n && start + k < acc_len; k++) {
        float t = (float)k / RATE, u = (float)k / n;
        float f = f0 * powf(f1 / f0, u);                  // glide (even steps in pitch)
        if (vibrato > 0) f *= 1 + vibrato * sinf(PI2 * 5.5f * t);
        float env = fminf(1, t / 0.004f) * expf(-decay * t);
        float tail = n - k < 480 ? (float)(n - k) / 480 : 1;        // 10 ms fade: no clicks
        float s;
        if (w == NOISE) {                                 // noise, a little low-passed by f0
            float white = (float)rand() / (float)RAND_MAX * 2 - 1;
            lp += (white - lp) * fminf(1, f / 8000);
            s = lp * 1.5f;
        } else if (w == BELL) {                           // bell: a second, out-of-tune partial that fades faster
            s = sinf(PI2 * ph) + 0.5f * sinf(PI2 * ph2) * expf(-8 * t);
            ph2 += f * 2.76f / RATE;
        } else s = wave(w, ph);
        ph += f / RATE;
        acc[start + k] += s * env * tail * vol;
    }
}

static float midi(int m) { return 440.0f * powf(2, (m - 69) / 12.0f); }

static void build_effects(void) {
    begin(40); tone(0, 40, 1500, 1100, TRI, 0.5f, 30, 0); sfx[SND_TICK] = finish(0.6f);
    begin(110); tone(0, 110, 260, 620, TRI, 0.8f, 18, 0); sfx[SND_STEP] = finish(0.4f);      // jump "boing"
    begin(120); tone(0, 70, 900, 900, NOISE, 0.9f, 45, 0); tone(0, 110, 130, 70, SINE, 0.9f, 30, 0);
    sfx[SND_LAND] = finish(0.7f);                                                            // footstep "tap"
    begin(90); tone(0, 90, 1000, 1000, SINE, 0.9f, 45, 0); tone(0, 20, 3000, 3000, NOISE, 0.4f, 120, 0);
    sfx[SND_COUNTDOWN] = finish(0.6f);                                                       // wood block
    begin(450);                                                                              // "go!": bright arpeggio
    tone(0, 150, midi(84), midi(84), SQUARE, 0.5f, 10, 0);
    tone(90, 150, midi(88), midi(88), SQUARE, 0.5f, 10, 0);
    tone(180, 270, midi(91), midi(91), SQUARE, 0.6f, 6, 0);
    sfx[SND_GO] = finish(0.5f);
    begin(300); tone(0, 160, 2500, 600, NOISE, 1.0f, 18, 0); tone(0, 260, 110, 45, SINE, 1.0f, 10, 0);
    sfx[SND_CRACK] = finish(0.8f);                                                           // crack + thud
    begin(900); tone(0, 900, 1300, 160, SINE, 0.8f, 1.5f, 0.012f); sfx[SND_FALL] = finish(0.6f);   // slide whistle
    begin(1500);                                                                             // fanfare
    int fan[] = {67, 72, 76, 79};
    for (int i = 0; i < 4; i++) tone(i * 120, i == 3 ? 900 : 140, midi(fan[i]), midi(fan[i]), SQUARE, 0.5f, i == 3 ? 2.5f : 8, 0);
    int chord[] = {60, 64, 67, 72};
    for (int i = 0; i < 4; i++) tone(360, 1000, midi(chord[i]), midi(chord[i]), TRI, 0.3f, 2.5f, 0);
    for (int i = 0; i < 6; i++) tone(700 + i * 70, 400, midi(96 + (i % 3) * 4), midi(96 + (i % 3) * 4), BELL, 0.18f, 8, 0);
    sfx[SND_WIN] = finish(0.55f);
    int star_n[3] = {84, 88, 91};                                                            // one ding per star, higher each
    for (int s = 0; s < 3; s++) {
        begin(600);
        tone(0, 600, midi(star_n[s]), midi(star_n[s]), BELL, 0.7f, 5, 0);
        tone(40, 500, midi(star_n[s] + 12), midi(star_n[s] + 12), BELL, 0.3f, 7, 0);
        tone(0, 60, 6000, 6000, NOISE, 0.15f, 60, 0);
        sfx[SND_STAR1 + s] = finish(0.55f);
    }
    begin(1300);                                                                             // badge: magic shimmer
    int sc[] = {72, 76, 79, 84, 88, 91, 96, 100};
    for (int i = 0; i < 8; i++) tone(i * 45, 500, midi(sc[i]), midi(sc[i]), BELL, 0.35f, 6, 0);
    for (int i = 0; i < 3; i++) tone(400, 900, midi(chord[i] + 12), midi(chord[i] + 12), TRI, 0.25f, 2.5f, 0);
    sfx[SND_BADGE] = finish(0.55f);
    begin(600);                                                                              // unlock: click + ta-da
    tone(0, 30, 4000, 4000, NOISE, 0.6f, 80, 0);
    tone(60, 160, midi(79), midi(79), SQUARE, 0.45f, 9, 0);
    tone(200, 400, midi(84), midi(84), SQUARE, 0.5f, 4, 0);
    sfx[SND_UNLOCK] = finish(0.55f);
    begin(300); tone(0, 110, 150, 140, SQUARE, 0.5f, 6, 0); tone(150, 140, 140, 120, SQUARE, 0.5f, 6, 0);
    sfx[SND_LOCKED] = finish(0.5f);                                                          // "uh-uh"
    begin(2200);                                                                             // sad trombone: wah wah wah waaah
    float trom[4] = {midi(55), midi(54), midi(53), midi(52)};
    for (int i = 0; i < 3; i++) tone(i * 420, 400, trom[i], trom[i] * 0.97f, SQUARE, 0.55f, 1.2f, 0.004f);
    tone(1260, 940, trom[3], trom[3] * 0.94f, SQUARE, 0.6f, 0.8f, 0.03f);
    sfx[SND_OVER] = finish(0.5f);
    int scale[16] = {72, 74, 76, 79, 81, 84, 86, 88, 91, 93, 96, 98, 100, 103, 105, 108};    // major pentatonic
    for (int i = 0; i < 16; i++) {
        begin(500);
        tone(0, 500, midi(scale[i]), midi(scale[i]), BELL, 0.6f, 6, 0);
        sfx[SND_NOTE0 + i] = finish(0.5f);
    }
}

// background music: 8 bars at 112 beats a minute, C - Am - F - G twice
static void build_music(void) {
    const float beat = 60000.0f / 112;
    const int roots[4] = {48, 45, 41, 43};               // C3 A2 F2 G2
    const int thirds[4] = {4, 3, 4, 4};                  // major / minor chord
    begin(beat * 32);
    for (int bar = 0; bar < 8; bar++) {
        int r = roots[bar % 4], th = thirds[bar % 4];
        float b0 = bar * 4 * beat;
        tone(b0, beat * 1.9f, midi(r), midi(r), TRI, 0.55f, 1.2f, 0);                 // bass on 1 and 3
        tone(b0 + 2 * beat, beat * 1.9f, midi(r + 7), midi(r + 7), TRI, 0.45f, 1.2f, 0);
        int arp[8] = {0, th, 7, 12, 7, th, 7, 12};                                     // chord, up and down
        for (int i = 0; i < 8; i++) {
            int m = r + 24 + arp[i];
            tone(b0 + i * beat / 2, beat / 2, midi(m), midi(m), SQUARE, 0.14f, 6, 0);
        }
        for (int i = 0; i < 4; i++) tone(b0 + i * beat + beat / 2, 40, 7000, 7000, NOISE, 0.06f, 50, 0);   // hi-hat
        if (bar % 2 == 1) {                                                            // a little bell tune
            int mel[4] = {12, 7, th + 12, 7};
            for (int i = 0; i < 4; i++) tone(b0 + i * beat, beat, midi(r + 36 + mel[i] - 12), midi(r + 36 + mel[i] - 12), BELL, 0.12f, 3, 0);
        }
    }
    music = finish(0.5f);
}

// ---- mixing -------------------------------------------------------------------------------------
static void mix(void *u, Uint8 *stream, int bytes) {
    (void)u;
    Sint16 *out = (Sint16 *)stream;
    int frames = bytes / 4;
    for (int i = 0; i < frames; i++) {
        int s = 0;
        for (int v = 0; v < VOICES; v++) {
            if (!voice[v].b) continue;
            s += voice[v].b->data[voice[v].pos++];
            if (voice[v].pos >= voice[v].b->len) voice[v].b = NULL;
        }
        if (music_wait > 0) music_wait--;
        else if (g_music_on && music.data) {
            s += music.data[music_pos] * 3 / 10;          // music quietly underneath
            if (++music_pos >= music.len) music_pos = 0;
        }
        if (s > 32767) s = 32767;
        if (s < -32768) s = -32768;
        out[2 * i] = out[2 * i + 1] = (Sint16)s;
    }
}

bool audio_init(void) {
    SDL_AudioSpec want = {0}, have;
    want.freq = RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
#ifdef __EMSCRIPTEN__
    want.samples = 2048;                                 // browsers need a bigger buffer to avoid crackles
#else
    want.samples = 512;
#endif
    want.callback = mix;
    build_effects();
    build_music();
    dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!dev) { SDL_Log("audio: %s", SDL_GetError()); return false; }
    SDL_PauseAudioDevice(dev, 0);
    return true;
}

static void play(const Buf *b) {
    if (!dev || !b->data) return;
    SDL_LockAudioDevice(dev);
    int slot = 0;
    Uint32 most = 0;
    for (int v = 0; v < VOICES; v++) {
        if (!voice[v].b) { slot = v; break; }
        if (voice[v].pos > most) { most = voice[v].pos; slot = v; }    // all busy: replace the oldest
    }
    voice[slot].b = b;
    voice[slot].pos = 0;
    SDL_UnlockAudioDevice(dev);
}

void sound(int id) {
    if (g_sound_on && id >= 0 && id < SND_COUNT) play(&sfx[id]);
}

void note(int i) { sound(SND_NOTE0 + (i < 0 ? 0 : i > 15 ? 15 : i)); }

// The welcome greeting: a WAV in any format, converted to the mixer's mono 16-bit 48 kHz
void sfx_play_file(const char *path) {
    if (!dev || !g_sound_on) return;
    SDL_AudioSpec spec;
    Uint8 *buf;
    Uint32 len;
    if (!SDL_LoadWAV(path, &spec, &buf, &len)) return;
    SDL_AudioCVT cvt;
    if (SDL_BuildAudioCVT(&cvt, spec.format, spec.channels, spec.freq, AUDIO_S16SYS, 1, RATE) > 0) {
        cvt.len = (int)len;
        cvt.buf = SDL_malloc((size_t)len * cvt.len_mult);
        memcpy(cvt.buf, buf, len);
        SDL_ConvertAudio(&cvt);
        SDL_FreeWAV(buf);
        buf = cvt.buf;
        len = (Uint32)cvt.len_cvt;
    }
    file_buf.data = (Sint16 *)buf;
    file_buf.len = len / 2;
    SDL_LockAudioDevice(dev);
    music_wait = file_buf.len + RATE / 2;                // let the greeting finish first
    SDL_UnlockAudioDevice(dev);
    play(&file_buf);
}
