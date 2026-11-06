// Retained-mode renderer for the 360x360 round TFT.
//
// Every frame, a face describes the whole picture as a Scene: a short list of
// anti-aliased primitives (circles, rings, arcs, lines, text, rounded rects)
// painted in order. The renderer compares it with the previous frame's scene,
// marks the 8x8 px tiles whose pixels could have changed, merges those tiles
// into rectangles and repaints each rectangle from the new scene, row by row.
// Nothing is ever erased and redrawn, so nothing flickers or gets left behind,
// and only pixels that can change are sent over SPI.
//
// The ESP32-C3 has no FPU, so per-pixel maths is all integer: geometry is Q4
// fixed point (1/16 px) and coverage runs 0..256. Floats are only used when a
// primitive is added.
//
// The includer must define these before including this file:
//   static void sinkBegin();
//   static void sinkWindow(int x, int y, int w, int h);  // then pixels fill it row by row
//   static void sinkPixels(uint16_t *px, int n);
//   static void sinkEnd();
#pragma once
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "fonts.h"

// ---------------------------------------------------------------------------
// Scene
// ---------------------------------------------------------------------------
static const int SCR = 360;
static const float SCR_C = 180.0f;
static const int TILES = 45;  // 8 px tiles
#define MAX_PRIMS 160
#define MAX_GLYPHS 420

enum PrimType : uint8_t { PR_NONE, PR_CIRCLE, PR_RING, PR_ARC, PR_LINE, PR_TEXT, PR_RRECT };
enum Align : uint8_t { ALIGN_LEFT, ALIGN_CENTER, ALIGN_RIGHT };
enum FontId : uint8_t { F13, F16, F21, F26, F34, F40, D70 };
static const AAFont *const kFonts[] = {&font13, &font16, &font21, &font26, &font34, &font40, &digits70};

// Parameters per type (geometry in Q4):
//   CIRCLE  cx cy r
//   RING    cx cy rIn rOut
//   ARC     cx cy rIn rOut u0x u0y u1x u1y a0 a1   u = start/end direction (Q12), a = centidegrees
//   LINE    ax ay bx by ux uy len reach           reach = half width + 0.5 px; flags bit 0 = round caps
//   TEXT    firstGlyph glyphCount                 font = FontId
//   RRECT   cx cy halfW halfH radius
struct Prim {
    uint8_t type, alpha, font, flags;
    uint16_t color;
    int16_t bx0, by0, bx1, by1;  // pixel bounds [bx0, bx1) x [by0, by1), anti-aliasing included
    int32_t p[10];
};

struct GPlace { uint16_t glyph; int16_t x, y; };  // glyph index and top-left pixel

struct Scene {
    uint16_t bg, n, ng;
    Prim prims[MAX_PRIMS];
    GPlace glyphs[MAX_GLYPHS];
};

static uint16_t gSceneOverflow;  // primitives or glyphs dropped because a scene was full

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------
static inline int imin(int a, int b) { return a < b ? a : b; }
static inline int imax(int a, int b) { return a > b ? a : b; }
static inline int32_t iabs32(int32_t v) { return v < 0 ? -v : v; }
static inline int32_t q4(float v) { return (int32_t)lroundf(v * 16.0f); }
static inline int32_t cov256(int32_t v) { return v <= 0 ? 0 : v >= 256 ? 256 : v; }

static uint32_t isqrt32(uint32_t v) {
    uint32_t r = 0, bit = 1UL << 30;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; }
        else r >>= 1;
        bit >>= 2;
    }
    return r;
}

// Mix two RGB565 colours; a = 0 (all bg) .. 256 (all fg).
static inline uint16_t mix565(uint16_t fg, uint16_t bg, uint32_t a) {
    if (a >= 256) return fg;
    uint32_t ia = 256 - a;
    uint32_t r = (((fg >> 11) & 31) * a + ((bg >> 11) & 31) * ia) >> 8;
    uint32_t g = (((fg >> 5) & 63) * a + ((bg >> 5) & 63) * ia) >> 8;
    uint32_t b = ((fg & 31) * a + (bg & 31) * ia) >> 8;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

// ---------------------------------------------------------------------------
// Text layout
// ---------------------------------------------------------------------------

// Next glyph code from a UTF-8 string; the middle dot maps to slot 127.
static int nextCode(const char *&s) {
    uint8_t c = *s++;
    if (c < 0x80) return c;
    if (c == 0xC2 && (uint8_t)*s == 0xB7) { s++; return 127; }
    while (((uint8_t)*s & 0xC0) == 0x80) s++;
    return '?';
}

static const AAGlyph *glyphFor(const AAFont &f, int code) {
    if (code < f.first || code > f.last) code = (f.first <= '?' && '?' <= f.last) ? '?' : f.first;
    return &f.glyphs[code - f.first];
}

static int textWidth(const char *s, FontId font, int track = 0) {
    const AAFont &f = *kFonts[font];
    int w = 0;
    bool any = false;
    while (*s) {
        w += glyphFor(f, nextCode(s))->adv + track;
        any = true;
    }
    return any ? w - track : 0;
}

// ---------------------------------------------------------------------------
// Building a scene. Faces must add the same number of primitives in the same
// order every frame (a primitive that isn't shown is added with alpha 0), so
// unchanged primitives line up and cost nothing.
// ---------------------------------------------------------------------------
static Prim *primAdd(Scene &s, uint8_t type, uint16_t color, uint8_t alpha) {
    if (s.n >= MAX_PRIMS) {
        gSceneOverflow++;
        return nullptr;
    }
    Prim *p = &s.prims[s.n++];
    memset(p, 0, sizeof(Prim));
    if (alpha && type != PR_NONE) {
        p->type = type;
        p->color = color;
        p->alpha = alpha;
    }
    return p;
}

// Bounds from Q4 shape extents, padded for anti-aliasing and clipped to the screen.
static void setBounds(Prim *p, int32_t minX, int32_t minY, int32_t maxX, int32_t maxY) {
    int x0 = imax(0, (minX - 16) >> 4), y0 = imax(0, (minY - 16) >> 4);
    int x1 = imin(SCR, ((maxX + 16) >> 4) + 1), y1 = imin(SCR, ((maxY + 16) >> 4) + 1);
    if (x0 >= x1 || y0 >= y1) x0 = x1 = y0 = y1 = 0;
    p->bx0 = x0; p->by0 = y0; p->bx1 = x1; p->by1 = y1;
}

static void polarPt(float r, float deg, float &x, float &y) {
    float a = (deg - 90.0f) * (float)M_PI / 180.0f;
    x = SCR_C + r * cosf(a);
    y = SCR_C + r * sinf(a);
}

static void addCircle(Scene &s, float cx, float cy, float r, uint16_t col, uint8_t alpha = 255) {
    Prim *p = primAdd(s, r > 0 ? PR_CIRCLE : PR_NONE, col, alpha);
    if (!p || p->type == PR_NONE) return;
    int32_t x = q4(cx), y = q4(cy), rr = q4(r);
    p->p[0] = x; p->p[1] = y; p->p[2] = rr;
    setBounds(p, x - rr, y - rr, x + rr, y + rr);
}

static void addRing(Scene &s, float cx, float cy, float rIn, float rOut, uint16_t col, uint8_t alpha = 255) {
    if (rIn < 0) rIn = 0;  // so a shown ring has rOut > 0: the rasteriser needs that
    Prim *p = primAdd(s, rOut > rIn ? PR_RING : PR_NONE, col, alpha);
    if (!p || p->type == PR_NONE) return;
    int32_t x = q4(cx), y = q4(cy), ro = q4(rOut);
    p->p[0] = x; p->p[1] = y; p->p[2] = q4(rIn); p->p[3] = ro;
    setBounds(p, x - ro, y - ro, x + ro, y + ro);
}

static void arcDir(int32_t centideg, int32_t &ux, int32_t &uy) {
    float a = centideg * (float)M_PI / 18000.0f;
    ux = lroundf(sinf(a) * 4096.0f);
    uy = lroundf(-cosf(a) * 4096.0f);
}

// Ring segment from a0 clockwise to a1 degrees (0 = 12 o'clock), flat ends.
static void addArc(Scene &s, float cx, float cy, float rIn, float rOut, float a0, float a1, uint16_t col,
                   uint8_t alpha = 255) {
    float span = a1 - a0;
    if (span > 360.0f) span = 360.0f;
    if (rIn < 0) rIn = 0;  // so a shown arc has rOut > 0: the rasteriser needs that
    Prim *p = primAdd(s, (span >= 0.01f && rOut > rIn) ? PR_ARC : PR_NONE, col, alpha);
    if (!p || p->type == PR_NONE) return;
    float start = fmodf(a0, 360.0f);
    if (start < 0) start += 360.0f;
    int32_t c0 = lroundf(start * 100.0f), c1 = c0 + lroundf(span * 100.0f);
    int32_t x = q4(cx), y = q4(cy);
    p->p[0] = x; p->p[1] = y; p->p[2] = q4(rIn); p->p[3] = q4(rOut);
    arcDir(c0, p->p[4], p->p[5]);
    arcDir(c1, p->p[6], p->p[7]);
    p->p[8] = c0; p->p[9] = c1;

    // Bounds: both ends plus any compass extremes the arc sweeps through. Worked out from
    // the stored values, not the arguments: bounds clip what's drawn and primSame() only
    // compares p[], so equal p[] must mean equal bounds.
    float qx = x / 16.0f, qy = y / 16.0f, ri = p->p[2] / 16.0f, ro = p->p[3] / 16.0f;
    float qStart = c0 / 100.0f, qSpan = (c1 - c0) / 100.0f;
    float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
    auto grow = [&](float r, float deg) {
        float a = (deg - 90.0f) * (float)M_PI / 180.0f;
        float px = qx + r * cosf(a), py = qy + r * sinf(a);
        minX = px < minX ? px : minX; maxX = px > maxX ? px : maxX;
        minY = py < minY ? py : minY; maxY = py > maxY ? py : maxY;
    };
    grow(ri, qStart); grow(ro, qStart); grow(ri, qStart + qSpan); grow(ro, qStart + qSpan);
    for (int k = 0; k < 4; k++)
        if (fmodf(k * 90.0f - qStart + 720.0f, 360.0f) <= qSpan) grow(ro, k * 90.0f);
    setBounds(p, q4(minX), q4(minY), q4(maxX), q4(maxY));
}

// Arc centred on radius r with stroke width w and round ends: always 3 primitives.
static void addRoundArc(Scene &s, float r, float w, float a0, float a1, uint16_t col, uint8_t alpha = 255) {
    bool show = a1 - a0 >= 0.01f;
    addArc(s, SCR_C, SCR_C, r - w / 2, r + w / 2, a0, a1, col, show ? alpha : 0);
    float x, y;
    polarPt(r, a0, x, y);
    addCircle(s, x, y, w / 2, col, show ? alpha : 0);
    polarPt(r, a1, x, y);
    addCircle(s, x, y, w / 2, col, show ? alpha : 0);
}

static void addLine(Scene &s, float x0, float y0, float x1, float y1, float width, uint16_t col,
                    bool roundCaps = true, uint8_t alpha = 255) {
    Prim *p = primAdd(s, width > 0 ? PR_LINE : PR_NONE, col, alpha);
    if (!p || p->type == PR_NONE) return;
    float dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy);
    p->flags = roundCaps ? 1 : 0;
    p->p[0] = q4(x0); p->p[1] = q4(y0); p->p[2] = q4(x1); p->p[3] = q4(y1);
    if (len > 0.001f) {
        p->p[4] = lroundf(dx / len * 4096.0f);
        p->p[5] = lroundf(dy / len * 4096.0f);
    } else {
        p->p[4] = 4096;
    }
    p->p[6] = q4(len);
    p->p[7] = q4(width / 2) + 8;
    int32_t w = p->p[7];
    setBounds(p, (p->p[0] < p->p[2] ? p->p[0] : p->p[2]) - w, (p->p[1] < p->p[3] ? p->p[1] : p->p[3]) - w,
              (p->p[0] > p->p[2] ? p->p[0] : p->p[2]) + w, (p->p[1] > p->p[3] ? p->p[1] : p->p[3]) + w);
}

// Clock hand at deg (0 = 12) from `tail` px behind the centre out to `len`.
static void addHand(Scene &s, float deg, float len, float tail, float width, uint16_t col, uint8_t alpha = 255) {
    float x0, y0, x1, y1;
    polarPt(tail, deg + 180.0f, x0, y0);
    polarPt(len, deg, x1, y1);
    addLine(s, x0, y0, x1, y1, width, col, true, alpha);
}

// Text with its cap height centred on cy; x is the left edge, centre or right edge.
static void addText(Scene &s, const char *str, float x, float cy, FontId font, uint16_t col,
                    Align align = ALIGN_CENTER, int track = 0, uint8_t alpha = 255) {
    Prim *p = primAdd(s, PR_TEXT, col, alpha);
    if (!p || p->type == PR_NONE) return;
    const AAFont &f = *kFonts[font];
    p->font = font;
    p->flags = align;
    int tw = textWidth(str, font, track);
    int pen = lroundf(x) - (align == ALIGN_CENTER ? tw / 2 : align == ALIGN_RIGHT ? tw : 0);
    int baseline = lroundf(cy) + f.capH / 2;
    int x0 = SCR, y0 = SCR, x1 = 0, y1 = 0;
    p->p[0] = s.ng;
    for (const char *c = str; *c;) {
        const AAGlyph *g = glyphFor(f, nextCode(c));
        if (g->w && g->h) {
            if (s.ng >= MAX_GLYPHS) {
                gSceneOverflow++;
                break;
            }
            GPlace &gp = s.glyphs[s.ng++];
            gp.glyph = g - f.glyphs;
            gp.x = pen + g->xo;
            gp.y = baseline + g->yo;
            x0 = imin(x0, gp.x); y0 = imin(y0, gp.y);
            x1 = imax(x1, gp.x + g->w); y1 = imax(y1, gp.y + g->h);
        }
        pen += g->adv + track;
    }
    p->p[1] = s.ng - p->p[0];
    x0 = imax(0, x0); y0 = imax(0, y0); x1 = imin(SCR, x1); y1 = imin(SCR, y1);
    if (x0 >= x1 || y0 >= y1) x0 = x1 = y0 = y1 = 0;
    p->bx0 = x0; p->by0 = y0; p->bx1 = x1; p->by1 = y1;
}

static void addRRect(Scene &s, float cx, float cy, float w, float h, float radius, uint16_t col, uint8_t alpha = 255) {
    Prim *p = primAdd(s, (w > 0 && h > 0) ? PR_RRECT : PR_NONE, col, alpha);
    if (!p || p->type == PR_NONE) return;
    float r = radius;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    int32_t x = q4(cx), y = q4(cy), hx = q4(w / 2), hy = q4(h / 2);
    p->p[0] = x; p->p[1] = y; p->p[2] = hx; p->p[3] = hy; p->p[4] = q4(r);
    setBounds(p, x - hx, y - hy, x + hx, y + hy);
}

// ---------------------------------------------------------------------------
// Rasterising. Each primitive paints one row of pixels into gRow over what's
// already there. Coverage runs 0..256.
// ---------------------------------------------------------------------------
struct Prep { int32_t v[7]; };  // per-primitive constants, computed once per flush
static Prep gPrep[MAX_PRIMS];
static uint16_t gRow[SCR + 2];  // +2: some SPI drivers read pixels in pairs
static const uint16_t kAlpha15[16] = {0, 17, 34, 51, 68, 85, 102, 119, 137, 154, 171, 188, 205, 222, 239, 256};

// A disc of radius r: fully covered inside (r - 0.5 px), empty outside (r + 0.5 px),
// linear in distance-squared between (a close match to linear in distance there).
static void circleConsts(int32_t r, int32_t &out2, int32_t &in2, int32_t &k) {
    int32_t ro = r + 8, ri = r - 8;
    out2 = ro * ro;
    in2 = ri > 0 ? ri * ri : 0;
    k = (int32_t)((256LL << 16) / (out2 - in2));
}

static inline int32_t circleCov(int32_t d2, int32_t out2, int32_t in2, int32_t k) {
    if (d2 >= out2) return 0;
    if (d2 <= in2) return 256;
    return ((out2 - d2) * k) >> 16;
}

static void prepPrim(const Prim &p, Prep &pr) {
    switch (p.type) {
        case PR_CIRCLE:
            circleConsts(p.p[2], pr.v[0], pr.v[1], pr.v[2]);
            break;
        case PR_RING:
        case PR_ARC: {
            circleConsts(p.p[3], pr.v[0], pr.v[1], pr.v[2]);
            circleConsts(p.p[2], pr.v[3], pr.v[4], pr.v[5]);
            int32_t span = p.p[9] - p.p[8];
            pr.v[6] = p.type == PR_RING || span >= 35999 ? 2 : span > 18000 ? 1 : 0;  // 2 = full, 1 = wide
            break;
        }
        case PR_LINE:
            pr.v[0] = p.p[7] * p.p[7];
            break;
        default:
            break;
    }
}

static inline void plot(int x, int32_t cov, uint16_t col, int32_t a256) {
    int32_t a = a256 == 256 ? cov : (cov * a256) >> 8;
    if (a >= 256) gRow[x] = col;
    else if (a > 0) gRow[x] = mix565(col, gRow[x], a);
}

static void rowCircle(const Prim &p, const Prep &pr, int y, int x0, int x1, int32_t a256) {
    int32_t cx = p.p[0], dy = (y << 4) + 8 - p.p[1], dy2 = dy * dy;
    if (dy2 >= pr.v[0]) return;
    int32_t xo = isqrt32(pr.v[0] - dy2);
    int xs = imax(x0, (cx - xo) >> 4), xe = imin(x1, ((cx + xo) >> 4) + 1);
    int ss = xe, se = xe;  // fully covered middle of the row
    if (dy2 < pr.v[1]) {
        int32_t xi = isqrt32(pr.v[1] - dy2);
        ss = imax(xs, ((cx - xi) >> 4) + 1);
        se = imin(xe, (cx + xi) >> 4);
        if (se <= ss) ss = se = xe;
    }
    for (int x = xs; x < xe;) {
        if (x == ss) {
            if (a256 == 256) for (; x < se; x++) gRow[x] = p.color;
            else for (; x < se; x++) gRow[x] = mix565(p.color, gRow[x], a256);
            continue;
        }
        int32_t dx = (x << 4) + 8 - cx;
        plot(x, circleCov(dx * dx + dy2, pr.v[0], pr.v[1], pr.v[2]), p.color, a256);
        x++;
    }
}

static void rowRing(const Prim &p, const Prep &pr, int y, int x0, int x1, int32_t a256) {
    int32_t cx = p.p[0], dy = (y << 4) + 8 - p.p[1], dy2 = dy * dy;
    if (dy2 >= pr.v[0]) return;
    int32_t xo = isqrt32(pr.v[0] - dy2);
    int xs = imax(x0, (cx - xo) >> 4), xe = imin(x1, ((cx + xo) >> 4) + 1);
    int hs = xe, he = xe;  // the hole: pixels fully inside the inner edge
    if (dy2 < pr.v[4]) {
        int32_t xi = isqrt32(pr.v[4] - dy2);
        hs = imax(xs, ((cx - xi) >> 4) + 1);
        he = imin(xe, (cx + xi) >> 4);
        if (he <= hs) hs = he = xe;
    }
    bool angular = pr.v[6] != 2, wide = pr.v[6] == 1;
    int32_t u0x = p.p[4], u0y = p.p[5], u1x = p.p[6], u1y = p.p[7];
    for (int x = xs; x < xe; x++) {
        if (x == hs) {
            x = he - 1;
            continue;
        }
        int32_t dx = (x << 4) + 8 - cx, d2 = dx * dx + dy2;
        int32_t cov = circleCov(d2, pr.v[0], pr.v[1], pr.v[2]);
        if (!cov) continue;
        cov = (cov * (256 - circleCov(d2, pr.v[3], pr.v[4], pr.v[5]))) >> 8;
        if (angular && cov) {
            int32_t c0 = cov256(((u0x * dy - u0y * dx) >> 12) * 16 + 128);  // inside past the start edge
            int32_t c1 = cov256(((u1y * dx - u1x * dy) >> 12) * 16 + 128);  // inside before the end edge
            cov = (cov * (wide ? imax(c0, c1) : imin(c0, c1))) >> 8;
        }
        if (cov) plot(x, cov, p.color, a256);
    }
}

static void rowLine(const Prim &p, const Prep &pr, int y, int x0, int x1, int32_t a256) {
    int32_t ax = p.p[0], ay = p.p[1], ux = p.p[4], uy = p.p[5], len = p.p[6], reach = p.p[7];
    int32_t py = (y << 4) + 8, vy = py - ay;
    int xs = x0, xe = x1;
    int32_t auy = iabs32(uy);
    if (auy >= 64) {  // narrow the row to the band around the line
        int32_t xc = ax + (vy * ux) / uy;
        int32_t half = (reach << 12) / auy + 16;
        xs = imax(xs, (xc - half) >> 4);
        xe = imin(xe, ((xc + half) >> 4) + 1);
    }
    bool roundCaps = p.flags & 1;
    for (int x = xs; x < xe; x++) {
        int32_t vx = (x << 4) + 8 - ax;
        int32_t an = iabs32((vx * uy - vy * ux) >> 12);
        if (an >= reach) continue;
        int32_t t = (vx * ux + vy * uy) >> 12, cov;
        if (roundCaps) {
            int32_t dist = an;
            if (t < 0 || t > len) {
                int32_t ex = t < 0 ? vx : (x << 4) + 8 - p.p[2], ey = t < 0 ? vy : py - p.p[3];
                int32_t d2 = ex * ex + ey * ey;
                if (d2 >= pr.v[0]) continue;
                dist = isqrt32(d2);
            }
            cov = cov256((reach - dist) << 4);
        } else {
            cov = imin(cov256((reach - an) << 4), imin(cov256((t + 8) << 4), cov256((len + 8 - t) << 4)));
        }
        if (cov) plot(x, cov, p.color, a256);
    }
}

static void rowText(const Scene &s, const Prim &p, int y, int x0, int x1, int32_t a256) {
    const AAFont &f = *kFonts[p.font];
    for (int i = p.p[0], end = p.p[0] + p.p[1]; i < end; i++) {
        const GPlace &gp = s.glyphs[i];
        const AAGlyph &g = f.glyphs[gp.glyph];
        int r = y - gp.y;
        if (r < 0 || r >= g.h) continue;
        int c0 = imax(0, x0 - gp.x), c1 = imin(g.w, x1 - gp.x);
        uint32_t base = (uint32_t)r * g.w;
        for (int c = c0; c < c1; c++) {
            uint32_t k = base + c;
            uint8_t b = f.bmp[g.offset + (k >> 1)];
            uint8_t a = (k & 1) ? (b & 0x0F) : (b >> 4);
            if (a) plot(gp.x + c, kAlpha15[a], p.color, a256);
        }
    }
}

static void rowRRect(const Prim &p, int y, int x0, int x1, int32_t a256) {
    int32_t cx = p.p[0], r = p.p[4];
    int32_t qy = iabs32((y << 4) + 8 - p.p[1]) - (p.p[3] - r);
    int32_t ex = p.p[2] - r;
    for (int x = x0; x < x1; x++) {
        int32_t qx = iabs32((x << 4) + 8 - cx) - ex, dist;
        if (qx > 0 && qy > 0) dist = (int32_t)isqrt32(qx * qx + qy * qy) - r;
        else dist = (qx > qy ? qx : qy) - r;
        int32_t cov = cov256((8 - dist) << 4);
        if (cov) plot(x, cov, p.color, a256);
    }
}

// ---------------------------------------------------------------------------
// Dirty tiles
// ---------------------------------------------------------------------------
static uint64_t gDirty[TILES];
static uint64_t gMask[TILES];  // tiles that touch the round visible area
static bool gMaskReady = false;

// A tile's pixel centres lie within 5 px of its centre; add 0.5 px of anti-aliasing and round up.
static const int32_t TILE_REACH = 96;

static void initMask() {
    int32_t lim = 180 * 16 + TILE_REACH;
    for (int ty = 0; ty < TILES; ty++) {
        gMask[ty] = 0;
        for (int tx = 0; tx < TILES; tx++) {
            int32_t dx = ((tx << 3) + 4 - 180) << 4, dy = ((ty << 3) + 4 - 180) << 4;
            if (dx * dx + dy * dy <= lim * lim) gMask[ty] |= 1ULL << tx;
        }
    }
    gMaskReady = true;
}

static void markRectPx(int x0, int y0, int x1, int y1) {
    x0 = imax(0, x0); y0 = imax(0, y0); x1 = imin(SCR, x1); y1 = imin(SCR, y1);
    if (x0 >= x1 || y0 >= y1) return;
    int tx0 = x0 >> 3, tx1 = (x1 + 7) >> 3;
    uint64_t bits = (tx1 >= 64 ? ~0ULL : (1ULL << tx1) - 1) & ~((1ULL << tx0) - 1);
    for (int ty = y0 >> 3, ty1 = (y1 + 7) >> 3; ty < ty1; ty++) gDirty[ty] |= bits;
}

static bool tileHits(const Prim &p, int32_t tcx, int32_t tcy) {
    switch (p.type) {
        case PR_CIRCLE: {
            int32_t dx = tcx - p.p[0], dy = tcy - p.p[1], lim = p.p[2] + 8 + TILE_REACH;
            return dx * dx + dy * dy <= lim * lim;
        }
        case PR_RING:
        case PR_ARC: {
            int32_t dx = tcx - p.p[0], dy = tcy - p.p[1], d2 = dx * dx + dy * dy;
            int32_t hi = p.p[3] + 8 + TILE_REACH, lo = p.p[2] - 8 - TILE_REACH;
            if (d2 > hi * hi || (lo > 0 && d2 < lo * lo)) return false;
            if (p.type == PR_RING) return true;
            int32_t span = p.p[9] - p.p[8];
            if (span >= 35999) return true;
            int32_t n0 = (p.p[4] * dy - p.p[5] * dx) >> 12, n1 = (p.p[7] * dx - p.p[6] * dy) >> 12;
            int32_t m = -8 - TILE_REACH;
            return span > 18000 ? (n0 >= m || n1 >= m) : (n0 >= m && n1 >= m);
        }
        case PR_LINE: {
            int32_t vx = tcx - p.p[0], vy = tcy - p.p[1], m = p.p[7] + TILE_REACH;
            if (iabs32((vx * p.p[5] - vy * p.p[4]) >> 12) > m) return false;
            int32_t t = (vx * p.p[4] + vy * p.p[5]) >> 12;
            return t >= -m && t <= p.p[6] + m;
        }
        default:
            return true;
    }
}

static void markGlyph(const Scene &s, const Prim &p, const GPlace &gp) {
    const AAGlyph &g = kFonts[p.font]->glyphs[gp.glyph];
    markRectPx(gp.x, gp.y, gp.x + g.w, gp.y + g.h);
}

static void markPrim(const Scene &s, const Prim &p) {
    if (p.type == PR_NONE || p.bx0 >= p.bx1) return;
    if (p.type == PR_TEXT) {
        for (int i = 0; i < p.p[1]; i++) markGlyph(s, p, s.glyphs[p.p[0] + i]);
        return;
    }
    if (p.type == PR_RRECT) {
        markRectPx(p.bx0, p.by0, p.bx1, p.by1);
        return;
    }
    int tx0 = p.bx0 >> 3, tx1 = (p.bx1 + 7) >> 3, ty0 = p.by0 >> 3, ty1 = (p.by1 + 7) >> 3;
    for (int ty = ty0; ty < ty1; ty++) {
        uint64_t bits = 0;
        int32_t tcy = ((ty << 3) + 4) << 4;
        for (int tx = tx0; tx < tx1; tx++)
            if (tileHits(p, ((tx << 3) + 4) << 4, tcy)) bits |= 1ULL << tx;
        gDirty[ty] |= bits;
    }
}

static void ringBounds(Prim &p) {
    int32_t r = p.p[3];
    setBounds(&p, p.p[0] - r, p.p[1] - r, p.p[0] + r, p.p[1] + r);
}

// Only what an arc's moving edge can change, the other edge staying put. Each edge is
// a half-plane through the centre, so moving one changes its coverage in the slice
// between the old and new angle and in the opposite slice too. An arc's coverage
// combines the two edges, min (<= 180 deg) or max (wider), so a change shows where the
// fixed edge lets it through. That fixes which of the two slices can show: all of the
// real one, and of the opposite one the part along the fixed edge, reached when the
// span is near 180 deg (or, in a pie, around the centre). (fx, fy) is the fixed edge's
// inward normal in Q12.
static void markArcSlice(const Prim &base, int32_t c0, int32_t c1, int32_t fx, int32_t fy, bool wide) {
    Prim t = base;
    ringBounds(t);
    if (t.bx0 >= t.bx1) return;
    int32_t ax, ay, bx, by;
    arcDir(c0, ax, ay);
    arcDir(c1, bx, by);
    const int32_t m = 8 + TILE_REACH, hi = t.p[3] + m, lo = t.p[2] - m;
    int tx0 = t.bx0 >> 3, tx1 = (t.bx1 + 7) >> 3, ty0 = t.by0 >> 3, ty1 = (t.by1 + 7) >> 3;
    for (int ty = ty0; ty < ty1; ty++) {
        uint64_t bits = 0;
        int32_t dy = (((ty << 3) + 4) << 4) - t.p[1];
        for (int tx = tx0; tx < tx1; tx++) {
            int32_t dx = (((tx << 3) + 4) << 4) - t.p[0], d2 = dx * dx + dy * dy;
            if (d2 > hi * hi || (lo > 0 && d2 < lo * lo)) continue;
            int32_t f = (fx * dx + fy * dy) >> 12;
            if (wide ? f > m : f < -m) continue;  // the fixed edge decides here on its own
            int32_t sa = (ay * dx - ax * dy) >> 12, sb = (by * dx - bx * dy) >> 12;  // inside before c0, c1
            if ((sa <= m && sb >= -m) || (sa >= -m && sb <= m)) bits |= 1ULL << tx;
        }
        gDirty[ty] |= bits;
    }
}

// Only the band a ring's edge moved through.
static void markRingBand(const Scene &s, const Prim &base, int32_t r0, int32_t r1) {
    Prim t = base;
    t.p[2] = r0;
    t.p[3] = r1;
    ringBounds(t);
    markPrim(s, t);
}

static bool primSame(const Scene &sa, const Prim &a, const Scene &sb, const Prim &b) {
    if (a.type != b.type || a.alpha != b.alpha || a.font != b.font || a.flags != b.flags || a.color != b.color) return false;
    if (a.type != PR_TEXT) return memcmp(a.p, b.p, sizeof(a.p)) == 0;
    return a.p[1] == b.p[1] && memcmp(&sa.glyphs[a.p[0]], &sb.glyphs[b.p[0]], a.p[1] * sizeof(GPlace)) == 0;
}

static void markChanges(const Scene &o, const Scene &n) {
    int count = imax(o.n, n.n);
    for (int i = 0; i < count; i++) {
        if (i >= o.n) { markPrim(n, n.prims[i]); continue; }
        if (i >= n.n) { markPrim(o, o.prims[i]); continue; }
        const Prim &a = o.prims[i], &b = n.prims[i];
        if (primSame(o, a, n, b)) continue;
        bool sameLook = a.type == b.type && a.color == b.color && a.alpha == b.alpha && a.font == b.font && a.flags == b.flags;
        if (sameLook && a.type == PR_ARC && memcmp(a.p, b.p, 4 * sizeof(int32_t)) == 0) {
            int32_t sa = a.p[9] - a.p[8], sb = b.p[9] - b.p[8];
            bool sameKind = (sa > 18000) == (sb > 18000) && (sa >= 35999) == (sb >= 35999);
            if (sameKind && a.p[8] == b.p[8]) {
                markArcSlice(a, imin(a.p[9], b.p[9]), imax(a.p[9], b.p[9]), -a.p[5], a.p[4], sa > 18000);
                continue;
            }
            if (sameKind && a.p[9] == b.p[9]) {
                markArcSlice(a, imin(a.p[8], b.p[8]), imax(a.p[8], b.p[8]), a.p[7], -a.p[6], sa > 18000);
                continue;
            }
        }
        if (sameLook && a.type == PR_RING && a.p[0] == b.p[0] && a.p[1] == b.p[1]) {
            if (a.p[2] != b.p[2]) markRingBand(n, a, imin(a.p[2], b.p[2]), imax(a.p[2], b.p[2]));
            if (a.p[3] != b.p[3]) markRingBand(n, a, imin(a.p[3], b.p[3]), imax(a.p[3], b.p[3]));
            continue;
        }
        if (sameLook && a.type == PR_TEXT && a.p[1] == b.p[1]) {
            for (int k = 0; k < a.p[1]; k++) {
                const GPlace &ga = o.glyphs[a.p[0] + k], &gb = n.glyphs[b.p[0] + k];
                if (memcmp(&ga, &gb, sizeof(GPlace))) {
                    markGlyph(o, a, ga);
                    markGlyph(n, b, gb);
                }
            }
            continue;
        }
        markPrim(o, a);
        markPrim(n, b);
    }
}

// ---------------------------------------------------------------------------
// Flushing dirty tiles to the panel
// ---------------------------------------------------------------------------
struct RenderStats { uint32_t pixels, windows, frames; };
static RenderStats gStats;

static void renderRect(const Scene &s, int x, int y, int w, int h) {
    static uint8_t active[MAX_PRIMS];
    int na = 0;
    for (int i = 0; i < s.n; i++) {
        const Prim &p = s.prims[i];
        if (p.type != PR_NONE && p.bx0 < x + w && p.bx1 > x && p.by0 < y + h && p.by1 > y) active[na++] = i;
    }
    sinkWindow(x, y, w, h);
    gStats.windows++;
    gStats.pixels += w * h;
    for (int row = y; row < y + h; row++) {
        for (int i = x; i < x + w; i++) gRow[i] = s.bg;
        for (int j = 0; j < na; j++) {
            const Prim &p = s.prims[active[j]];
            if (row < p.by0 || row >= p.by1) continue;
            int x0 = imax(x, p.bx0), x1 = imin(x + w, p.bx1);
            int32_t a256 = p.alpha + (p.alpha >> 7);
            switch (p.type) {
                case PR_CIRCLE: rowCircle(p, gPrep[active[j]], row, x0, x1, a256); break;
                case PR_RING:
                case PR_ARC:    rowRing(p, gPrep[active[j]], row, x0, x1, a256); break;
                case PR_LINE:   rowLine(p, gPrep[active[j]], row, x0, x1, a256); break;
                case PR_TEXT:   rowText(s, p, row, x0, x1, a256); break;
                case PR_RRECT:  rowRRect(p, row, x0, x1, a256); break;
            }
        }
        sinkPixels(gRow + x, w);
    }
}

// Merges dirty tiles into rectangles (runs across, identical runs down) and paints them.
static void flushDirty(const Scene &s) {
    bool any = false;
    for (int ty = 0; ty < TILES; ty++) {
        gDirty[ty] &= gMask[ty];
        if (gDirty[ty]) any = true;
    }
    if (!any) return;
    for (int i = 0; i < s.n; i++) prepPrim(s.prims[i], gPrep[i]);

    struct Span { uint8_t x0, x1, y0; bool seen; };
    Span open[24], runs[24];
    int nOpen = 0;
    sinkBegin();
    for (int ty = 0; ty <= TILES; ty++) {
        uint64_t bits = ty < TILES ? gDirty[ty] : 0;
        bits |= (bits >> 1) & (bits << 1);  // bridge one-tile gaps: cheaper than a new window
        int nRuns = 0;
        for (int tx = 0; tx < TILES && nRuns < 24;) {
            if (!(bits >> tx & 1)) { tx++; continue; }
            int start = tx;
            while (tx < TILES && (bits >> tx & 1)) tx++;
            runs[nRuns++] = {(uint8_t)start, (uint8_t)tx, (uint8_t)ty, false};
        }
        for (int i = 0; i < nOpen; i++) open[i].seen = false;
        for (int r = 0; r < nRuns; r++) {
            for (int i = 0; i < nOpen; i++)
                if (!open[i].seen && open[i].x0 == runs[r].x0 && open[i].x1 == runs[r].x1) {
                    open[i].seen = runs[r].seen = true;
                    break;
                }
        }
        int kept = 0;
        for (int i = 0; i < nOpen; i++) {
            if (open[i].seen) { open[kept++] = open[i]; continue; }
            int x = open[i].x0 << 3, y = open[i].y0 << 3;
            renderRect(s, x, y, imin(SCR, open[i].x1 << 3) - x, imin(SCR, ty << 3) - y);
        }
        nOpen = kept;
        for (int r = 0; r < nRuns && nOpen < 24; r++)
            if (!runs[r].seen) open[nOpen++] = runs[r];
    }
    sinkEnd();
    memset(gDirty, 0, sizeof(gDirty));
}

// ---------------------------------------------------------------------------
// Frames
// ---------------------------------------------------------------------------
struct RendererState {
    Scene scenes[2];
    uint8_t cur;  // index of the scene that's on screen
    bool haveCur, forceFull, staticReady;
    uint32_t staticKey;
    uint16_t staticN, staticNG;
};
static RendererState gR;

// Repaint everything next frame, and rebuild static layers.
static void renderInvalidate() {
    gR.forceFull = true;
    gR.staticReady = false;
}

// Starts a frame. If the face's static layer (everything added before
// staticDone) is still valid for staticKey, it's already in the scene and
// needStatic is false; otherwise the face must add it and call staticDone.
static Scene &frameBegin(uint16_t bg, uint32_t staticKey, bool &needStatic) {
    Scene &next = gR.scenes[1 - gR.cur];
    next.bg = bg;
    if (gR.haveCur && gR.staticReady && staticKey == gR.staticKey) {
        const Scene &cur = gR.scenes[gR.cur];
        memcpy(next.prims, cur.prims, gR.staticN * sizeof(Prim));
        memcpy(next.glyphs, cur.glyphs, gR.staticNG * sizeof(GPlace));
        next.n = gR.staticN;
        next.ng = gR.staticNG;
        needStatic = false;
    } else {
        next.n = next.ng = 0;
        gR.staticKey = staticKey;
        gR.staticReady = false;
        needStatic = true;
    }
    return next;
}

static void staticDone(const Scene &s) {
    gR.staticN = s.n;
    gR.staticNG = s.ng;
    gR.staticReady = true;
}

static void frameEnd() {
    if (!gMaskReady) initMask();
    const Scene &next = gR.scenes[1 - gR.cur];
    if (!gR.haveCur || gR.forceFull || gR.scenes[gR.cur].bg != next.bg) {
        memcpy(gDirty, gMask, sizeof(gDirty));
    } else {
        markChanges(gR.scenes[gR.cur], next);
    }
    flushDirty(next);
    gR.cur = 1 - gR.cur;
    gR.haveCur = true;
    gR.forceFull = false;
    gStats.frames++;
}
