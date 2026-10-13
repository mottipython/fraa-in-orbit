/*
 * fraa-in-orbit: screensaver of orbital mechanics, after "Anathem".
 *
 * A fraa in orbit about Arbre chases the sphere he has let go of, and finds that to
 * catch up he must slow down.
 *
 *   fraa-in-orbit            full screen on every display; any key or mouse movement quits
 *   fraa-in-orbit --window   in a window; Esc or Q quits
 *   FraaInOrbit.scr /s       Windows screensaver conventions
 */
#include <SDL.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* The pictures (drawn with Gemini, prepared by tools/make_sprites.py) are built
 * into the program as PNG and JPEG files and decoded at start-up. */
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wmisleading-indentation"
#endif
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_THREAD_LOCALS /* else Windows builds need libwinpthread-1.dll */
#include "stb_image.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#if defined(__APPLE__)
/* Mach-O: its own section names, and C names carry a leading underscore */
#define ART_SECTION ".section __DATA,__const\n"
#define ART_SYM "_art_"
#elif defined(_WIN32)
#define ART_SECTION ".section .rdata,\"dr\"\n"
#define ART_SYM "art_"
#else
#define ART_SECTION ".section .rodata\n"
#define ART_SYM "art_"
#endif
#define ART(name, file) \
    __asm__(ART_SECTION ".globl " ART_SYM #name "\n" ART_SYM #name ":\n.incbin \"assets/art/" file "\"\n" \
            ".globl " ART_SYM #name "_end\n" ART_SYM #name "_end:\n.text\n"); \
    extern const unsigned char art_##name[], art_##name##_end[];
ART(sky, "sky.jpg")
ART(banner, "banner.png")
ART(glow, "glow.jpg")
ART(planet, "planet.png")
ART(sphere, "sphere.png")
ART(f_wave, "f_wave.png")
ART(f_drift, "f_drift.png")
ART(f_thrust, "f_thrust.png")
ART(f_puzzled, "f_puzzled.png")
ART(f_dismay, "f_dismay.png")
ART(f_glad, "f_glad.png")

enum { SP_SKY, SP_BANNER, SP_GLOW, SP_PLANET, SP_SPHERE, SP_F_WAVE, SP_F_DRIFT, SP_F_THRUST, SP_F_PUZZLED, SP_F_DISMAY, SP_F_GLAD, NSPRITE };
static struct { const unsigned char *file, *end; int w, h; unsigned char *px; } sprite[NSPRITE] = {
    {art_sky, art_sky_end},
    {art_banner, art_banner_end},
    {art_glow, art_glow_end},
    {art_planet, art_planet_end},
    {art_sphere, art_sphere_end},
    {art_f_wave, art_f_wave_end},
    {art_f_drift, art_f_drift_end},
    {art_f_thrust, art_f_thrust_end},
    {art_f_puzzled, art_f_puzzled_end},
    {art_f_dismay, art_f_dismay_end},
    {art_f_glad, art_f_glad_end},
};
static SDL_Texture **cur_tex; /* the pictures, for the renderer being drawn to */

static void load_sprites(void)
{
    for (int i = 0; i < NSPRITE; i++) {
        int n;
        sprite[i].px = stbi_load_from_memory(sprite[i].file, (int)(sprite[i].end - sprite[i].file), &sprite[i].w, &sprite[i].h, &n, 4);
        if (!sprite[i].px) { sprite[i].w = sprite[i].h = 1; }
    }
}
static void make_textures(SDL_Renderer *r, SDL_Texture **tex)
{
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
    for (int i = 0; i < NSPRITE; i++) {
        tex[i] = NULL;
        if (!sprite[i].px) continue;
        tex[i] = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, sprite[i].w, sprite[i].h);
        if (!tex[i]) continue;
        SDL_UpdateTexture(tex[i], NULL, sprite[i].px, sprite[i].w * 4);
        /* the glow is light painted on black, and is added to what is behind it */
        SDL_SetTextureBlendMode(tex[i], i == SP_GLOW ? SDL_BLENDMODE_ADD : SDL_BLENDMODE_BLEND);
    }
}

/* ---- options ---- */
static int opt_window = 0;
static double opt_speed = 1.0;
static uint64_t opt_seed = 0;
static const char *opt_snapshot = NULL;
static double opt_time = 30;
static int opt_w = 1920, opt_h = 1200;

/* ---- colours ---- */
typedef struct { Uint8 r, g, b; } Rgb;
static const Rgb BG = {8, 8, 20}, INK = {46, 32, 22}, CHORD = {150, 44, 30};
static const Rgb THREAD = {170, 180, 230}, GOLD = {250, 214, 120}, DEEP = {14, 16, 38};

/* ---- geometry buffer ---- */
static SDL_Vertex *vb;
static int *ib;
static int nv, ni, vcap, icap;

static void vb_need(int v, int i)
{
    if (nv + v > vcap) { vcap = (nv + v) * 2; vb = realloc(vb, sizeof *vb * vcap); }
    if (ni + i > icap) { icap = (ni + i) * 2; ib = realloc(ib, sizeof *ib * icap); }
}
static inline SDL_Color col(Rgb c, double a)
{
    SDL_Color o = {c.r, c.g, c.b, (Uint8)(a < 0 ? 0 : a > 1 ? 255 : a * 255)};
    return o;
}
static inline void vert(double x, double y, SDL_Color c)
{
    vb[nv].position.x = (float)x; vb[nv].position.y = (float)y;
    vb[nv].color = c; vb[nv].tex_coord.x = vb[nv].tex_coord.y = 0;
    nv++;
}
static void quad4(double x0, double y0, double x1, double y1, double x2, double y2, double x3, double y3,
                  SDL_Color c0, SDL_Color c1, SDL_Color c2, SDL_Color c3)
{
    vb_need(4, 6);
    int b = nv;
    vert(x0, y0, c0); vert(x1, y1, c1); vert(x2, y2, c2); vert(x3, y3, c3);
    ib[ni++] = b; ib[ni++] = b + 1; ib[ni++] = b + 2; ib[ni++] = b; ib[ni++] = b + 2; ib[ni++] = b + 3;
}
static void quad(double x0, double y0, double x1, double y1, double x2, double y2, double x3, double y3, SDL_Color c)
{
    quad4(x0, y0, x1, y1, x2, y2, x3, y3, c, c, c, c);
}
static void rect(double x, double y, double w, double h, SDL_Color c) { quad(x, y, x + w, y, x + w, y + h, x, y + h, c); }
static void disc(double cx, double cy, double r, SDL_Color centre, SDL_Color rim)
{
    int n = r < 4 ? 10 : r < 30 ? 28 : 56;
    vb_need(n + 1, 3 * n);
    int b = nv;
    vert(cx, cy, centre);
    for (int i = 0; i < n; i++) vert(cx + r * cos(i * 2 * M_PI / n), cy + r * sin(i * 2 * M_PI / n), rim);
    for (int i = 0; i < n; i++) { ib[ni++] = b; ib[ni++] = b + 1 + i; ib[ni++] = b + 1 + (i + 1) % n; }
}
/* a stroke with round ends */
static void line(double x0, double y0, double x1, double y1, double w, SDL_Color c)
{
    double dx = x1 - x0, dy = y1 - y0, l = hypot(dx, dy);
    if (l > 1e-9) {
        double nx = -dy / l * w / 2, ny = dx / l * w / 2;
        quad(x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny, c);
    }
    if (w > 2.5) { disc(x0, y0, w / 2, c, c); disc(x1, y1, w / 2, c, c); }
}
__attribute__((unused)) static void ring(double cx, double cy, double r, double w, SDL_Color c)
{
    for (int i = 0; i < 64; i++) {
        double a = i * M_PI / 32, b = (i + 1) * M_PI / 32;
        line(cx + cos(a) * r, cy + sin(a) * r, cx + cos(b) * r, cy + sin(b) * r, w, c);
    }
}
static void flush(SDL_Renderer *r)
{
    if (ni) SDL_RenderGeometry(r, NULL, vb, nv, ib, ni);
    nv = ni = 0;
}

static SDL_Renderer *ren; /* where the current frame is being drawn */

/* a picture stretched over a rectangle; sx0..sx1 picks a slice of it across (0..1) */
static void pic_part(int which, double x, double y, double w, double h, double sx0, double sx1, double tilt, int flip)
{
    flush(ren);
    if (!cur_tex || !cur_tex[which]) return;
    SDL_Rect src = {(int)(sx0 * sprite[which].w), 0, (int)((sx1 - sx0) * sprite[which].w), sprite[which].h};
    SDL_FRect dst = {(float)x, (float)y, (float)w, (float)h};
    SDL_FPoint pivot = {(float)(w / 2), (float)h};
    SDL_RenderCopyExF(ren, cur_tex[which], &src, &dst, tilt, &pivot, flip ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
}
/* a picture `tall` high, standing with the point (ax, ay) of it (0..1 across and down) at (x, y) */
__attribute__((unused)) static void pic(int which, double x, double y, double tall, double ax, double ay)
{
    double w = tall * sprite[which].w / sprite[which].h;
    pic_part(which, x - w * ax, y - tall * ay, w, tall, 0, 1, 0, 0);
}

/* ---- lettering: pen strokes on a 4 x 6 grid, drawn with a round nib ---- */
static const char *GLYPH[] = {
    "A06 02 20 42 46|04 44", "B00 06 36 45 44 33 03|33 42 41 30 00", "C41 30 10 01 05 16 36 45",
    "D00 06 26 44 42 20 00", "E40 00 06 46|03 33", "F40 00 06|03 33", "G41 30 10 01 05 16 36 45 43 23",
    "H00 06|40 46|03 43", "I20 26|10 30|16 36", "J40 45 36 16 05", "K00 06|40 03 46", "L00 06 46",
    "M06 00 23 40 46", "N06 00 46 40", "O10 30 41 45 36 16 05 01 10", "P06 00 30 41 42 33 03",
    "Q10 30 41 45 36 16 05 01 10|24 46", "R06 00 30 41 42 33 03|23 46", "S41 30 10 01 02 13 33 44 45 36 16 05",
    "T00 40|20 26", "U00 05 16 36 45 40", "V00 26 40", "W00 16 23 36 40", "X00 46|40 06", "Y00 23 40|23 26",
    "Z00 40 06 46", "010 30 41 45 36 16 05 01 10", "111 20 26|16 36", "201 10 30 41 42 06 46",
    "301 10 30 41 42 33 23|33 44 45 36 16 05", "436 30 04 44", "540 00 03 33 44 45 36 16 05",
    "630 10 01 05 16 36 45 44 33 03", "700 40 16", "813 02 01 10 30 41 42 33 13 04 05 16 36 45 44 33",
    "943 13 02 01 10 30 41 45 36 16", ",25 26 17", ".26 26", ":22 22|25 25", "'20 21", "?01 10 30 41 42 24|26 26",
    "-13 33", "!20 24|26 26", "+03 43|21 25", "=02 42|04 44",
};
static const char *glyph(char ch)
{
    for (unsigned i = 0; i < sizeof GLYPH / sizeof *GLYPH; i++)
        if (GLYPH[i][0] == ch) return GLYPH[i] + 1;
    return NULL;
}
static double text_width(const char *str, double s) { return strlen(str) * 6.2 * s - 2.2 * s; }
/* text centred on cx, top at y; s is one grid step, so letters are 6s tall */
static void text(const char *str, double cx, double y, double s, double weight, SDL_Color c)
{
    double x = cx - text_width(str, s) / 2;
    for (; *str; str++, x += 6.2 * s) {
        const char *g = glyph(*str);
        if (!g) continue;
        int have = 0; double lx = 0, ly = 0;
        for (; *g; g++) {
            if (*g == '|') { have = 0; continue; }
            if (*g == ' ') continue;
            double gx = x + (g[0] - '0') * s, gy = y + (g[1] - '0') * s;
            g++;
            if (have) line(lx, ly, gx, gy, s * weight, c);
            disc(gx, gy, s * weight * (have && lx == gx && ly == gy ? 0.62 : 0.5), c, c); /* the nib is round */
            lx = gx; ly = gy; have = 1;
        }
    }
}
/* ---- the orbit ----
 *
 * A fraa in orbit about Arbre has let go of his sphere, and it has drifted ahead of
 * him along the same orbit. Thrusting towards it only loses it: more speed lifts him
 * into a higher orbit, and a higher orbit takes longer to go round. Thrusting away
 * from it drops him into a lower, quicker orbit, and one turn later they meet.
 *
 * Arbre is at (0, 0) with radius 1, and its pull is such that speed on a circle of
 * radius r is 1 / sqrt(r). Both start on a circle of radius R0, going anticlockwise,
 * the sphere PHI ahead. The motion is worked out step by step from the pull alone;
 * the dotted line is the orbit his position and speed at that moment put him on. */
#define R0 1.6
#define PHI (35 * M_PI / 180)
#define TURN 12.0                              /* seconds on screen for one turn of the starting orbit */
#define RATE (2 * M_PI * R0 * sqrt(R0) / TURN) /* so much of the orbit's own time passes in a second */
static const double LENGTH[3] = {11, 20, 33};
#define ROUND 64.0
#define BURN 3.0                               /* seconds into a scene when he thrusts */
#define MEET (BURN + TURN * (1 - PHI / (2 * M_PI))) /* when the quicker orbit brings him back, with the sphere there */

static double T, anim_t;
static uint64_t seed_state;
static int scene = -1;
static double fx, fy, fvx, fvy;                /* the fraa */
static double sphere_at;                       /* the sphere's angle round its circle */
static double thrust_t = -10, thrust_way;      /* when he last thrust, and whether forward (+1) or back (-1) */
static int burned, met;

static uint64_t next_rand(void)
{
    seed_state ^= seed_state << 13; seed_state ^= seed_state >> 7; seed_state ^= seed_state << 17;
    return seed_state;
}
static double ramp(double t, double a, double b) { return t <= a ? 0 : t >= b ? 1 : (t - a) / (b - a); }
static double ease(double x) { return x * x * (3 - 2 * x); }

static void begin(int which)
{
    double a = 100 * M_PI / 180, v = 1 / sqrt(R0);
    scene = which;
    fx = R0 * cos(a); fy = R0 * sin(a); fvx = -v * sin(a); fvy = v * cos(a);
    sphere_at = a + PHI;
    burned = met = 0; thrust_t = -10;
}

/* change his speed along the way he is going, to `speed` */
static void thrust(double speed, double now)
{
    double v = hypot(fvx, fvy);
    thrust_way = speed > v ? 1 : -1; thrust_t = now;
    fvx *= speed / v; fvy *= speed / v;
}

static void new_round(void) { T = 0; scene = -1; (void)next_rand(); }

static void update(double dt)
{
    anim_t += dt;
    dt *= opt_speed;
    T += dt;
    if (T > ROUND) new_round();
    int k = 0;
    double t = T;
    while (k < 2 && t >= LENGTH[k]) t -= LENGTH[k++];
    if (k != scene) begin(k);
    if (k == 1 && !burned && t >= BURN) { burned = 1; thrust(hypot(fvx, fvy) * 1.085, anim_t); }  /* forwards: the obvious thing */
    if (k == 2 && !burned && t >= BURN) {                                                           /* backwards */
        double a = R0 * pow(1 - PHI / (2 * M_PI), 2.0 / 3.0); /* the orbit whose turn is shorter by just the sphere's lead */
        burned = 1; thrust(sqrt(2 / R0 - 1 / a), anim_t);
    }
    if (k == 2 && !met && t >= MEET) {                                                              /* and forwards again, to stay */
        double r = hypot(fx, fy), v = 1 / sqrt(r);
        met = 1; thrust_way = 1; thrust_t = anim_t;
        fvx = -v * fy / r; fvy = v * fx / r;
    }
    /* falling: many small steps of pull, then move */
    int steps = 40;
    double h = dt * RATE / steps;
    for (int i = 0; i < steps; i++) {
        double r = hypot(fx, fy), g = -1 / (r * r * r);
        fvx += g * fx * h / 2; fvy += g * fy * h / 2;
        fx += fvx * h; fy += fvy * h;
        r = hypot(fx, fy); g = -1 / (r * r * r);
        fvx += g * fx * h / 2; fvy += g * fy * h / 2;
    }
    sphere_at += dt * RATE / (R0 * sqrt(R0));
}

/* ---- drawing ---- */
static double unit;
static int sw, sh;
static double sx(double x) { return sw * 0.58 + x * sh * 0.158; }
static double sy(double y) { return sh * 0.57 - y * sh * 0.158; }

static void glow(double x, double y, double d, double alpha)
{
    flush(ren);
    if (!cur_tex || !cur_tex[SP_GLOW] || alpha <= 0) return;
    SDL_FRect dst = {(float)(x - d / 2), (float)(y - d / 2), (float)d, (float)d};
    SDL_SetTextureAlphaMod(cur_tex[SP_GLOW], (Uint8)(fmin(alpha, 1) * 255));
    SDL_RenderCopyF(ren, cur_tex[SP_GLOW], NULL, &dst);
    SDL_SetTextureAlphaMod(cur_tex[SP_GLOW], 255);
}
static void turned_flip(int which, double x, double y, double tall, double deg, int flip)
{
    flush(ren);
    if (!cur_tex || !cur_tex[which]) return;
    double w = tall * sprite[which].w / sprite[which].h;
    SDL_FRect dst = {(float)(x - w / 2), (float)(y - tall / 2), (float)w, (float)tall};
    SDL_RenderCopyExF(ren, cur_tex[which], NULL, &dst, deg, NULL, flip ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
}
static void turned(int which, double x, double y, double tall, double deg) { turned_flip(which, x, y, tall, deg, 0); }
/* the fraa, `scale` of the screen height for the tallest drawing of him: all the
 * drawings are to one scale, so he is the same size whatever he is doing */
static void fraa(int pose, double x, double y, double scale, double deg, int flip)
{
    turned_flip(pose, x, y, sh * scale * sprite[pose].h / 420.0, deg, flip);
}

static void draw_caption(int w, int h, const char *l1, const char *l2)
{
    double s1 = unit * 0.46, s2 = unit * 0.3;
    double widest = fmax(text_width(l1, s1), text_width(l2, s2)), room = w * 0.62;
    if (widest > room) { s1 *= room / widest; s2 *= room / widest; }
    double bw = fmax(text_width(l1, s1), text_width(l2, s2)) + unit * 13, bh = unit * 11.5;
    double bx = w / 2.0 - bw / 2, by = unit * 1.2;
    double cap = 0.135, cw = bh * 1.15 * sprite[SP_BANNER].w * cap / sprite[SP_BANNER].h;
    pic_part(SP_BANNER, bx + cw - 1, by, bw - 2 * cw + 2, bh, cap, 1 - cap, 0, 0);
    pic_part(SP_BANNER, bx, by, cw, bh, 0, cap, 0, 0);
    pic_part(SP_BANNER, bx + bw - cw, by, cw, bh, 1 - cap, 1, 0, 0);
    text(l1, w / 2.0, by + bh * 0.25, s1, 0.95, col(INK, 1));
    text(l2, w / 2.0, by + bh * 0.25 + s1 * 6 + unit * 1.2, s2, 0.9, col(CHORD, 1));
}

static void draw_scene(SDL_Renderer *r, int w, int h)
{
    ren = r; sw = w; sh = h;
    unit = (w < h ? w : h) * 0.0125;
    SDL_SetRenderDrawColor(r, BG.r, BG.g, BG.b, 255);
    SDL_RenderClear(r);
    pic_part(SP_SKY, 0, 0, w, h, 0, 1, 0, 0);
    int k = scene < 0 ? 0 : scene;
    double t = T;
    for (int i = 0; i < k; i++) t -= LENGTH[i];

    /* Arbre, turning */
    glow(sx(0), sy(0), h * 0.158 * 3.0, 0.14);
    turned(SP_PLANET, sx(0), sy(0), h * 0.158 * 2, anim_t * 2.5);

    /* the sphere's orbit, a faint circle, and the orbit he is on now, dotted */
    for (int i = 0; i < 120; i++) {
        double a0 = i * M_PI / 60, a1 = (i + 1) * M_PI / 60;
        line(sx(R0 * cos(a0)), sy(R0 * sin(a0)), sx(R0 * cos(a1)), sy(R0 * sin(a1)), unit * 0.14, col(THREAD, 0.3));
    }
    {
        double hh = fx * fvy - fy * fvx, rr = hypot(fx, fy);
        double ex = fvy * hh - fx / rr, ey = -fvx * hh - fy / rr, e = hypot(ex, ey), p = hh * hh, om = atan2(ey, ex);
        if (e < 0.95) for (int i = 0; i < 180; i += 2) {
            double n0 = i * M_PI / 90, n1 = (i + 1.2) * M_PI / 90, r0 = p / (1 + e * cos(n0)), r1 = p / (1 + e * cos(n1));
            line(sx(r0 * cos(om + n0)), sy(r0 * sin(om + n0)), sx(r1 * cos(om + n1)), sy(r1 * sin(om + n1)), unit * 0.24, col(GOLD, 0.85));
        }
    }
    flush(r);

    /* what he is doing */
    double since = anim_t - thrust_t, caught = met ? ease(ramp(since, 0.2, 1.2)) : 0;
    int pose = SP_F_DRIFT;
    if (k == 0) pose = t < 3.5 ? SP_F_WAVE : SP_F_DRIFT;
    else if (since < 1.6 && !(met && since > 1.0)) pose = SP_F_THRUST;
    else if (k == 1) pose = t < BURN ? SP_F_DRIFT : t < 9 ? SP_F_PUZZLED : SP_F_DISMAY;
    else pose = met ? SP_F_GLAD : t < BURN ? SP_F_PUZZLED : SP_F_DRIFT;

    /* the sphere, until he has it */
    double bx = R0 * cos(sphere_at), by = R0 * sin(sphere_at), px = sx(fx), py = sy(fy);
    if (pose != SP_F_GLAD) {
        double x = sx(bx) + (px - sx(bx)) * caught, y = sy(by) + (py - sy(by)) * caught;
        glow(x, y, h * 0.09, 0.25);
        turned(SP_SPHERE, x, y, h * 0.04, anim_t * 40);
    }
    /* the puff from his thrusters: behind him to go faster, ahead of him to go slower */
    if (since >= 0 && since < 1.0) {
        double v = hypot(fvx, fvy), ux = fvx / v, uy = fvy / v, f = since / 1.0;
        for (int i = 0; i < 5; i++) {
            double d = (0.12 + 0.5 * f + 0.07 * i) * -thrust_way;
            glow(sx(fx + ux * d), sy(fy + uy * d), h * (0.035 + 0.04 * f + 0.006 * i), (1 - f) * 0.42);
        }
    }
    /* the drawings of him reaching, straining and wailing face right; he is turned to
     * face the sphere, which is the way he is going, or away from it to thrust back */
    double bob = sin(anim_t * 1.3) * 4;
    int sideways = pose == SP_F_DRIFT || pose == SP_F_THRUST || pose == SP_F_DISMAY, left = fvx < 0;
    if (pose == SP_F_THRUST && thrust_way < 0) left = !left;
    fraa(pose, px, py, 0.125, bob, sideways && left);

    /* and close to, so that his face can be seen */
    {
        double pw = h * 0.36, ph = h * 0.36, x0 = w * 0.03, y0 = h * 0.52, b = unit * 0.5;
        rect(x0 - b, y0 - b, pw + 2 * b, ph + 2 * b, col(INK, 1));
        rect(x0 - b * 0.55, y0 - b * 0.55, pw + b * 1.1, ph + b * 1.1, col(THREAD, 1));
        rect(x0, y0, pw, ph, col(DEEP, 1));
        flush(r);
        fraa(pose, x0 + pw / 2, y0 + ph / 2 + sin(anim_t * 1.3) * unit * 0.5, 0.3, bob * 0.6, sideways && left);
    }

    const char *l1 = "", *l2 = "";
    if (k == 0) {
        l1 = "A FRAA IN ORBIT"; l2 = "HIS SPHERE HAS DRIFTED AWAY AHEAD OF HIM";
        if (t > 5.5) { l1 = "BOTH ARE FALLING ROUND ARBRE"; l2 = "FAST ENOUGH SIDEWAYS THAT THEY NEVER COME DOWN"; }
    } else if (k == 1) {
        l1 = "HE THRUSTS TOWARDS IT"; l2 = "THE OBVIOUS THING TO DO";
        if (t > BURN + 1.2) { l1 = "GOING FASTER LIFTS HIM HIGHER"; l2 = "AND A HIGHER ORBIT TAKES LONGER TO GO ROUND"; }
        if (t > 10) { l1 = "THE HARDER HE CHASES IT, THE FURTHER BEHIND HE FALLS"; l2 = "THE SPHERE GETS AWAY"; }
    } else {
        l1 = "SO HE DOES THE OPPOSITE"; l2 = "HE THRUSTS BACKWARDS, AWAY FROM IT";
        if (t > BURN + 1.2) { l1 = "SLOWING DOWN DROPS HIM LOWER"; l2 = "AND A LOWER ORBIT IS A QUICKER ONE: HE GAINS ON IT"; }
        if (t > MEET) { l1 = "ONE TURN LATER THEY MEET"; l2 = "HE THRUSTS FORWARD TO STAY. TO CATCH UP, SLOW DOWN"; }
        if (t > MEET + 8) { l1 = "FORWARD IS UP, UP IS BACK"; l2 = "BACK IS DOWN, DOWN IS FORWARD"; }
    }
    draw_caption(w, h, l1, l2);
    flush(r);
    double fade = fmin(ramp(t, 0, 0.8), 1 - ramp(t, LENGTH[k] - 0.8, LENGTH[k]));
    if (fade < 1) { rect(0, 0, w, h, col(BG, 1 - fade)); flush(r); }
}

typedef struct { SDL_Window *win; SDL_Renderer *ren; SDL_Texture *scene, *tex[NSPRITE]; int w, h; } View;

static void size_view(View *v)
{
    int ow, oh;
    SDL_RendererInfo info;
    SDL_GetRendererOutputSize(v->ren, &ow, &oh);
    if (ow < 1 || oh < 1) return;
    int ss = 2; /* drawn at twice the size and scaled down, for smooth edges */
    if (SDL_GetRendererInfo(v->ren, &info) == 0) {
        if (!(info.flags & SDL_RENDERER_TARGETTEXTURE) || (info.flags & SDL_RENDERER_SOFTWARE)) ss = 0;
        else if (info.max_texture_width && (ow * 2 > info.max_texture_width || oh * 2 > info.max_texture_height)) ss = 0;
    }
    if (v->w == ow * (ss ? ss : 1) && v->h == oh * (ss ? ss : 1) && (v->scene || !ss)) return;
    if (v->scene) SDL_DestroyTexture(v->scene);
    v->scene = NULL; v->w = ow; v->h = oh;
    if (!ss) return;
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
    v->scene = SDL_CreateTexture(v->ren, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, ow * ss, oh * ss);
    if (v->scene) { v->w = ow * ss; v->h = oh * ss; }
}
static void render(View *v)
{
    if (v->scene) SDL_SetRenderTarget(v->ren, v->scene);
    cur_tex = v->tex;
    draw_scene(v->ren, v->w, v->h);
    if (v->scene) { SDL_SetRenderTarget(v->ren, NULL); SDL_RenderCopy(v->ren, v->scene, NULL, NULL); }
    SDL_RenderPresent(v->ren);
}
static int make_view(View *v, int display)
{
    Uint32 flags = SDL_WINDOW_ALLOW_HIGHDPI;
    int x = SDL_WINDOWPOS_CENTERED_DISPLAY(display);
    if (opt_window) flags |= SDL_WINDOW_RESIZABLE;
    else flags |= SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_BORDERLESS;
    memset(v, 0, sizeof *v);
    v->win = SDL_CreateWindow("A Fraa in Orbit", x, x, 1200, 750, flags);
    if (!v->win) return 0;
    v->ren = SDL_CreateRenderer(v->win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_TARGETTEXTURE);
    if (!v->ren) v->ren = SDL_CreateRenderer(v->win, -1, SDL_RENDERER_SOFTWARE);
    if (!v->ren) return 0;
    SDL_SetRenderDrawBlendMode(v->ren, SDL_BLENDMODE_BLEND);
    make_textures(v->ren, v->tex);
    return 1;
}

static void start_show(void)
{
    seed_state = opt_seed ? opt_seed * 0x9E3779B97F4A7C15ULL : ((uint64_t)time(NULL) << 20) ^ SDL_GetPerformanceCounter();
    if (!seed_state) seed_state = 1;
    for (int i = 0; i < 6; i++) next_rand();
    anim_t = 0;
    new_round();
}

static int snapshot(void)
{
    SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormat(0, opt_w, opt_h, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_Renderer *r = surf ? SDL_CreateSoftwareRenderer(surf) : NULL;
    if (!r) { fprintf(stderr, "fraa-in-orbit: %s\n", SDL_GetError()); return 1; }
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_Texture *tex[NSPRITE];
    make_textures(r, tex);
    cur_tex = tex;
    start_show();
    for (double t = 0; t < opt_time; t += 1.0 / 60) update(1.0 / 60);
    draw_scene(r, opt_w, opt_h);
    SDL_RenderPresent(r);
    if (SDL_SaveBMP(surf, opt_snapshot) != 0) { fprintf(stderr, "fraa-in-orbit: %s\n", SDL_GetError()); return 1; }
    return 0;
}

static void usage(void)
{
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "A Fraa in Orbit",
        "A Fraa in Orbit screensaver\n\n"
        "  --window        run in a window (Esc or Q quits)\n"
        "  --speed X       animation speed, 1 = normal\n"
        "  --seed N        repeatable sequence\n"
        "  --snapshot FILE.bmp [--time SECONDS] [--size WxH]\n"
        "                  write a picture of the screensaver after SECONDS and exit\n", NULL);
}

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if ((a[0] == '/' || a[0] == '-') && (a[1] == 's' || a[1] == 'S') && !a[2]) continue;
        else if ((a[0] == '/' || a[0] == '-') && (a[1] == 'p' || a[1] == 'P') && (!a[2] || a[2] == ':')) return 0;
        else if ((a[0] == '/' || a[0] == '-') && (a[1] == 'c' || a[1] == 'C') && (!a[2] || a[2] == ':')) {
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "A Fraa in Orbit", "A Fraa in Orbit screensaver has no settings.", NULL);
            return 0;
        }
        else if (!strcmp(a, "--window")) opt_window = 1;
        else if (!strcmp(a, "--speed") && i + 1 < argc) opt_speed = SDL_atof(argv[++i]);
        else if (!strcmp(a, "--seed") && i + 1 < argc) opt_seed = SDL_strtoull(argv[++i], NULL, 10);
        else if (!strcmp(a, "--snapshot") && i + 1 < argc) opt_snapshot = argv[++i];
        else if (!strcmp(a, "--time") && i + 1 < argc) opt_time = SDL_atof(argv[++i]);
        else if (!strcmp(a, "--size") && i + 1 < argc) {
            const char *x = SDL_strchr(argv[++i], 'x');
            opt_w = SDL_atoi(argv[i]); opt_h = x ? SDL_atoi(x + 1) : 0;
            if (opt_w < 64 || opt_h < 64) { usage(); return 1; }
        }
        else { usage(); return 1; }
    }
    if (opt_speed < 0.05) opt_speed = 0.05;
    load_sprites();

    if (opt_snapshot) return snapshot();
    SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "1");
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { fprintf(stderr, "fraa-in-orbit: %s\n", SDL_GetError()); return 1; }

    View views[8];
    int nviews = 0, ndisp = opt_window ? 1 : SDL_GetNumVideoDisplays();
    if (ndisp < 1) ndisp = 1;
    for (int d = 0; d < ndisp && nviews < 8; d++)
        if (make_view(&views[nviews], d)) nviews++;
    if (!nviews) { fprintf(stderr, "fraa-in-orbit: %s\n", SDL_GetError()); return 1; }
    if (!opt_window) SDL_ShowCursor(SDL_DISABLE);
    start_show();

    Uint64 freqc = SDL_GetPerformanceFrequency(), last = SDL_GetPerformanceCounter(), start = last;
    double moved = 0;
    int running = 1;
    while (running) {
        SDL_Event e;
        double since = (double)(SDL_GetPerformanceCounter() - start) / freqc;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            else if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_CLOSE) running = 0;
            else if (e.type == SDL_KEYDOWN) {
                if (!opt_window || e.key.keysym.sym == SDLK_ESCAPE || e.key.keysym.sym == SDLK_q) running = 0;
            } else if (!opt_window && since > 1.0) {
                if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEWHEEL || e.type == SDL_FINGERDOWN) running = 0;
                else if (e.type == SDL_MOUSEMOTION) {
                    moved += abs(e.motion.xrel) + abs(e.motion.yrel);
                    if (moved > 24) running = 0;
                }
            }
        }
        Uint64 now = SDL_GetPerformanceCounter();
        double dt = (double)(now - last) / freqc;
        last = now;
        if (dt > 0.1) dt = 0.1;
        update(dt);
        for (int i = 0; i < nviews; i++) { size_view(&views[i]); render(&views[i]); }
        SDL_Delay(views[0].scene ? 1 : 12);
    }
    SDL_Quit();
    return 0;
}





