/*
 * PSP homebrew intro
 *   starfield + title  ->  plasma  ->  rotozoomer  ->  copper bars
 *   sine-wave scroller on top of every part, procedural chiptune (no audio files).
 *
 * Build with the pspdev toolchain:   make        (gives EBOOT.PBP)
 * Controls: START = quit, CIRCLE = next part, HOME = quit (system callback).
 *
 * The effect/audio code below is platform independent (see host_preview.c);
 * only the section at the bottom, guarded by __PSP__, touches the PSP API.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>

#ifdef __PSP__
#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspctrl.h>
#include <pspaudio.h>
#include <pspge.h>
#include <psppower.h>

PSP_MODULE_INFO("PSP Intro", PSP_MODULE_USER, 1, 0);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER);
#endif

/* ------------------------------------------------------------------ */
/* configuration - edit these                                          */
/* ------------------------------------------------------------------ */
static const char *GROUP_NAME  = "YOUR GROUP";
static const char *SUBTITLE    = "A PSP HOMEBREW INTRO";
static const char *SCROLL_TEXT =
    "   HELLO PSP WORLD!   THIS IS A TINY HOMEBREW INTRO WRITTEN IN C FOR THE SONY PSP   "
    "STARFIELD - PLASMA - ROTOZOOMER - COPPER BARS - SINE SCROLLER - CHIPTUNE   "
    "EDIT MAIN.C TO CHANGE THE TEXT AND THE PARTS   GREETINGS TO ALL SCENERS   ...   ";

#define PART_FRAMES   720          /* 12 s per part at 60 fps            */
#define NUM_PARTS     4
#define FADE_FRAMES   45

/* ------------------------------------------------------------------ */
/* basics                                                              */
/* ------------------------------------------------------------------ */
#define SCR_W   480
#define SCR_H   272
#define BUF_W   512                /* PSP frame buffer pitch in pixels   */
#define SAMPLE_RATE 44100

/* PSP 8888 pixel in memory (little endian): 0xAABBGGRR */
#define RGB(r, g, b) (0xFF000000u | ((uint32_t)(b) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(r))

static int16_t  isintab[256];      /* sin * 256, 256 steps per circle    */
static uint8_t  basepal[256][3];   /* rainbow                             */
static uint32_t pal[256];          /* rainbow scaled by current fade      */
static uint8_t  tex[65536];        /* rotozoomer texture                  */
static int      g_frame;

static inline int isin(int a) { return isintab[a & 255]; }
static inline int icos(int a) { return isintab[(a + 64) & 255]; }

static uint32_t rng_state = 12345u;
static uint32_t rnd(void) { rng_state = rng_state * 1664525u + 1013904223u; return rng_state >> 8; }

static inline int clamp255(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }

static inline uint32_t scale_col(uint32_t c, int f)   /* f: 0..256 */
{
    uint32_t r = (c & 255), g = (c >> 8) & 255, b = (c >> 16) & 255;
    return RGB((r * f) >> 8, (g * f) >> 8, (b * f) >> 8);
}

static void build_pal(int fade)
{
    for (int i = 0; i < 256; i++)
        pal[i] = RGB((basepal[i][0] * fade) >> 8, (basepal[i][1] * fade) >> 8, (basepal[i][2] * fade) >> 8);
}

static void clear_fb(uint32_t *fb, uint32_t c)
{
    for (int y = 0; y < SCR_H; y++) {
        uint32_t *row = fb + y * BUF_W;
        for (int x = 0; x < SCR_W; x++) row[x] = c;
    }
}

static void fill_rect(uint32_t *fb, int x, int y, int w, int h, uint32_t c)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SCR_W) w = SCR_W - x;
    if (y + h > SCR_H) h = SCR_H - y;
    for (int j = 0; j < h; j++) {
        uint32_t *row = fb + (y + j) * BUF_W + x;
        for (int i = 0; i < w; i++) row[i] = c;
    }
}

/* ------------------------------------------------------------------ */
/* 5x7 font                                                            */
/* ------------------------------------------------------------------ */
static const char FONT_CHARS[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.,!?-:=+'/*()";
static const uint8_t FONT[][7] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* space */
    {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}, /* A */
    {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}, /* B */
    {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}, /* C */
    {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E}, /* D */
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}, /* E */
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}, /* F */
    {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F}, /* G */
    {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}, /* H */
    {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}, /* I */
    {0x07,0x02,0x02,0x02,0x02,0x12,0x0C}, /* J */
    {0x11,0x12,0x14,0x18,0x14,0x12,0x11}, /* K */
    {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}, /* L */
    {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}, /* M */
    {0x11,0x19,0x15,0x13,0x11,0x11,0x11}, /* N */
    {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}, /* O */
    {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}, /* P */
    {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}, /* Q */
    {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}, /* R */
    {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}, /* S */
    {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}, /* T */
    {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}, /* U */
    {0x11,0x11,0x11,0x11,0x11,0x0A,0x04}, /* V */
    {0x11,0x11,0x11,0x15,0x15,0x1B,0x11}, /* W */
    {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}, /* X */
    {0x11,0x11,0x0A,0x04,0x04,0x04,0x04}, /* Y */
    {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}, /* Z */
    {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, /* 0 */
    {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}, /* 1 */
    {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}, /* 2 */
    {0x1E,0x01,0x01,0x0E,0x01,0x01,0x1E}, /* 3 */
    {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, /* 4 */
    {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}, /* 5 */
    {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}, /* 6 */
    {0x1F,0x01,0x02,0x04,0x08,0x08,0x08}, /* 7 */
    {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, /* 8 */
    {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}, /* 9 */
    {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C}, /* . */
    {0x00,0x00,0x00,0x00,0x0C,0x04,0x08}, /* , */
    {0x04,0x04,0x04,0x04,0x04,0x00,0x04}, /* ! */
    {0x0E,0x11,0x01,0x02,0x04,0x00,0x04}, /* ? */
    {0x00,0x00,0x00,0x1F,0x00,0x00,0x00}, /* - */
    {0x00,0x0C,0x0C,0x00,0x0C,0x0C,0x00}, /* : */
    {0x00,0x00,0x1F,0x00,0x1F,0x00,0x00}, /* = */
    {0x00,0x04,0x04,0x1F,0x04,0x04,0x00}, /* + */
    {0x04,0x04,0x08,0x00,0x00,0x00,0x00}, /* ' */
    {0x01,0x02,0x02,0x04,0x08,0x08,0x10}, /* / */
    {0x00,0x15,0x0E,0x1F,0x0E,0x15,0x00}, /* * */
    {0x02,0x04,0x08,0x08,0x08,0x04,0x02}, /* ( */
    {0x08,0x04,0x02,0x02,0x02,0x04,0x08}, /* ) */
};

static void draw_char(uint32_t *fb, int x, int y, int scale, char ch, uint32_t color)
{
    const char *p = strchr(FONT_CHARS, toupper((unsigned char)ch));
    int idx = p ? (int)(p - FONT_CHARS) : 0;
    for (int r = 0; r < 7; r++) {
        uint8_t bits = FONT[idx][r];
        for (int c = 0; c < 5; c++)
            if (bits & (16 >> c)) fill_rect(fb, x + c * scale, y + r * scale, scale, scale, color);
    }
}

static int text_width(const char *s, int scale) { return (int)strlen(s) * 6 * scale - scale; }

static void draw_text(uint32_t *fb, int x, int y, int scale, const char *s, uint32_t color)
{
    for (; *s; s++, x += 6 * scale) draw_char(fb, x, y, scale, *s, color);
}

static void draw_text_shadow(uint32_t *fb, int x, int y, int scale, const char *s, uint32_t color)
{
    draw_text(fb, x + scale, y + scale, scale, s, RGB(0, 0, 0));
    draw_text(fb, x, y, scale, s, color);
}

/* ------------------------------------------------------------------ */
/* effects                                                             */
/* ------------------------------------------------------------------ */
#define NUM_STARS 260
typedef struct { int x, y, z; } Star;
static Star stars[NUM_STARS];

static void star_reset(Star *s, int z)
{
    s->x = (int)(rnd() % 1024) - 512;
    s->y = (int)(rnd() % 1024) - 512;
    s->z = z;
}

static void effect_stars(uint32_t *fb, int t, int fade)
{
    (void)t;
    clear_fb(fb, scale_col(RGB(0, 0, 24), fade));
    for (int i = 0; i < NUM_STARS; i++) {
        Star *s = &stars[i];
        s->z -= 2 + (i & 3);
        if (s->z <= 8) star_reset(s, 256);
        int px = SCR_W / 2 + (s->x * 192) / s->z;
        int py = SCR_H / 2 + (s->y * 192) / s->z;
        if (px < 0 || px >= SCR_W - 3 || py < 0 || py >= SCR_H - 3) { star_reset(s, 256); continue; }
        int b = 256 - s->z;
        int c = (b * fade) >> 8;
        uint32_t col = RGB(clamp255(c), clamp255(c), clamp255(c + 50));
        int size = s->z < 48 ? 3 : (s->z < 110 ? 2 : 1);
        fill_rect(fb, px, py, size, size, col);
    }
}

static void effect_plasma(uint32_t *fb, int t, int fade)
{
    int ax[SCR_W];
    build_pal(fade);
    for (int x = 0; x < SCR_W; x++)
        ax[x] = isin(x * 2 + t * 3) + isin(x * 3 - t * 2 + 64);
    for (int y = 0; y < SCR_H; y++) {
        int ay = isin(y * 3 + t * 2) + isin(y * 5 - t * 3 + 100);
        uint32_t *row = fb + y * BUF_W;
        for (int x = 0; x < SCR_W; x++) {
            int v = ((ax[x] + ay + isin((x + y) * 2 + t)) >> 2) + (t >> 1);
            row[x] = pal[v & 255];
        }
    }
}

static void effect_roto(uint32_t *fb, int t, int fade)
{
    build_pal(fade);
    int a = (t * 3) >> 1;
    int ca = icos(a), sa = isin(a);
    int zoom = 210 + (isin(t * 2) >> 1);
    int du = ca * zoom, dv = sa * zoom;
    int shift = t * 2;
    for (int y = 0; y < SCR_H; y++) {
        int yy = y - SCR_H / 2;
        int u = (-(SCR_W / 2) * ca - yy * sa) * zoom + t * 30000;
        int v = (-(SCR_W / 2) * sa + yy * ca) * zoom + t * 20000;
        uint32_t *row = fb + y * BUF_W;
        for (int x = 0; x < SCR_W; x++) {
            row[x] = pal[(tex[(((v >> 16) & 255) << 8) | ((u >> 16) & 255)] + shift) & 255];
            u += du; v += dv;
        }
    }
}

static void effect_copper(uint32_t *fb, int t, int fade)
{
    for (int y = 0; y < SCR_H; y++) {
        int r = 0, g = 0, b = 12 + y / 14;
        for (int k = 0; k < 8; k++) {
            int yc = SCR_H / 2 + ((isin(t * 2 + k * 24) * 100) >> 8);
            int d = y > yc ? y - yc : yc - y;
            if (d < 20) {
                int in = (20 - d) * 13;
                const uint8_t *c = basepal[(k * 32 + t) & 255];
                r += (c[0] * in) >> 8; g += (c[1] * in) >> 8; b += (c[2] * in) >> 8;
                if (d < 3) { r += 70; g += 70; b += 70; }
            }
        }
        uint32_t col = RGB((clamp255(r) * fade) >> 8, (clamp255(g) * fade) >> 8, (clamp255(b) * fade) >> 8);
        uint32_t *row = fb + y * BUF_W;
        for (int x = 0; x < SCR_W; x++) row[x] = col;
    }
}

/* ------------------------------------------------------------------ */
/* text overlays                                                       */
/* ------------------------------------------------------------------ */
static void scroller(uint32_t *fb, int t, int fade)
{
    const int scale = 3, cw = 6 * scale;
    int len = (int)strlen(SCROLL_TEXT);
    int total = len * cw + SCR_W;
    int pos = (t * 3) % total;                 /* 3 px per frame */
    char s[2] = { 0, 0 };
    for (int i = 0; i < len; i++) {
        int x = SCR_W - pos + i * cw;
        if (x < -cw || x > SCR_W) continue;
        int y = 226 + ((isin(x * 2 + t * 6) * 16) >> 8);
        const uint8_t *c = basepal[(i * 9 + t * 2) & 255];
        uint32_t col = scale_col(RGB(c[0] / 2 + 128, c[1] / 2 + 128, c[2] / 2 + 128), fade);
        s[0] = SCROLL_TEXT[i];
        draw_text(fb, x - 2, y, scale, s, RGB(0, 0, 0)); draw_text(fb, x + 2, y, scale, s, RGB(0, 0, 0));
        draw_text(fb, x, y - 2, scale, s, RGB(0, 0, 0)); draw_text(fb, x, y + 2, scale, s, RGB(0, 0, 0));
        draw_text(fb, x, y, scale, s, col);
    }
}

static void caption(uint32_t *fb, const char *s, int fade)
{
    draw_text_shadow(fb, 8, 8, 2, s, scale_col(RGB(255, 255, 255), fade));
}

static void title_part(uint32_t *fb, int pf, int t, int fade)
{
    int w = text_width(GROUP_NAME, 5);
    int y = 70 + (isin(t * 4) >> 4);
    int x = (SCR_W - w) / 2;
    int len = (int)strlen(GROUP_NAME);
    char s[2] = { 0, 0 };
    for (int i = 0; i < len; i++) {
        const uint8_t *c = basepal[(i * 20 + t * 3) & 255];
        s[0] = GROUP_NAME[i];
        draw_text_shadow(fb, x + i * 30, y + (isin(i * 24 + t * 5) >> 5), 5, s,
                         scale_col(RGB(c[0] / 2 + 128, c[1] / 2 + 128, c[2] / 2 + 128), fade));
    }
    if (pf > 110) {
        const char *p = "PRESENTS";
        draw_text_shadow(fb, (SCR_W - text_width(p, 3)) / 2, 138, 3, p, scale_col(RGB(255, 255, 255), fade));
    }
    if (pf > 230) {
        draw_text_shadow(fb, (SCR_W - text_width(SUBTITLE, 2)) / 2, 178, 2, SUBTITLE, scale_col(RGB(255, 220, 120), fade));
    }
}

/* ------------------------------------------------------------------ */
/* init + one frame                                                    */
/* ------------------------------------------------------------------ */
void intro_init(void)
{
    for (int i = 0; i < 256; i++) {
        float a = (float)i * 6.2831853f / 256.0f;
        isintab[i] = (int16_t)(sinf(a) * 256.0f);
        basepal[i][0] = (uint8_t)(127.5f + 127.5f * sinf(a));
        basepal[i][1] = (uint8_t)(127.5f + 127.5f * sinf(a + 2.0944f));
        basepal[i][2] = (uint8_t)(127.5f + 127.5f * sinf(a + 4.1888f));
    }
    for (int y = 0; y < 256; y++)
        for (int x = 0; x < 256; x++)
            tex[y * 256 + x] = (uint8_t)((x ^ y) + ((x & y) >> 2));
    for (int i = 0; i < NUM_STARS; i++) star_reset(&stars[i], 8 + (int)(rnd() % 248));
    g_frame = 0;
}

static int part_fade(int pf)
{
    int in = pf * 256 / FADE_FRAMES;
    int out = (PART_FRAMES - pf) * 256 / FADE_FRAMES;
    int v = in < out ? in : out;
    return v < 0 ? 0 : (v > 256 ? 256 : v);
}

void intro_skip_part(void)
{
    g_frame = (g_frame / PART_FRAMES + 1) * PART_FRAMES;
}

void intro_frame(uint32_t *fb)
{
    int f = g_frame % (PART_FRAMES * NUM_PARTS);
    int part = f / PART_FRAMES, pf = f % PART_FRAMES;
    int fade = part_fade(pf);
    int t = g_frame;

    switch (part) {
    case 0:  effect_stars(fb, t, fade);  title_part(fb, pf, t, fade);  break;
    case 1:  effect_plasma(fb, t, fade); caption(fb, "PLASMA", fade);  break;
    case 2:  effect_roto(fb, t, fade);   caption(fb, "ROTOZOOMER", fade); break;
    default: effect_copper(fb, t, fade); caption(fb, "COPPER BARS", fade); break;
    }
    scroller(fb, t, fade);
    g_frame++;
}

/* ------------------------------------------------------------------ */
/* chiptune: bass + arpeggio + kick + hi-hat, generated sample by sample */
/* ------------------------------------------------------------------ */
#define BPM          128
#define STEP_SAMPLES (SAMPLE_RATE * 60 / BPM / 4)      /* one 16th note */

static const uint8_t bass_midi[4]   = { 33, 29, 36, 31 };                 /* A1 F1 C2 G1   */
static const uint8_t arp_midi[4][4] = { {69,72,76,81}, {65,69,72,77},     /* Am  F         */
                                        {72,76,79,84}, {67,71,74,79} };   /* C   G         */
static const uint8_t arp_pat[16]    = { 0,1,2,3,2,1,2,3, 0,1,2,3,2,1,3,2 };
static uint32_t midi_inc[128];
static uint32_t ph_bass, ph_arp, ph_arp2, ph_kick, noise_seed = 1;
static int a_step, a_in_step, kick_t = 100000, hat_t = 100000;

void audio_init(void)
{
    for (int m = 0; m < 128; m++) {
        double f = 440.0 * pow(2.0, (m - 69) / 12.0);
        midi_inc[m] = (uint32_t)(f * 4294967296.0 / SAMPLE_RATE);
    }
    ph_bass = ph_arp = ph_arp2 = ph_kick = 0; noise_seed = 1;
    a_step = 0; a_in_step = 0; kick_t = 100000; hat_t = 100000;
}

/* n stereo frames (interleaved L,R) */
void audio_render(int16_t *out, int n)
{
    const int KICK_LEN = 7000, HAT_LEN = 1600;
    for (int i = 0; i < n; i++) {
        int s16 = a_step & 15, bar = (a_step >> 4) & 3;
        if (a_in_step == 0) {
            if ((s16 & 3) == 0) kick_t = 0;
            if ((s16 & 3) == 2) hat_t = 0;
        }
        int env = STEP_SAMPLES - a_in_step;                       /* 16th-note decay */

        /* bass: square, octave jump every 4th step, 8th-note decay */
        int bn = bass_midi[bar] + ((s16 & 4) ? 12 : 0);
        ph_bass += midi_inc[bn];
        int e8 = 2 * STEP_SAMPLES - ((s16 & 1) * STEP_SAMPLES + a_in_step);
        int bass = ((ph_bass >> 31) ? 5000 : -5000) * e8 / (2 * STEP_SAMPLES);

        /* arpeggio: 25% pulse, slightly detuned copy on the right channel */
        int an = arp_midi[bar][arp_pat[s16]];
        ph_arp  += midi_inc[an];
        ph_arp2 += midi_inc[an] + (midi_inc[an] >> 9);
        int arpA = 3200 * env / STEP_SAMPLES;
        int arpL = ((ph_arp  >> 30) == 0) ? arpA : -arpA;
        int arpR = ((ph_arp2 >> 30) == 0) ? arpA : -arpA;

        /* kick: sine sweep 160 -> 45 Hz */
        int kick = 0;
        if (kick_t < KICK_LEN) {
            int d = KICK_LEN - kick_t;
            int hz = 45 + (int)(((int64_t)115 * d * d) / ((int64_t)KICK_LEN * KICK_LEN));
            ph_kick += (uint32_t)hz * 97392u;                     /* 2^32 / 44100 */
            kick = (isin((int)(ph_kick >> 24)) * d) >> 8;
            kick_t++;
        }

        /* hi-hat: short noise burst */
        int hat = 0;
        if (hat_t < HAT_LEN) {
            noise_seed = noise_seed * 1664525u + 1013904223u;
            int nz = (int)((noise_seed >> 16) & 0xFFFF) - 32768;
            hat = (nz >> 4) * (HAT_LEN - hat_t) / (HAT_LEN * 2);
            hat_t++;
        }

        int L = bass + kick + arpL + hat, R = bass + kick + arpR + hat;
        out[2 * i]     = (int16_t)(L > 32000 ? 32000 : (L < -32000 ? -32000 : L));
        out[2 * i + 1] = (int16_t)(R > 32000 ? 32000 : (R < -32000 ? -32000 : R));

        if (++a_in_step >= STEP_SAMPLES) { a_in_step = 0; a_step++; }
    }
}

/* ------------------------------------------------------------------ */
/* PSP platform layer                                                  */
/* ------------------------------------------------------------------ */
#ifdef __PSP__

static volatile int g_running = 1;

static int exit_callback(int arg1, int arg2, void *common)
{
    (void)arg1; (void)arg2; (void)common;
    g_running = 0;
    sceKernelExitGame();
    return 0;
}

static int callback_thread(SceSize args, void *argp)
{
    (void)args; (void)argp;
    int cbid = sceKernelCreateCallback("Exit Callback", exit_callback, NULL);
    sceKernelRegisterExitCallback(cbid);
    sceKernelSleepThreadCB();
    return 0;
}

static void setup_callbacks(void)
{
    int thid = sceKernelCreateThread("update_thread", callback_thread, 0x11, 0xFA0, 0, NULL);
    if (thid >= 0) sceKernelStartThread(thid, 0, NULL);
}

static int audio_thread(SceSize args, void *argp)
{
    static int16_t buf[1024 * 2] __attribute__((aligned(64)));
    (void)args; (void)argp;
    int ch = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL, 1024, PSP_AUDIO_FORMAT_STEREO);
    if (ch < 0) return 0;
    while (g_running) {
        audio_render(buf, 1024);
        sceAudioOutputBlocking(ch, PSP_AUDIO_VOLUME_MAX, buf);
    }
    sceAudioChRelease(ch);
    return 0;
}

int main(int argc, char *argv[])
{
    (void)argc; (void)argv;
    setup_callbacks();
    scePowerSetClockFrequency(333, 333, 166);

    intro_init();
    audio_init();

    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_DIGITAL);

    sceDisplaySetMode(0, SCR_W, SCR_H);
    uint8_t *vram = (uint8_t *)sceGeEdramGetAddr();
    const uint32_t fb_bytes = BUF_W * SCR_H * 4;
    int draw = 1;                                    /* buffer we draw into */
    sceDisplaySetFrameBuf(vram, BUF_W, PSP_DISPLAY_PIXEL_FORMAT_8888, PSP_DISPLAY_SETBUF_NEXTFRAME);

    int athr = sceKernelCreateThread("audio", audio_thread, 0x12, 0x10000, PSP_THREAD_ATTR_USER, NULL);
    if (athr >= 0) sceKernelStartThread(athr, 0, NULL);

    SceCtrlData pad; unsigned int last = 0;
    while (g_running) {
        /* uncached mirror of VRAM: no cache flush needed */
        uint32_t *fb = (uint32_t *)((uint32_t)(vram + draw * fb_bytes) | 0x40000000);
        intro_frame(fb);

        sceDisplayWaitVblankStart();
        sceDisplaySetFrameBuf(vram + draw * fb_bytes, BUF_W, PSP_DISPLAY_PIXEL_FORMAT_8888, PSP_DISPLAY_SETBUF_IMMEDIATE);
        draw ^= 1;

        sceCtrlPeekBufferPositive(&pad, 1);
        if (pad.Buttons & PSP_CTRL_START) break;
        if ((pad.Buttons & PSP_CTRL_CIRCLE) && !(last & PSP_CTRL_CIRCLE)) intro_skip_part();
        last = pad.Buttons;
    }
    g_running = 0;
    sceKernelDelayThread(150000);
    sceKernelExitGame();
    return 0;
}
#endif /* __PSP__ */
