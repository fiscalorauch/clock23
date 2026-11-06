// Adversarial stress test for the dirty-tile diffing in HaloClock/render.h,
// independent of any clock face.
//
// It drives frameBegin / staticDone / frameEnd with synthetic scenes and, on
// EVERY frame:
//   * renders the same scene from scratch in one 360x360 window (the reference);
//   * checks every pixel that changed between the previous and current reference
//     lies in a tile markChanges() marked ("unmarked" = a missed change);
//   * compares the incrementally updated panel with the reference ("mismatch");
//   * checks each window got exactly w*h pixels, lies on the screen, windows never
//     overlap, and the painted tiles equal the (gap-bridged) marked tiles;
//   * checks the scene built with a reused static layer equals a fresh build.
// Every few frames it also repeats sim.cpp's checkAgainstFull() path (reset gR,
// forceFull, rebuild) and repaints the scene through a random window tiling, which
// proves the rasterisers don't depend on how the screen is cut into windows.
//
// Build and run from the "OLED Code" folder:
//   clang++ -std=c++17 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -o tools/sim/out/stress tools/sim/stress.cpp
//   tools/sim/out/stress                   targeted cases, then 150 random sequences
//   tools/sim/out/stress cases [filter]    targeted cases only (name filter optional)
//   tools/sim/out/stress random N [seed]   N random sequences starting at seed (each in a child process,
//                                          so a sanitizer abort is reported with its seed and the run goes on)
//   tools/sim/out/stress probes            huge/odd geometry, each in a child process (UBSan aborts)
//   tools/sim/out/stress seams             arc coverage vs a 16x16 supersampled exact reference
//
// Negative left shifts: rowLine/rowRRect do `(negative) << 4`, which is UB in C++17
// (UBSan traps it) but defined since C++20, and the ESP32-C3 core builds with
// -std=gnu++2b. So by default shift-base is not instrumented inside render.h;
// add -DSTRICT_SHIFT to trap it. Everything else (signed overflow, bounds, ...) stays strict.
// -DSTRESS_RENDER_H='"/path/render.h"' tests another copy of the renderer (add -I HaloClock).
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cmath>
#include <functional>
#include <random>
#include <string>
#include <vector>

#include "host_platform.h"

// ---------------------------------------------------------------------------
// Checked sinks: wrap host_platform.h's panel so every window is audited.
// ---------------------------------------------------------------------------
static const int W = 360, NPX = 360 * 360;
static inline int imax_(int a, int b) { return a > b ? a : b; }

struct FrameAcc {
    long windows, px, badRect, badCount, overlapPx, misaligned;
    uint64_t tiles[45];  // tiles touched by windows
};
static FrameAcc gAcc;
static bool gAccount = false;  // frame accounting on (off for reference renders)
static bool gWinOpen = false;
static long gWinExpect = 0;
static long gBadRectTotal = 0, gBadCountTotal = 0;
static uint8_t gPaint[NPX];

static void accCloseWindow() {
    if (gWinOpen && gWinPos != gWinExpect) { gAcc.badCount++; gBadCountTotal++; }
    gWinOpen = false;
}
static void chkSinkBegin() { sinkBegin(); }
static void chkSinkEnd() { accCloseWindow(); sinkEnd(); }
static void chkSinkWindow(int x, int y, int w, int h) {
    accCloseWindow();
    if (w <= 0 || h <= 0 || x < 0 || y < 0 || x + w > W || y + h > W) { gAcc.badRect++; gBadRectTotal++; }
    if (gAccount) {
        gAcc.windows++;
        gAcc.px += (long)w * h;
        if (x % 8 || y % 8 || ((x + w) % 8 && x + w != W) || ((y + h) % 8 && y + h != W)) gAcc.misaligned++;
        for (int ty = imax_(0, y >> 3); ty < ((y + h + 7) >> 3) && ty < 45; ty++)
            for (int tx = imax_(0, x >> 3); tx < ((x + w + 7) >> 3) && tx < 45; tx++) gAcc.tiles[ty] |= 1ULL << tx;
    }
    sinkWindow(x, y, w, h);
    gWinOpen = true;
    gWinExpect = (long)w * h;
}
static void chkSinkPixels(uint16_t *px, int n) {
    if (gAccount && gWinW > 0) {
        for (int i = 0; i < n; i++) {
            long pos = gWinPos + i;
            if (pos >= gWinExpect) break;
            int x = gWinX + (int)(pos % gWinW), y = gWinY + (int)(pos / gWinW);
            if (x >= 0 && x < W && y >= 0 && y < W && gPaint[y * W + x]++) gAcc.overlapPx++;
        }
    }
    sinkPixels(px, n);
}
#define sinkBegin chkSinkBegin
#define sinkEnd chkSinkEnd
#define sinkWindow chkSinkWindow
#define sinkPixels chkSinkPixels

#ifndef STRICT_SHIFT
#pragma clang attribute push(__attribute__((no_sanitize("shift-base"))), apply_to = function)
#endif
#ifdef STRESS_RENDER_H
#include STRESS_RENDER_H
#else
#include "../../HaloClock/render.h"
#endif
#ifndef STRICT_SHIFT
#pragma clang attribute pop
#endif
#undef sinkBegin
#undef sinkEnd
#undef sinkWindow
#undef sinkPixels

static const char *kOutDir = "tools/sim/out";

// ---------------------------------------------------------------------------
// Scene descriptions
// ---------------------------------------------------------------------------
enum OpKind : uint8_t { K_CIRCLE, K_RING, K_ARC, K_ROUNDARC, K_LINE, K_HAND, K_TEXT, K_RRECT, K_NKINDS };

struct Op {
    uint8_t kind = K_CIRCLE;
    float f[6] = {0, 0, 0, 0, 0, 0};
    uint16_t col = 0xFFFF;
    uint8_t alpha = 255;
    bool round = true;
    uint8_t font = F16, align = ALIGN_CENTER;
    int track = 0;
    std::string text;
};

struct Frame {
    uint16_t bg = 0;
    uint32_t key = 1;
    bool invalidate = false;  // renderInvalidate() before this frame
    std::vector<Op> stat, dyn;
};

static Op C(float cx, float cy, float r, uint16_t col = 0xFFFF, uint8_t a = 255) {
    Op o; o.kind = K_CIRCLE; o.f[0] = cx; o.f[1] = cy; o.f[2] = r; o.col = col; o.alpha = a; return o;
}
static Op RG(float cx, float cy, float ri, float ro, uint16_t col = 0xFFFF, uint8_t a = 255) {
    Op o; o.kind = K_RING; o.f[0] = cx; o.f[1] = cy; o.f[2] = ri; o.f[3] = ro; o.col = col; o.alpha = a; return o;
}
static Op AR(float cx, float cy, float ri, float ro, float a0, float a1, uint16_t col = 0xFFFF, uint8_t a = 255) {
    Op o; o.kind = K_ARC; o.f[0] = cx; o.f[1] = cy; o.f[2] = ri; o.f[3] = ro; o.f[4] = a0; o.f[5] = a1;
    o.col = col; o.alpha = a; return o;
}
static Op RA(float r, float w, float a0, float a1, uint16_t col = 0xFFFF, uint8_t a = 255) {
    Op o; o.kind = K_ROUNDARC; o.f[0] = r; o.f[1] = w; o.f[2] = a0; o.f[3] = a1; o.col = col; o.alpha = a; return o;
}
static Op LN(float x0, float y0, float x1, float y1, float w, bool round = true, uint16_t col = 0xFFFF, uint8_t a = 255) {
    Op o; o.kind = K_LINE; o.f[0] = x0; o.f[1] = y0; o.f[2] = x1; o.f[3] = y1; o.f[4] = w; o.round = round;
    o.col = col; o.alpha = a; return o;
}
static Op HD(float deg, float len, float tail, float w, uint16_t col = 0xFFFF, uint8_t a = 255) {
    Op o; o.kind = K_HAND; o.f[0] = deg; o.f[1] = len; o.f[2] = tail; o.f[3] = w; o.col = col; o.alpha = a; return o;
}
static Op TX(const std::string &s, float x, float cy, int font, int align = ALIGN_CENTER, int track = 0,
             uint16_t col = 0xFFFF, uint8_t a = 255) {
    Op o; o.kind = K_TEXT; o.text = s; o.f[0] = x; o.f[1] = cy; o.font = font; o.align = align; o.track = track;
    o.col = col; o.alpha = a; return o;
}
static Op RR(float cx, float cy, float w, float h, float r, uint16_t col = 0xFFFF, uint8_t a = 255) {
    Op o; o.kind = K_RRECT; o.f[0] = cx; o.f[1] = cy; o.f[2] = w; o.f[3] = h; o.f[4] = r; o.col = col; o.alpha = a; return o;
}

static void addOp(Scene &s, const Op &o) {
    const float *f = o.f;
    switch (o.kind) {
        case K_CIRCLE: addCircle(s, f[0], f[1], f[2], o.col, o.alpha); break;
        case K_RING: addRing(s, f[0], f[1], f[2], f[3], o.col, o.alpha); break;
        case K_ARC: addArc(s, f[0], f[1], f[2], f[3], f[4], f[5], o.col, o.alpha); break;
        case K_ROUNDARC: addRoundArc(s, f[0], f[1], f[2], f[3], o.col, o.alpha); break;
        case K_LINE: addLine(s, f[0], f[1], f[2], f[3], f[4], o.col, o.round, o.alpha); break;
        case K_HAND: addHand(s, f[0], f[1], f[2], f[3], o.col, o.alpha); break;
        case K_TEXT: addText(s, o.text.c_str(), f[0], f[1], (FontId)o.font, o.col, (Align)o.align, o.track, o.alpha); break;
        case K_RRECT: addRRect(s, f[0], f[1], f[2], f[3], f[4], o.col, o.alpha); break;
    }
}
static void buildInto(Scene &s, const std::vector<Op> &ops) {
    for (const Op &o : ops) addOp(s, o);
}

static std::string fmtf(float v) {
    char b[40];
    if (std::isnan(v)) return "NAN";
    if (std::isinf(v)) return v > 0 ? "INFINITY" : "-INFINITY";
    snprintf(b, sizeof b, "%.9g", v);
    std::string s = b;
    if (s.find_first_of(".en") == std::string::npos) s += ".0";
    return s + "f";
}
static std::string escText(const std::string &t) {
    std::string r;
    bool lastHex = false;
    for (unsigned char c : t) {
        if (c >= 32 && c < 127 && c != '"' && c != '\\') {
            if (lastHex && isxdigit(c)) r += "\" \"";
            r += (char)c;
            lastHex = false;
        } else {
            char b[8];
            snprintf(b, sizeof b, "\\x%02X", c);
            r += b;
            lastHex = true;
        }
    }
    return r;
}
static std::string opStr(const Op &o) {
    static const char *fonts[] = {"F13", "F16", "F21", "F26", "F34", "F40", "D70"};
    static const char *aligns[] = {"ALIGN_LEFT", "ALIGN_CENTER", "ALIGN_RIGHT"};
    char b[600];
    const float *f = o.f;
    switch (o.kind) {
        case K_CIRCLE:
            snprintf(b, sizeof b, "addCircle(s, %s, %s, %s, 0x%04X, %u);", fmtf(f[0]).c_str(), fmtf(f[1]).c_str(),
                     fmtf(f[2]).c_str(), o.col, o.alpha);
            break;
        case K_RING:
            snprintf(b, sizeof b, "addRing(s, %s, %s, %s, %s, 0x%04X, %u);", fmtf(f[0]).c_str(), fmtf(f[1]).c_str(),
                     fmtf(f[2]).c_str(), fmtf(f[3]).c_str(), o.col, o.alpha);
            break;
        case K_ARC:
            snprintf(b, sizeof b, "addArc(s, %s, %s, %s, %s, %s, %s, 0x%04X, %u);", fmtf(f[0]).c_str(), fmtf(f[1]).c_str(),
                     fmtf(f[2]).c_str(), fmtf(f[3]).c_str(), fmtf(f[4]).c_str(), fmtf(f[5]).c_str(), o.col, o.alpha);
            break;
        case K_ROUNDARC:
            snprintf(b, sizeof b, "addRoundArc(s, %s, %s, %s, %s, 0x%04X, %u);", fmtf(f[0]).c_str(), fmtf(f[1]).c_str(),
                     fmtf(f[2]).c_str(), fmtf(f[3]).c_str(), o.col, o.alpha);
            break;
        case K_LINE:
            snprintf(b, sizeof b, "addLine(s, %s, %s, %s, %s, %s, 0x%04X, %s, %u);", fmtf(f[0]).c_str(), fmtf(f[1]).c_str(),
                     fmtf(f[2]).c_str(), fmtf(f[3]).c_str(), fmtf(f[4]).c_str(), o.col, o.round ? "true" : "false", o.alpha);
            break;
        case K_HAND:
            snprintf(b, sizeof b, "addHand(s, %s, %s, %s, %s, 0x%04X, %u);", fmtf(f[0]).c_str(), fmtf(f[1]).c_str(),
                     fmtf(f[2]).c_str(), fmtf(f[3]).c_str(), o.col, o.alpha);
            break;
        case K_TEXT:
            snprintf(b, sizeof b, "addText(s, \"%s\", %s, %s, %s, 0x%04X, %s, %d, %u);", escText(o.text).c_str(),
                     fmtf(f[0]).c_str(), fmtf(f[1]).c_str(), fonts[o.font % 7], o.col, aligns[o.align % 3], o.track, o.alpha);
            break;
        case K_RRECT:
            snprintf(b, sizeof b, "addRRect(s, %s, %s, %s, %s, %s, 0x%04X, %u);", fmtf(f[0]).c_str(), fmtf(f[1]).c_str(),
                     fmtf(f[2]).c_str(), fmtf(f[3]).c_str(), fmtf(f[4]).c_str(), o.col, o.alpha);
            break;
        default: snprintf(b, sizeof b, "?"); break;
    }
    return b;
}

static Frame flat(const Frame &f) {
    Frame r;
    r.bg = f.bg;
    r.key = 0xF1A7;
    r.dyn = f.stat;
    r.dyn.insert(r.dyn.end(), f.dyn.begin(), f.dyn.end());
    return r;
}

// ---------------------------------------------------------------------------
// Rendering helpers
// ---------------------------------------------------------------------------
static bool inCircle(int x, int y) {
    int dx = 2 * x + 1 - 360, dy = 2 * y + 1 - 360;
    return dx * dx + dy * dy <= 358 * 358;
}

static void resetRenderer() {
    memset(&gR, 0, sizeof(gR));
    memset(gDirty, 0, sizeof(gDirty));
}

// One window covering the whole screen.
static void renderRef(const Scene &s, uint16_t *fb) {
    for (int i = 0; i < s.n; i++) prepPrim(s.prims[i], gPrep[i]);
    uint16_t *save = gPanel;
    bool acc = gAccount;
    RenderStats st = gStats;
    gAccount = false;
    gPanel = fb;
    renderRect(s, 0, 0, W, W);
    accCloseWindow();
    gPanel = save;
    gAccount = acc;
    gStats = st;
}

static void tileRect(const Scene &s, int x, int y, int w, int h, std::mt19937 &rng, int depth) {
    bool leaf = depth > 11 || (w <= 1 && h <= 1) || rng() % 6 == 0 || (w * h < 300 && rng() % 2);
    if (leaf) {
        renderRect(s, x, y, w, h);
        return;
    }
    if ((w >= h && w > 1) || h <= 1) {
        int c = 1 + (int)(rng() % (unsigned)(w - 1));
        tileRect(s, x, y, c, h, rng, depth + 1);
        tileRect(s, x + c, y, w - c, h, rng, depth + 1);
    } else {
        int c = 1 + (int)(rng() % (unsigned)(h - 1));
        tileRect(s, x, y, w, c, rng, depth + 1);
        tileRect(s, x, y + c, w, h - c, rng, depth + 1);
    }
}
// Random, non-grid window tiling (1 px windows included).
static void renderTiled(const Scene &s, uint16_t *fb, std::mt19937 &rng) {
    for (int i = 0; i < s.n; i++) prepPrim(s.prims[i], gPrep[i]);
    for (int i = 0; i < NPX; i++) fb[i] = 0xF81F;
    uint16_t *save = gPanel;
    bool acc = gAccount;
    RenderStats st = gStats;
    gAccount = false;
    gPanel = fb;
    tileRect(s, 0, 0, W, W, rng, 0);
    accCloseWindow();
    gPanel = save;
    gAccount = acc;
    gStats = st;
}

static bool sceneEqual(const Scene &a, const Scene &b, std::string &why) {
    char buf[160];
    if (a.bg != b.bg || a.n != b.n || a.ng != b.ng) {
        snprintf(buf, sizeof buf, "bg %04X/%04X n %u/%u ng %u/%u", a.bg, b.bg, a.n, b.n, a.ng, b.ng);
        why = buf;
        return false;
    }
    for (int i = 0; i < a.n; i++) {
        const Prim &p = a.prims[i], &q = b.prims[i];
        bool same = p.type == q.type && p.alpha == q.alpha && p.font == q.font && p.flags == q.flags && p.color == q.color &&
                    p.bx0 == q.bx0 && p.by0 == q.by0 && p.bx1 == q.bx1 && p.by1 == q.by1 && !memcmp(p.p, q.p, sizeof p.p);
        if (!same) {
            snprintf(buf, sizeof buf, "prim %d differs", i);
            why = buf;
            return false;
        }
    }
    if (memcmp(a.glyphs, b.glyphs, a.ng * sizeof(GPlace))) {
        why = "glyphs differ";
        return false;
    }
    return true;
}

static void savePNG(const uint16_t *fb, const std::string &name) {
    std::string bmp = std::string(kOutDir) + "/" + name + ".bmp";
    std::string png = std::string(kOutDir) + "/" + name + ".png";
    FILE *f = fopen(bmp.c_str(), "wb");
    if (!f) return;
    const int H = 360, rowSize = (W * 3 + 3) & ~3, dataSize = rowSize * H;
    uint8_t hdr[54] = {'B', 'M'};
    auto put32 = [&](int off, uint32_t v) { for (int i = 0; i < 4; i++) hdr[off + i] = v >> (8 * i); };
    put32(2, 54 + dataSize); put32(10, 54); put32(14, 40); put32(18, W); put32(22, H);
    hdr[26] = 1; hdr[28] = 24; put32(34, dataSize);
    fwrite(hdr, 1, 54, f);
    std::vector<uint8_t> row(rowSize);
    for (int y = H - 1; y >= 0; y--) {
        for (int x = 0; x < W; x++) {
            uint16_t c = fb[y * W + x];
            row[x * 3 + 2] = ((c >> 11) & 31) * 255 / 31;
            row[x * 3 + 1] = ((c >> 5) & 63) * 255 / 63;
            row[x * 3 + 0] = (c & 31) * 255 / 31;
        }
        fwrite(row.data(), 1, rowSize, f);
    }
    fclose(f);
    std::string cmd = "sips -s format png '" + bmp + "' --out '" + png + "' >/dev/null 2>&1 && rm '" + bmp + "'";
    if (system(cmd.c_str())) {}
}

// Replica of flushDirty's merge (runs across, identical runs down), on an already bridged tile set.
static void mergeCost(const uint64_t *bits, long &windows, long &px) {
    windows = px = 0;
    struct Span { int x0, x1, y0; bool seen; };
    std::vector<Span> open;
    for (int ty = 0; ty <= TILES; ty++) {
        uint64_t b = ty < TILES ? bits[ty] : 0;
        std::vector<Span> runs;
        for (int tx = 0; tx < TILES;) {
            if (!(b >> tx & 1)) { tx++; continue; }
            int st = tx;
            while (tx < TILES && (b >> tx & 1)) tx++;
            runs.push_back({st, tx, ty, false});
        }
        for (auto &o : open) o.seen = false;
        for (auto &r : runs)
            for (auto &o : open)
                if (!o.seen && o.x0 == r.x0 && o.x1 == r.x1) { o.seen = r.seen = true; break; }
        std::vector<Span> kept;
        for (auto &o : open) {
            if (o.seen) { kept.push_back(o); continue; }
            windows++;
            px += (long)(imin(W, o.x1 * 8) - o.x0 * 8) * (imin(W, ty * 8) - o.y0 * 8);
        }
        open = kept;
        for (auto &r : runs) if (!r.seen) open.push_back(r);
    }
}

// Rough ESP32-C3 cost (same model as sim.cpp): 1.8 us/px + 0.15 ms/window.
static double costMs(long px, long win) { return px * 0.0018 + win * 0.15; }

// Lower-bound-ish estimate of a smarter merge: per 8-connected component of painted
// tiles, the cheaper of the current merge and a single bounding-box window.
static double altCostMs(const uint64_t *tiles) {
    uint64_t left[TILES];
    memcpy(left, tiles, sizeof left);
    double total = 0;
    for (int ty = 0; ty < TILES; ty++)
        while (left[ty]) {
            int tx = __builtin_ctzll(left[ty]);
            uint64_t comp[TILES] = {};
            std::vector<std::pair<int, int>> stack{{tx, ty}};
            left[ty] &= ~(1ULL << tx);
            int x0 = tx, x1 = tx, y0 = ty, y1 = ty;
            while (!stack.empty()) {
                auto [cx, cy] = stack.back();
                stack.pop_back();
                comp[cy] |= 1ULL << cx;
                x0 = imin(x0, cx); x1 = imax(x1, cx); y0 = imin(y0, cy); y1 = imax(y1, cy);
                for (int dy = -1; dy <= 1; dy++)
                    for (int dx = -1; dx <= 1; dx++) {
                        int nx = cx + dx, ny = cy + dy;
                        if (nx < 0 || ny < 0 || nx >= TILES || ny >= TILES) continue;
                        if (left[ny] >> nx & 1) { left[ny] &= ~(1ULL << nx); stack.push_back({nx, ny}); }
                    }
            }
            long w, p;
            mergeCost(comp, w, p);
            long bpx = (long)(imin(W, (x1 + 1) * 8) - x0 * 8) * (imin(W, (y1 + 1) * 8) - y0 * 8);
            total += std::min(costMs(p, w), costMs(bpx, 1));
        }
    return total;
}

// ---------------------------------------------------------------------------
// Runner: plays frames and checks each one
// ---------------------------------------------------------------------------
struct Totals {
    long frames = 0, fullFrames = 0;
    long unmarkedFrames = 0, unmarkedPx = 0, artifactPx = 0;
    long mismatchFrames = 0, mismatchPx = 0;
    long simChecks = 0, simBad = 0, tiledChecks = 0, tiledBad = 0;
    long sceneBad = 0, windowBad = 0, tileSetBad = 0, overlapPx = 0, misaligned = 0, outOfWindow = 0, crashes = 0;
    long incFrames = 0, markedTiles = 0, neededTiles = 0, paintedPx = 0, windows = 0, changedPx = 0;
    double estMs = 0, altMs = 0, maxEstMs = 0;
    void add(const Totals &o) {
        frames += o.frames; fullFrames += o.fullFrames;
        unmarkedFrames += o.unmarkedFrames; unmarkedPx += o.unmarkedPx; artifactPx += o.artifactPx;
        mismatchFrames += o.mismatchFrames; mismatchPx += o.mismatchPx;
        simChecks += o.simChecks; simBad += o.simBad; tiledChecks += o.tiledChecks; tiledBad += o.tiledBad;
        sceneBad += o.sceneBad; windowBad += o.windowBad; tileSetBad += o.tileSetBad; overlapPx += o.overlapPx;
        misaligned += o.misaligned; outOfWindow += o.outOfWindow; crashes += o.crashes;
        incFrames += o.incFrames; markedTiles += o.markedTiles; neededTiles += o.neededTiles;
        paintedPx += o.paintedPx; windows += o.windows; changedPx += o.changedPx;
        estMs += o.estMs; altMs += o.altMs; maxEstMs = std::max(maxEstMs, o.maxEstMs);
    }
    bool bad() const {
        return unmarkedPx || mismatchPx || simBad || tiledBad || sceneBad || windowBad || tileSetBad || overlapPx ||
               outOfWindow || crashes;
    }
};

struct Failure { Frame prev, cur; long unmarked, mismatch; int frame; };

struct Runner {
    std::string name;
    std::vector<uint16_t> refPrev = std::vector<uint16_t>(NPX), refCur = std::vector<uint16_t>(NPX),
                          tmp = std::vector<uint16_t>(NPX);
    bool havePrev = false, quiet = false;
    int simEvery = 5, tiledEvery = 13, frameNo = 0, printed = 0;
    Totals t;
    Frame prevFrame;
    long lastUnmarked = 0;
    std::vector<Failure> failures;
    std::mt19937 rng{12345};
};

static int gPngBudget = 6;
static Scene gRefScene;
static RendererState gSavedState;

static void step(Runner &R, const Frame &fr) {
    Scene &ref = gRefScene;
    ref.bg = fr.bg;
    ref.n = ref.ng = 0;
    buildInto(ref, fr.stat);
    buildInto(ref, fr.dyn);
    renderRef(ref, R.refCur.data());

    if (fr.invalidate) renderInvalidate();
    bool needStatic;
    Scene &s = frameBegin(fr.bg, fr.key, needStatic);
    if (needStatic) {
        buildInto(s, fr.stat);
        staticDone(s);
    }
    buildInto(s, fr.dyn);
    std::string why;
    if (!sceneEqual(s, ref, why)) {
        R.t.sceneBad++;
        if (!R.quiet && R.printed++ < 5) printf("  ! %s frame %d: renderer scene != fresh build (%s)\n", R.name.c_str(), R.frameNo, why.c_str());
    }

    bool full = !gR.haveCur || gR.forceFull || gR.scenes[gR.cur].bg != s.bg;
    uint64_t marked[TILES];
    if (full) {
        memcpy(marked, gMask, sizeof marked);
    } else {
        memset(gDirty, 0, sizeof gDirty);
        markChanges(gR.scenes[gR.cur], s);
        for (int ty = 0; ty < TILES; ty++) marked[ty] = gDirty[ty] & gMask[ty];
        memset(gDirty, 0, sizeof gDirty);
    }

    memset(&gAcc, 0, sizeof gAcc);
    memset(gPaint, 0, sizeof gPaint);
    long oow = gOutOfWindow;
    gAccount = true;
    frameEnd();
    gAccount = false;
    accCloseWindow();
    R.t.frames++;
    if (full) R.t.fullFrames++;

    // Windows.
    if (gAcc.badRect || gAcc.badCount) R.t.windowBad += gAcc.badRect + gAcc.badCount;
    R.t.outOfWindow += gOutOfWindow - oow;
    R.t.overlapPx += gAcc.overlapPx;
    R.t.misaligned += gAcc.misaligned;
    bool anyMarked = false;
    for (int ty = 0; ty < TILES; ty++) anyMarked |= marked[ty] != 0;
    int tileSetErr = 0;
    for (int ty = 0; ty < TILES; ty++) {
        uint64_t b = anyMarked ? marked[ty] : 0;
        b |= (b >> 1) & (b << 1);
        if (b != gAcc.tiles[ty]) tileSetErr++;
    }
    if (tileSetErr) R.t.tileSetBad++;

    // Changed pixels vs marked / painted tiles.
    long changed = 0, unmarked = 0, artifact = 0, needed = 0;
    int bx0 = W, by0 = W, bx1 = -1, by1 = -1;
    if (R.havePrev) {
        uint64_t need[TILES] = {};
        for (int y = 0; y < W; y++)
            for (int x = 0; x < W; x++) {
                int i = y * W + x;
                if (!inCircle(x, y) || R.refPrev[i] == R.refCur[i]) continue;
                changed++;
                int tx = x >> 3, ty = y >> 3;
                need[ty] |= 1ULL << tx;
                if (!(marked[ty] >> tx & 1)) {
                    unmarked++;
                    bx0 = imin(bx0, x); by0 = imin(by0, y); bx1 = imax(bx1, x); by1 = imax(by1, y);
                }
                if (!(gAcc.tiles[ty] >> tx & 1)) artifact++;
            }
        for (int ty = 0; ty < TILES; ty++) needed += __builtin_popcountll(need[ty]);
    }
    long mism = 0;
    int worstCh = 0;  // largest per-channel error of a stale pixel, in 0..255 steps
    for (int y = 0; y < W; y++)
        for (int x = 0; x < W; x++) {
            uint16_t a = gPanelA[y * W + x], b = R.refCur[y * W + x];
            if (!inCircle(x, y) || a == b) continue;
            mism++;
            int dr = abs(((a >> 11) & 31) - ((b >> 11) & 31)) * 255 / 31, dg = abs(((a >> 5) & 63) - ((b >> 5) & 63)) * 255 / 63,
                db = abs((a & 31) - (b & 31)) * 255 / 31;
            worstCh = std::max(worstCh, std::max(dr, std::max(dg, db)));
        }

    if (!full && R.havePrev) {
        long nm = 0;
        for (int ty = 0; ty < TILES; ty++) nm += __builtin_popcountll(marked[ty]);
        R.t.incFrames++;
        R.t.markedTiles += nm;
        R.t.neededTiles += needed;
        R.t.paintedPx += gAcc.px;
        R.t.windows += gAcc.windows;
        R.t.changedPx += changed;
        double c = costMs(gAcc.px, gAcc.windows);
        R.t.estMs += c;
        R.t.maxEstMs = std::max(R.t.maxEstMs, c);
        R.t.altMs += anyMarked ? altCostMs(gAcc.tiles) : 0;
    }
    if (unmarked) { R.t.unmarkedFrames++; R.t.unmarkedPx += unmarked; R.t.artifactPx += artifact; }
    if (mism) { R.t.mismatchFrames++; R.t.mismatchPx += mism; }
    R.lastUnmarked = unmarked;

    bool newMismatch = mism && !(R.frameNo > 0 && R.t.mismatchFrames > 1);
    if ((unmarked || newMismatch) && R.havePrev) {
        if (R.failures.size() < 4) R.failures.push_back({R.prevFrame, fr, unmarked, mism, R.frameNo});
        if (!R.quiet && R.printed++ < 6) {
            printf("  ! %s frame %d: %ld changed px in unmarked tiles (%ld not repainted), %ld px differ from a full repaint",
                   R.name.c_str(), R.frameNo, unmarked, artifact, mism);
            if (unmarked) printf("; unmarked bbox (%d,%d)-(%d,%d)", bx0, by0, bx1, by1);
            if (mism) printf("; worst stale channel error %d/255", worstCh);
            printf("\n");
            Frame a = flat(R.prevFrame), b = flat(fr);
            int shown = 0;
            for (size_t i = 0; i < std::max(a.dyn.size(), b.dyn.size()) && shown < 6; i++) {
                std::string sa = i < a.dyn.size() ? opStr(a.dyn[i]) : "(none)", sb = i < b.dyn.size() ? opStr(b.dyn[i]) : "(none)";
                if (sa != sb) { printf("      [%zu] %s\n        -> %s\n", i, sa.c_str(), sb.c_str()); shown++; }
            }
        }
        if (gPngBudget > 0) {
            gPngBudget--;
            static uint16_t diffImg[NPX];
            for (int i = 0; i < NPX; i++) {
                int x = i % W, y = i / W;
                bool bad = inCircle(x, y) && gPanelA[i] != R.refCur[i];
                diffImg[i] = bad ? 0xF800 : (R.refCur[i] >> 2 & 0x39E7);
            }
            std::string label = "stress_" + R.name + "_f" + std::to_string(R.frameNo);
            for (char &ch : label) if (!isalnum((unsigned char)ch) && ch != '_') ch = '_';
            savePNG(gPanelA, label + "_incremental");
            savePNG(R.refCur.data(), label + "_full");
            savePNG(diffImg, label + "_diff");
        }
    }

    // sim.cpp's checkAgainstFull() path: reset gR, forceFull, rebuild the frame.
    if (R.simEvery && R.frameNo % R.simEvery == R.simEvery - 1) {
        gSavedState = gR;
        RenderStats st = gStats;
        memset(&gR, 0, sizeof gR);
        gR.forceFull = true;
        for (int i = 0; i < NPX; i++) gPanelB[i] = 0xF81F;
        gPanel = gPanelB;
        bool ns;
        Scene &s2 = frameBegin(fr.bg, fr.key, ns);
        buildInto(s2, fr.stat);
        if (ns) staticDone(s2);
        buildInto(s2, fr.dyn);
        frameEnd();
        gPanel = gPanelA;
        gStats = st;
        gR = gSavedState;
        long d = 0;
        for (int y = 0; y < W; y++)
            for (int x = 0; x < W; x++)
                if (inCircle(x, y) && gPanelB[y * W + x] != R.refCur[y * W + x]) d++;
        R.t.simChecks++;
        if (d) {
            R.t.simBad++;
            if (!R.quiet && R.printed++ < 6) printf("  ! %s frame %d: sim-style full repaint differs from reference by %ld px\n", R.name.c_str(), R.frameNo, d);
        }
    }
    if (R.tiledEvery && R.frameNo % R.tiledEvery == R.tiledEvery - 1) {
        renderTiled(ref, R.tmp.data(), R.rng);
        long d = 0;
        for (int i = 0; i < NPX; i++) d += R.tmp[i] != R.refCur[i];
        R.t.tiledChecks++;
        if (d) {
            R.t.tiledBad++;
            if (!R.quiet && R.printed++ < 6) printf("  ! %s frame %d: random window tiling differs from one window by %ld px\n", R.name.c_str(), R.frameNo, d);
        }
    }

    R.refPrev.swap(R.refCur);
    R.havePrev = true;
    R.prevFrame = fr;
    R.frameNo++;
}

static void startRun(Runner &R) {
    resetRenderer();
    memset(gPanelA, 0, sizeof gPanelA);
    gPanel = gPanelA;
    R.havePrev = false;
    R.frameNo = 0;
}

// Unmarked changed pixels for the transition a -> b, from a clean renderer.
static long transitionUnmarked(const Frame &a, const Frame &b) {
    Runner *R = new Runner;
    R->quiet = true;
    R->simEvery = R->tiledEvery = 0;
    int budget = gPngBudget;
    gPngBudget = 0;
    startRun(*R);
    step(*R, a);
    step(*R, b);
    long u = R->lastUnmarked;
    gPngBudget = budget;
    delete R;
    return u;
}

// Greedy delta-debugging: drop primitives (same index in both frames) while the miss persists.
static void minimize(const Failure &f, const std::string &label) {
    Frame A = flat(f.prev), B = flat(f.cur);
    B.key = A.key;
    if (A.bg != B.bg || transitionUnmarked(A, B) == 0) {
        printf("    (%s: the miss does not reproduce as a plain 2-frame sequence; it needs the static-layer/invalidate history)\n", label.c_str());
        return;
    }
    bool progress = true;
    while (progress) {
        progress = false;
        for (int i = (int)std::max(A.dyn.size(), B.dyn.size()) - 1; i >= 0; i--) {
            Frame A2 = A, B2 = B;
            if (i < (int)A2.dyn.size()) A2.dyn.erase(A2.dyn.begin() + i);
            if (i < (int)B2.dyn.size()) B2.dyn.erase(B2.dyn.begin() + i);
            if (transitionUnmarked(A2, B2) > 0) { A = A2; B = B2; progress = true; }
        }
    }
    long u = transitionUnmarked(A, B);
    printf("    minimal repro (%s): %ld unmarked px\n", label.c_str(), u);
    printf("      frame 1 (bg 0x%04X):\n", A.bg);
    for (auto &o : A.dyn) printf("        %s\n", opStr(o).c_str());
    printf("      frame 2:\n");
    for (auto &o : B.dyn) printf("        %s\n", opStr(o).c_str());
}

static void printTotals(const char *name, const Totals &t) {
    double n = t.incFrames ? (double)t.incFrames : 1;
    printf("%-26s %s fr %4ld | miss %ld px/%ld fr, mism %ld px/%ld fr, sim %ld/%ld tiled %ld/%ld scene %ld win %ld tiles %ld ovl %ld%s | "
           "marked %5.1f need %5.1f tiles  %6.0f px %4.1f win ~%5.2f ms (max %5.2f, alt %5.2f)\n",
           name, t.bad() ? "FAIL" : "PASS", t.frames, t.unmarkedPx, t.unmarkedFrames, t.mismatchPx, t.mismatchFrames, t.simBad,
           t.simChecks, t.tiledBad, t.tiledChecks, t.sceneBad, t.windowBad + t.outOfWindow, t.tileSetBad, t.overlapPx,
           t.crashes ? (" CRASHED " + std::to_string(t.crashes)).c_str() : "",
           t.markedTiles / n, t.neededTiles / n, t.paintedPx / n, t.windows / n, t.estMs / n, t.maxEstMs, t.altMs / n);
}

// ---------------------------------------------------------------------------
// Targeted cases
// ---------------------------------------------------------------------------
struct Case { std::string name; std::vector<Frame> frames; };
using FrameFn = std::function<void(int, Frame &)>;
static Case mk(const char *name, int n, FrameFn fn) {
    Case c;
    c.name = name;
    for (int i = 0; i < n; i++) {
        Frame f;
        fn(i, f);
        c.frames.push_back(f);
    }
    return c;
}
#define COUNT(a) (int)(sizeof(a) / sizeof((a)[0]))

static const uint16_t PEACH = 0xFE96, MINT = 0xAF39, TRACK = 0x2124, DIM = 0x9CD3, CORAL = 0xFD54;

static std::vector<Op> backdrop() {
    std::vector<Op> v;
    v.push_back(RG(180, 180, 150, 170, TRACK));
    for (int i = 0; i < 12; i++) {
        float a = i * 30 * (float)M_PI / 180;
        v.push_back(LN(180 + 151 * sinf(a), 180 - 151 * cosf(a), 180 + 171 * sinf(a), 180 - 171 * cosf(a), i % 3 ? 2 : 4.5f,
                       i % 3 == 0, i % 3 ? DIM : 0xFFFF));
    }
    v.push_back(TX("12", 180, 58, F34));
    v.push_back(TX("6", 180, 302, F34));
    v.push_back(RR(180, 240, 140, 40, 12, 0x39E7, 160));
    v.push_back(C(180, 180, 60, 0x18C3, 200));
    return v;
}
static void withBackdrop(Frame &f) { f.stat = backdrop(); f.key = 1; }

static std::vector<Case> targetedCases() {
    std::vector<Case> cs;

    cs.push_back(mk("circle_subpixel", 90, [](int i, Frame &f) {
        withBackdrop(f);
        f.dyn = {C(90 + i * 0.031f, 120 + i * 0.047f, 25, PEACH), C(250 - i * 0.013f, 200, 0.02f + i * 0.02f),
                 C(180 + i * 0.0625f, 180, 7.5f, 0xFFFF, 128), C(8 + i * 0.1f, 180, 3.9f, MINT)};
    }));
    cs.push_back(mk("circle_jumps", 60, [](int i, Frame &f) {
        static const float P[][2] = {{180, 180}, {-50, -50}, {400, 180}, {-30, 180}, {180, 390}, {0, 0}, {359.9f, 359.9f},
                                     {8, 8}, {180, -7}, {352, 180}, {-10000, 0}, {180, 180}, {40, 40}, {320, 300}, {-1, 181}};
        static const float R[] = {30, 200, 500, 0.5f, 0, 12};
        withBackdrop(f);
        f.dyn = {C(P[i % COUNT(P)][0], P[i % COUNT(P)][1], R[i % COUNT(R)], CORAL, i % 4 == 3 ? 100 : 255)};
    }));
    cs.push_back(mk("circle_radius", 40, [](int i, Frame &f) {
        static const float R[] = {0, 0.01f, 0.03f, 0.06f, 0.5f, 1, 7.99f, 8, 8.01f, 50, 179, 180, 181, 255, 400, 2880, 1000, 3, 0, 120};
        withBackdrop(f);
        f.dyn = {C(180, 180, R[i % COUNT(R)], PEACH, 180), C(100.5f, 100.5f, R[(i + 7) % COUNT(R)] / 4, MINT)};
    }));
    cs.push_back(mk("circle_huge_sane", 30, [](int i, Frame &f) {
        withBackdrop(f);
        f.dyn = {C(-1000 + i * 1.3f, 180, 1200, MINT, 200), C(180, 180, 2800 + i * 2.5f, 0x4208, 90), C(180, 540 - i * 0.7f, 200, PEACH)};
    }));
    cs.push_back(mk("ring_inner_edge", 40, [](int i, Frame &f) {
        static const float V[] = {100, 100.02f, 100.5f, 107, 140, 149.9f, 150, 149.99f, 60, 0.4f, 0, -5, 0.2f, 100, 100.03f, 7, 8, 9, 23.97f};
        withBackdrop(f);
        f.dyn = {RG(180, 180, V[i % COUNT(V)], 150, PEACH)};
    }));
    cs.push_back(mk("ring_outer_edge", 40, [](int i, Frame &f) {
        static const float V[] = {150, 150.02f, 150.5f, 157, 200, 100.01f, 100, 300, 1000, 150, 179.5f, 180.5f, 186, 101};
        withBackdrop(f);
        f.dyn = {RG(180, 180, 100, V[i % COUNT(V)], MINT, 220)};
    }));
    cs.push_back(mk("ring_both_edges", 44, [](int i, Frame &f) {
        static const float V[][2] = {{100, 110}, {100.1f, 110.1f}, {120, 130}, {95, 135}, {0, 180}, {179, 181}, {2, 3},
                                     {100, 110}, {100.03f, 109.97f}, {60, 61}, {160, 161}, {60.5f, 161.5f}};
        withBackdrop(f);
        f.dyn = {RG(180, 180, V[i % COUNT(V)][0], V[i % COUNT(V)][1], CORAL), RG(180.5f, 179.5f, V[(i + 5) % COUNT(V)][0] / 2, V[(i + 5) % COUNT(V)][1] / 2, 0xFFFF, 128)};
    }));
    cs.push_back(mk("ring_offcentre", 36, [](int i, Frame &f) {
        withBackdrop(f);
        f.dyn = {RG(40, 60, 100 + (i % 6) * 1.7f, 120 + (i % 5) * 3.1f, PEACH), RG(-100, 180, 200 + (i % 4) * 0.3f, 210 + (i % 3), MINT),
                 RG(180, 540, 300 - (i % 7), 330 + (i % 2) * 0.5f, 0xFFFF, 200)};
    }));
    cs.push_back(mk("ring_halfpixel_centre", 16, [](int i, Frame &f) {
        static const float V[] = {0, 0.5f, 0, 0.01f, 0.2f, 0, 1, 0};
        withBackdrop(f);
        f.dyn = {RG(180.5f, 180.5f, V[i % COUNT(V)], 30, PEACH), RG(100, 100, V[(i + 3) % COUNT(V)], 12, MINT)};
    }));
    cs.push_back(mk("arc_end_small_steps", 700, [](int i, Frame &f) {
        withBackdrop(f);
        float span;
        if (i < 494) span = i * 0.73f;
        else if (i < 594) span = 179.5f + (i - 494) * 0.01f;
        else span = 359.0f + (i - 594) * 0.01f;
        f.dyn = {AR(180, 180, 140, 160, 30, 30 + span, PEACH), AR(180, 180, 100, 104, -90, -90 + span * 0.5f, MINT)};
    }));
    cs.push_back(mk("arc_end_jumps", 220, [](int i, Frame &f) {
        static const float S[] = {10, 170, 179.99f, 180, 180.01f, 190, 350, 359, 359.9f, 359.98f, 359.99f, 360, 359.95f, 200, 181,
                                  180, 90, 0.01f, 0.5f, 359.9f, 190, 359.9f, 350, 181, 359.5f, 0.05f, 179.9f, 0.2f, 180};
        static const float A0[] = {0, 30, 90, 211.1f, 359.9f, -45, 137.37f};
        withBackdrop(f);
        float a0 = A0[(i / COUNT(S)) % COUNT(A0)];
        f.dyn = {AR(180, 180, 140, 160, a0, a0 + S[i % COUNT(S)], PEACH)};
    }));
    cs.push_back(mk("arc_start_moves", 60, [](int i, Frame &f) {
        static const float A[] = {299.99f, 299.5f, 290, 200, 120.01f, 120, 119.99f, 100, -59, -59.99f, -60, 0, -50, 250, 299.999f};
        withBackdrop(f);
        f.dyn = {AR(180, 180, 130, 165, A[i % COUNT(A)], 300, CORAL), AR(180, 180, 60, 90, A[(i + 4) % COUNT(A)] - 120, 180, MINT, 160)};
    }));
    cs.push_back(mk("arc_rotate", 240, [](int i, Frame &f) {
        withBackdrop(f);
        float a = -50 + i * 3.7f;
        f.dyn = {AR(180, 180, 140, 160, a, a + 40, PEACH), AR(180, 180, 110, 125, i * 1.0f, i * 1.0f + 359.9f, MINT),
                 AR(180, 180, 80, 95, -i * 0.9f, -i * 0.9f + 0.05f, 0xFFFF), AR(180, 180, 30, 50, i * 0.37f, i * 0.37f + 181, CORAL)};
    }));
    cs.push_back(mk("arc_wrap_start", 48, [](int i, Frame &f) {
        static const float A[] = {359.9f, 0.1f, 359.95f, 0.05f, -0.05f, 360.05f, 720.1f, -359.9f, 359.99f, 0.01f, 0, 360};
        static const float S[] = {90, 270, 5, 359.9f};
        withBackdrop(f);
        float a = A[i % COUNT(A)], s = S[(i / COUNT(A)) % COUNT(S)];
        f.dyn = {AR(180, 180, 140, 160, a, a + s, PEACH)};
    }));
    cs.push_back(mk("arc_full_360", 60, [](int i, Frame &f) {
        static const float S[] = {360, 400, 359.99f, 359.995f, 359.98f, 360.01f};
        withBackdrop(f);
        float a = i * 7.3f;
        f.dyn = {AR(180, 180, 140, 160, a, a + S[i % COUNT(S)], PEACH), AR(170, 190, 50, 70, -a, -a + S[(i + 2) % COUNT(S)], MINT)};
    }));
    cs.push_back(mk("arc_pie", 80, [](int i, Frame &f) {
        static const float S[] = {0.01f, 1, 10, 90, 179.9f, 180, 181, 270, 359.9f, 360};
        withBackdrop(f);
        float s = i < 40 ? S[i % COUNT(S)] : (i - 40) * 9.1f;
        f.dyn = {AR(180, 180, 0, 60, 20, 20 + s, PEACH), AR(180.5f, 180.5f, 0, 25, 200, 200 + s * 0.5f, MINT)};
    }));
    cs.push_back(mk("arc_thin", 120, [](int i, Frame &f) {
        withBackdrop(f);
        f.dyn = {AR(180, 180, 150, 150.3f, 10, 10 + i * 3.01f, 0xFFFF), AR(180, 180, 120, 120.05f, -i * 2.9f, 100, PEACH)};
    }));
    cs.push_back(mk("arc_offscreen", 80, [](int i, Frame &f) {
        withBackdrop(f);
        f.dyn = {AR(400, 180, 250, 270, 180, 180 + i * 4.5f, PEACH), AR(180, -200, 300, 310, 90 + i * 0.7f, 270, MINT),
                 AR(-60, -60, 100, 400, i * 11.0f, i * 11.0f + 45, CORAL, 150)};
    }));
    cs.push_back(mk("roundarc_progress", 260, [](int i, Frame &f) {
        withBackdrop(f);
        float p = (i % 230) * 1.7f;
        f.dyn = {RA(160, 12, 0, p, PEACH), RA(120, 8, -90 + i * 0.5f, 90, MINT), RA(90, 0.5f, 0, p / 2, 0xFFFF)};
    }));
    cs.push_back(mk("countdown_jumps", 40, [](int i, Frame &f) {
        static const float F[] = {1, 0.99999f, 0.9999f, 0.9997f, 0.999f, 0.99f, 0.97f, 0.9997f, 0.5001f, 0.5f, 0.4999f, 0.001f,
                                  0.0001f, 0.00001f, 0.999f, 0.97f, 0.9995f};
        withBackdrop(f);
        float fr = F[i % COUNT(F)];
        f.dyn = {AR(180, 180, 150, 162, 0, 360 * fr, PEACH), RA(135, 10, 0, 360 * fr, MINT)};
    }));
    cs.push_back(mk("near_180_narrow", 120, [](int i, Frame &f) {
        static const float S[] = {179.5f, 180, 179.9f, 180, 170, 180, 179.99f, 150, 180};
        static const float A[] = {0, 17.3f, 90, 211.1f, 300.7f};
        withBackdrop(f);
        float a = A[(i / COUNT(S)) % COUNT(A)];
        f.dyn = {AR(180, 180, 20, 170, a, a + S[i % COUNT(S)], PEACH)};
    }));
    cs.push_back(mk("hands_sweep", 400, [](int i, Frame &f) {
        withBackdrop(f);
        float sec = i * 0.05f * 6;  // 20 fps
        float sd = roundf(sec / 0.2f) * 0.2f;
        float md = roundf((10 * 6 + sec / 60) / 0.25f) * 0.25f;
        f.dyn = {HD(300 + md / 12, 79, 23, 10), HD(md, 119, 23, 6), HD(sd, 137, 31, 2.2f, PEACH), C(180, 180, 7), C(180, 180, 3, PEACH)};
    }));
    cs.push_back(mk("line_edges", 90, [](int i, Frame &f) {
        static const float Wd[] = {0.01f, 0.5f, 1, 2.2f, 30, 0.1f};
        withBackdrop(f);
        float w = Wd[i % COUNT(Wd)], d = i * 0.01f;
        f.dyn = {LN(40, 100 + d, 320, 100.4f + d, w, i % 2), LN(64 + d, 30, 64.2f + d, 330, w, i % 3 == 0, PEACH),
                 LN(180, 180, 180, 180, w * 2 + 1, i % 2, MINT), LN(-1500, -1000, 2000, 1500 + i * 3, 3, false, CORAL),
                 LN(200, 200 + i, 200.001f, 200 + i, 9, true), LN(96, 16, 96, 344, 1, false, 0xFFFF, 128),
                 LN(300 - i * 2.7f, 60, 60 + i * 1.3f, 300, 5 + (i % 4), i % 2, DIM)};
    }));
    cs.push_back(mk("line_hand_steps", 120, [](int i, Frame &f) {
        withBackdrop(f);
        float deg = i < 60 ? i * 0.013f : 60 * 0.013f + (i - 60) * 37.1f;
        f.dyn = {HD(deg, 170, 40, 4), HD(-deg * 3, 400, 400, 1.5f, PEACH), HD(deg + 90, 0, 0, 12, MINT)};
    }));
    cs.push_back(mk("text_changes", 140, [](int i, Frame &f) {
        static const char *S[] = {"12:59", "13:00", "9:59", "10:00", "", " ", "0", "Mathematics \xC2\xB7 Paper 1", "WWW", "iii",
                                  "\xC2\xB7", "\xC2", "\xFF\xFE", "a\x80\x80z", "88:88", "gjpqy", "\xC3\x84\xC3\x96", "Halo", "1:00"};
        withBackdrop(f);
        const char *s = S[i % COUNT(S)];
        float x = 180 + ((i / 3) % 5) * 0.37f;
        f.dyn = {TX(s, x, 180, (i / COUNT(S)) % 6, (i / 2) % 3, (i / 5) % 4 - 1, PEACH),
                 TX(S[(i + 3) % COUNT(S)], 20 + (i % 7) * 0.5f, 120, F16, ALIGN_LEFT, 0, MINT),
                 TX("12:34", 180, 260, D70, ALIGN_CENTER, 0, 0xFFFF, (uint8_t)(i * 37)),
                 TX((i % 2) ? "12:35" : "12:3:", 180 + (i % 3), 120, D70, ALIGN_RIGHT, 2),
                 TX(s, -30 + (i % 5) * 20.0f, 350 + (i % 4) * 10.0f, F40, ALIGN_LEFT)};
    }));
    cs.push_back(mk("text_rounding", 60, [](int i, Frame &f) {
        withBackdrop(f);
        float x = 180.5f + ((i % 3) - 1) * 0.001f, cy = 99.5f + (i % 2) * 0.002f;
        f.dyn = {TX("Halo", x, cy, F40), TX("09:41", 180 + i * 0.25f, 200.49f + (i % 2) * 0.02f, F26, ALIGN_RIGHT)};
    }));
    cs.push_back(mk("text_glyph_limit", 60, [](int i, Frame &f) {
        f.key = 7;
        for (int k = 0; k < 20; k++) f.stat.push_back(TX("ABCDEFGHIJKLMNOPQRST", 180, 20 + k * 16.0f, F13, ALIGN_CENTER, 0, DIM));
        std::string d(i % 45, 'X');
        for (size_t k = 0; k < d.size(); k++) d[k] = 'A' + (char)((k * 7 + i) % 26);
        f.dyn = {TX(d, 180, 180, F21, ALIGN_CENTER, 0, PEACH), C(180 + (i % 3), 330, 10, MINT), TX(i % 2 ? "Z" : "Y", 300, 300, F34)};
    }));
    cs.push_back(mk("rrect_changes", 80, [](int i, Frame &f) {
        static const float R[] = {0, 5, 20, 100, -5, 1000};
        withBackdrop(f);
        f.dyn = {RR(180 + (i % 5) * 0.3f, 100 + i, 200 - i, 44, R[i % COUNT(R)], PEACH),
                 RR(20 + i * 5.0f, 340, 60, 30 + (i % 3), 8, MINT, 180), RR(180, 180, 0.1f + (i % 4), 0.1f, 3, 0xFFFF),
                 RR(-40, 180, 100, 50, 25, CORAL), RR(180, 180, 360, 360, 180 - (i % 10), 0x2104, 60)};
    }));
    cs.push_back(mk("alpha_toggle", 60, [](int i, Frame &f) {
        static const uint8_t A[] = {255, 0, 128, 255, 1, 254, 0, 0, 77};
        withBackdrop(f);
        auto a = [&](int k) { return A[(i + k) % COUNT(A)]; };
        f.dyn = {C(170, 170, 50, PEACH, a(0)), RG(180, 180, 40, 80, MINT, a(1)), AR(180, 180, 90, 120, 10, 250, CORAL, a(2)),
                 LN(60, 60, 300, 300, 8, true, 0xFFFF, a(3)), TX("12:00", 180, 180, F40, ALIGN_CENTER, 0, 0xFFFF, a(4)),
                 RR(180, 200, 100, 60, 10, DIM, a(5)), RA(140, 10, 45, 300, PEACH, a(6)), HD(i * 6.0f, 150, 20, 3, MINT, a(7))};
    }));
    cs.push_back(mk("color_only", 30, [](int i, Frame &f) {
        withBackdrop(f);
        uint16_t c = (uint16_t)(i * 2654435761u >> 16);
        f.dyn = {RG(180, 180, 100, 140, c), TX("COLOUR", 180, 180, F26, ALIGN_CENTER, 0, (uint16_t)~c), C(100, 250, 20, c),
                 AR(180, 180, 40, 60, 0, 200, (uint16_t)(c ^ 0x5555))};
    }));
    cs.push_back(mk("count_changes", 80, [](int i, Frame &f) {
        withBackdrop(f);
        std::vector<Op> all = {C(100, 100, 20, PEACH), RG(180, 180, 90, 100, MINT), TX("A", 250, 250, F34), LN(40, 300, 320, 60, 3),
                               AR(180, 180, 120, 130, 0, 90, CORAL), RR(180, 320, 80, 20, 6, DIM)};
        switch (i % 8) {
            case 0: f.dyn = all; break;
            case 1: f.dyn.assign(all.begin(), all.end() - 2); break;
            case 2: f.dyn.assign(all.begin() + 1, all.end()); break;  // index shift
            case 3: f.dyn = {}; break;
            case 4: f.dyn = all; f.dyn.insert(f.dyn.begin() + 2, TX("inserted", 180, 150, F21)); break;
            case 5: f.dyn = all; std::swap(f.dyn[0], f.dyn[5]); break;
            case 6: f.dyn = all; f.dyn.erase(f.dyn.begin() + 3); break;
            case 7: f.dyn = all; f.dyn.push_back(C(180, 180, 170, 0x0841, 40)); break;
        }
    }));
    cs.push_back(mk("static_layer", 90, [](int i, Frame &f) {
        int phase = (i / 10) % 6;
        std::vector<Op> s1 = backdrop();
        std::vector<Op> s2 = {TX("STATIC TWO", 180, 100, F21, ALIGN_CENTER, 1, MINT), RG(180, 180, 160, 175, PEACH), C(60, 180, 30, DIM)};
        switch (phase) {
            case 0: f.key = 1; f.stat = s1; break;
            case 1: f.key = 2; f.stat = s2; break;
            case 2: f.key = 1; f.stat = s1; f.invalidate = i % 10 == 4; break;
            case 3: f.key = 1; f.stat = s1; f.bg = (i % 2) ? 0x0000 : 0x0842; break;
            case 4: f.key = 3; f.stat = {}; break;
            case 5: f.key = 2; f.stat = s2; f.invalidate = i % 3 == 0; break;
        }
        f.dyn = {HD(i * 4.0f, 140, 20, 3, PEACH), TX(std::to_string(i), 180, 220, F26), AR(180, 180, 100, 110, 0, i * 4.1f, MINT)};
    }));
    cs.push_back(mk("max_prims", 50, [](int i, Frame &f) {
        f.key = 9;
        for (int k = 0; k < 100; k++) f.stat.push_back(C(20 + (k % 10) * 36.0f, 20 + (k / 10) * 36.0f, 10, (uint16_t)(k * 997)));
        int nd = 55 + (i % 15);  // 155..169 total: overflow on some frames
        for (int k = 0; k < nd; k++) {
            float d = (k >= 50 && (i + k) % 3 == 0) ? i * 0.7f : 0;
            f.dyn.push_back(k % 2 ? LN(30 + k * 5.0f, 40 + d, 60 + k * 4.0f, 320, 2, true, PEACH) : RG(180, 180, 20 + k * 2.5f + d * 0.1f, 21 + k * 2.5f, MINT, 120));
        }
    }));
    cs.push_back(mk("bg_changes", 30, [](int i, Frame &f) {
        withBackdrop(f);
        static const uint16_t B[] = {0x0000, 0x0000, 0x2945, 0x2945, 0xFFFF, 0x0000};
        f.bg = B[i % COUNT(B)];
        f.dyn = {C(180 + i, 180, 30, PEACH)};
    }));
    cs.push_back(mk("invalidate_mid", 40, [](int i, Frame &f) {
        withBackdrop(f);
        f.invalidate = i % 7 == 3 || i % 11 == 0;
        f.dyn = {HD(i * 9.0f, 150, 0, 5, MINT), TX(std::to_string(i * 13), 180, 200, F34)};
    }));
    cs.push_back(mk("dense_overlap", 120, [](int i, Frame &f) {
        std::mt19937 rng(99);
        auto U = [&](float a, float b) { return a + (b - a) * (rng() / 4294967296.0f); };
        f.key = 11;
        for (int k = 0; k < 150; k++) {
            float cx = 180 + U(-120, 120), cy = 180 + U(-120, 120);
            uint8_t a = (uint8_t)U(20, 255);
            uint16_t c = (uint16_t)rng();
            bool moving = k == i % 150;
            float d = moving ? U(-3, 3) : 0;
            switch (k % 5) {
                case 0: f.dyn.push_back(C(cx + d, cy, U(3, 40), c, a)); break;
                case 1: f.dyn.push_back(RG(cx, cy, U(5, 20), U(21, 50) + d, c, a)); break;
                case 2: { float a0 = U(0, 360); f.dyn.push_back(AR(cx, cy, U(5, 20), U(21, 50), a0 + d, a0 + U(1, 350), c, a)); break; }
                case 3: f.dyn.push_back(LN(cx, cy, cx + U(-60, 60) + d, cy + U(-60, 60), U(0.5f, 9), rng() & 1, c, a)); break;
                case 4: f.dyn.push_back(TX(moving && (i & 1) ? "b" : "a", cx + d, cy, (FontId)(rng() % 6), ALIGN_CENTER, 0, c, a)); break;
            }
        }
    }));
    cs.push_back(mk("round_edge", 60, [](int i, Frame &f) {
        withBackdrop(f);
        float t = i * 6 * (float)M_PI / 180;
        f.dyn = {C(180 + 181 * sinf(t), 180 - 181 * cosf(t), 3, PEACH), C(1 + (i % 3), 1, 6, MINT), TX("X", 2, 2, F40, ALIGN_LEFT),
                 RG(180, 180, 178.5f, 179.5f + (i % 2) * 0.4f, 0xFFFF), AR(180, 180, 176, 186, i * 5.0f, i * 5.0f + 30, CORAL),
                 LN(0, 359, 359, 0, 1 + (i % 3), true, DIM), RR(356, 356, 10, 10, 2, PEACH)};
    }));
    cs.push_back(mk("tile_boundaries", 64, [](int i, Frame &f) {
        withBackdrop(f);
        float e = (i % 4 - 1.5f) * 0.03125f;  // around 1/32 px
        f.dyn = {C(96 + e, 96, 16 + e, PEACH), RG(180, 180, 64 + e, 72 - e, MINT), LN(160 + e, 0, 160 + e, 360, 1, false),
                 AR(180, 180, 40, 48, 90 + e, 180 - e, CORAL), AR(180, 180, 16, 24, 45 + i * 0.001f, 225, 0xFFFF),
                 RR(248 + e, 248, 16, 16, 0, DIM), LN(0 + e, 8, 360, 8 + e, 16, false, 0x39E7)};
    }));

    // Efficiency-oriented motions (numbers in the table).
    cs.push_back(mk("eff_second_hand", 300, [](int i, Frame &f) {
        withBackdrop(f);
        f.dyn = {HD(300, 79, 23, 10), HD(60, 119, 23, 6), HD(roundf(i * 0.3f / 0.2f) * 0.2f, 137, 31, 2.2f, PEACH), C(180, 180, 7), C(180, 180, 3, PEACH)};
    }));
    cs.push_back(mk("eff_arc_rotate_1deg", 120, [](int i, Frame &f) {
        withBackdrop(f);
        f.dyn = {AR(180, 180, 150, 162, i * 1.0f, i * 1.0f + 30, PEACH)};
    }));
    cs.push_back(mk("eff_arc_grow_0.05deg", 200, [](int i, Frame &f) {
        withBackdrop(f);
        f.dyn = {AR(180, 180, 150, 162, 0, 120 + i * 0.05f, PEACH)};
    }));
    cs.push_back(mk("eff_arc_tiny_slice", 200, [](int i, Frame &f) {
        withBackdrop(f);
        f.dyn = {AR(180, 180, 20, 170, 0, 90 + i * 0.01f, PEACH)};
    }));
    cs.push_back(mk("eff_arc_wrap", 60, [](int i, Frame &f) {
        withBackdrop(f);
        float a = 330 + i * 1.0f;
        if (a >= 360) a -= 360;
        f.dyn = {AR(180, 180, 150, 162, a, a + 30, PEACH)};
    }));
    cs.push_back(mk("eff_disc_move_1px", 60, [](int i, Frame &f) {
        withBackdrop(f);
        f.dyn = {C(150 + i, 180, 100, PEACH)};
    }));
    cs.push_back(mk("eff_rrect_slide_1px", 60, [](int i, Frame &f) {
        withBackdrop(f);
        f.dyn = {RR(180, 120 + i, 240, 44, 12, MINT)};
    }));
    cs.push_back(mk("eff_ring_pulse", 60, [](int i, Frame &f) {
        withBackdrop(f);
        f.dyn = {RG(180, 180, 120, 130 + (i % 8) * 0.25f, PEACH)};
    }));
    cs.push_back(mk("eff_ring_move_1px", 60, [](int i, Frame &f) {
        withBackdrop(f);
        f.dyn = {RG(150 + i * 0.5f, 180, 110, 120, PEACH)};
    }));
    cs.push_back(mk("eff_text_timer", 120, [](int i, Frame &f) {
        withBackdrop(f);
        int secs = 600 - i;
        char b[16];
        snprintf(b, sizeof b, "%d:%02d", secs / 60, secs % 60);
        f.dyn = {TX(b, 180, 180, D70)};
    }));
    cs.push_back(mk("eff_color_only_ring", 20, [](int i, Frame &f) {
        withBackdrop(f);
        f.dyn = {RG(180, 180, 150, 162, i % 2 ? PEACH : MINT)};
    }));
    return cs;
}

// ---------------------------------------------------------------------------
// Random sequences
// ---------------------------------------------------------------------------
struct Gen {
    std::mt19937 rng;
    uint32_t nextKey = 100;
    std::vector<std::pair<uint32_t, std::vector<Op>>> statics;
    explicit Gen(uint32_t seed) : rng(seed) {}
    double u01() { return (rng() >> 5) * (1.0 / 134217728.0); }
    float uni(double a, double b) { return (float)(a + (b - a) * u01()); }
    int irange(int a, int b) { return a + (int)(u01() * (b - a + 1)); }
    bool chance(double p) { return u01() < p; }

    float coord() {
        double r = u01();
        if (r < 0.65) return uni(0, 360);
        if (r < 0.8) return (float)(irange(0, 45) * 8) + (chance(0.5) ? 0 : uni(-0.07, 0.07));
        if (r < 0.92) return uni(-80, 440);
        if (r < 0.98) return uni(-600, 960);
        return uni(-1200, 1560);  // within +-1400 px of the centre: beyond that int32 Q4 maths can overflow (see probes)
    }
    float radius() {
        double r = u01();
        if (r < 0.1) return uni(0, 1.5);
        if (r < 0.75) return uni(1, 180);
        if (r < 0.9) return uni(180, 400);
        if (r < 0.97) return uni(400, 1000);
        return uni(1000, 1400);  // see coord()
    }
    float angle() {
        double r = u01();
        if (r < 0.45) return uni(-360, 720);
        if (r < 0.65) return (float)(irange(-4, 8) * 45);
        if (r < 0.8) return (float)(irange(-2, 4) * 90) + uni(-0.02, 0.02);
        return irange(0, 3600) / 10.0f;
    }
    float span() {
        static const float special[] = {0, 0.004f, 0.005f, 0.01f, 0.02f, 0.05f, 0.1f, 0.5f, 1, 89.99f, 90, 179, 179.9f, 179.99f,
                                        180, 180.01f, 180.1f, 181, 270, 350, 359, 359.5f, 359.9f, 359.95f, 359.98f, 359.99f,
                                        359.995f, 360, 360.01f, 400, -1};
        double r = u01();
        if (r < 0.4) return uni(0, 360);
        if (r < 0.85) return special[irange(0, COUNT(special) - 1)];
        return uni(-5, 400);
    }
    uint16_t color() {
        static const uint16_t pal[] = {0xFFFF, 0x0000, PEACH, MINT, TRACK, DIM, CORAL, 0xF800};
        return chance(0.6) ? pal[irange(0, COUNT(pal) - 1)] : (uint16_t)irange(0, 65535);
    }
    uint8_t alpha() {
        double r = u01();
        return r < 0.6 ? 255 : r < 0.72 ? 0 : r < 0.76 ? 1 : (uint8_t)irange(0, 255);
    }
    float width() {
        double r = u01();
        if (r < 0.1) return uni(0, 0.3);
        if (r < 0.85) return uni(0.3, 12);
        if (r < 0.97) return uni(12, 60);
        return uni(-2, 0.01);
    }
    std::string text() {
        static const char *S[] = {"", " ", "0", "9", "10", "59", "1:00", "12:59", "13:00", "09:41", "Halo", "CLOCK SETUP",
                                  "Mathematics \xC2\xB7 Paper 1", "WWW", "iii", "\xC2\xB7", "\xC2", "\xFF\xFE", "a\x80\x80z",
                                  "~{}|", "Connecting to Wi-Fi...", "88:88", "-", "gjpqy", "\xC3\x84\xC3\x96", ":"};
        double r = u01();
        if (r < 0.6) return S[irange(0, COUNT(S) - 1)];
        int n = r < 0.95 ? irange(0, 12) : irange(40, 120);
        std::string s;
        for (int i = 0; i < n; i++) s += (char)irange(32, 126);
        return s;
    }
    Op op() {
        Op o;
        o.kind = (uint8_t)irange(0, K_NKINDS - 1);
        o.col = color();
        o.alpha = alpha();
        switch (o.kind) {
            case K_CIRCLE: o.f[0] = coord(); o.f[1] = coord(); o.f[2] = chance(0.05) ? uni(-2, 0) : radius(); break;
            case K_RING: {
                bool centred = chance(0.5);
                o.f[0] = centred ? 180 : coord(); o.f[1] = centred ? 180 : coord();
                o.f[2] = chance(0.1) ? (chance(0.5) ? 0 : uni(-10, 0)) : radius();
                o.f[3] = o.f[2] + (chance(0.9) ? uni(0.01, 40) : uni(-3, 0.01));
                break;
            }
            case K_ARC: {
                bool centred = chance(0.6);
                o.f[0] = centred ? 180 : coord(); o.f[1] = centred ? 180 : coord();
                o.f[2] = chance(0.15) ? 0 : uni(0, 170);
                o.f[3] = o.f[2] + (chance(0.9) ? uni(0.05, 40) : uni(-2, 0.05));
                o.f[4] = angle(); o.f[5] = o.f[4] + span();
                break;
            }
            case K_ROUNDARC: o.f[0] = uni(0, 200); o.f[1] = uni(-1, 30); o.f[2] = angle(); o.f[3] = o.f[2] + span(); break;
            case K_LINE: o.f[0] = coord(); o.f[1] = coord(); o.f[2] = chance(0.05) ? o.f[0] : coord(); o.f[3] = chance(0.05) ? o.f[1] : coord();
                o.f[4] = width(); o.round = chance(0.5); break;
            case K_HAND: o.f[0] = angle(); o.f[1] = uni(-10, 200); o.f[2] = uni(-20, 60); o.f[3] = width(); break;
            case K_TEXT: o.text = text(); o.f[0] = coord(); o.f[1] = coord(); o.font = (uint8_t)irange(0, 6);
                o.align = (uint8_t)irange(0, 2); o.track = chance(0.7) ? 0 : irange(-4, 8); break;
            case K_RRECT: o.f[0] = coord(); o.f[1] = coord(); o.f[2] = chance(0.1) ? uni(-1, 2) : uni(0, 400);
                o.f[3] = chance(0.1) ? uni(-1, 2) : uni(0, 400); o.f[4] = chance(0.1) ? uni(-10, 0) : uni(0, 120); break;
        }
        return o;
    }
    float small() { return chance(0.5) ? uni(-0.1, 0.1) : uni(-1.5, 1.5); }
    float large() { return uni(-250, 250); }
    void mutate(Op &o) {
        double r = u01();
        if (r < 0.06) { o = op(); return; }
        if (r < 0.14) { o.col = color(); return; }
        if (r < 0.22) { o.alpha = alpha(); return; }
        float *f = o.f;
        switch (o.kind) {
            case K_CIRCLE:
                switch (irange(0, 3)) {
                    case 0: f[0] += small(); f[1] += small(); break;
                    case 1: f[0] += large(); break;
                    case 2: f[2] += small(); break;
                    case 3: f[2] = radius(); break;
                }
                break;
            case K_RING:
                switch (irange(0, 5)) {
                    case 0: f[2] += small(); break;
                    case 1: f[3] += small(); break;
                    case 2: { float d = small(); f[2] += d; f[3] += d; break; }
                    case 3: f[2] = chance(0.3) ? 0 : radius(); break;
                    case 4: f[3] = f[2] + uni(-1, 60); break;
                    case 5: f[0] += small(); break;
                }
                break;
            case K_ARC:
                switch (irange(0, 11)) {
                    case 0: f[5] += small(); break;
                    case 1: f[5] += large(); break;
                    case 2: f[4] += small(); break;
                    case 3: f[4] += large(); break;
                    case 4: { float d = chance(0.5) ? small() : large(); f[4] += d; f[5] += d; break; }
                    case 5: f[5] = f[4] + span(); break;
                    case 6: { float d = f[4] >= 180 ? -360.0f : 360.0f; f[4] += d; f[5] += d; break; }
                    case 7: f[2] += small(); break;
                    case 8: f[3] += small(); break;
                    case 9: f[0] += small(); break;
                    case 10: { float sp = f[5] - f[4]; f[4] = chance(0.5) ? 359.9f + uni(0, 0.09) : uni(0, 0.1); f[5] = f[4] + sp; break; }
                    case 11: f[4] = f[5] - span(); break;
                }
                break;
            case K_ROUNDARC:
                switch (irange(0, 3)) {
                    case 0: f[3] += chance(0.5) ? small() : large(); break;
                    case 1: f[2] += chance(0.5) ? small() : large(); break;
                    case 2: f[3] = f[2] + span(); break;
                    case 3: f[1] += small(); break;
                }
                break;
            case K_LINE:
                switch (irange(0, 4)) {
                    case 0: f[0] += small(); f[1] += small(); break;
                    case 1: f[2] += small(); break;
                    case 2: f[4] = width(); break;
                    case 3: o.round = !o.round; break;
                    case 4: f[irange(0, 3)] = coord(); break;
                }
                break;
            case K_HAND:
                switch (irange(0, 3)) {
                    case 0: f[0] += small(); break;
                    case 1: f[0] += large(); break;
                    case 2: f[1] += small(); break;
                    case 3: f[3] = width(); break;
                }
                break;
            case K_TEXT:
                switch (irange(0, 8)) {
                    case 0: o.text = text(); break;
                    case 1: if (!o.text.empty()) o.text[irange(0, (int)o.text.size() - 1)] = (char)irange('0', '9'); break;
                    case 2: f[0] += small(); break;
                    case 3: f[0] = roundf(f[0]) + 0.5f + uni(-0.002, 0.002); break;
                    case 4: o.align = (uint8_t)irange(0, 2); break;
                    case 5: o.font = (uint8_t)irange(0, 6); break;
                    case 6: o.track += irange(-2, 2); break;
                    case 7: f[1] += chance(0.5) ? small() : large(); break;
                    case 8: o.text += (char)irange(32, 126); break;
                }
                break;
            case K_RRECT:
                switch (irange(0, 4)) {
                    case 0: f[0] += small(); f[1] += small(); break;
                    case 1: f[2] += small(); break;
                    case 2: f[4] += small(); break;
                    case 3: f[4] = uni(-5, 300); break;
                    case 4: f[1] += large(); break;
                }
                break;
        }
    }
    void newStatic(Frame &f, int maxOps) {
        if (!statics.empty() && chance(0.3)) {
            auto &p = statics[irange(0, (int)statics.size() - 1)];
            f.key = p.first;
            f.stat = p.second;
            return;
        }
        f.stat.clear();
        int n = chance(0.3) ? 0 : irange(0, maxOps);
        for (int i = 0; i < n; i++) f.stat.push_back(op());
        f.key = nextKey++;
        statics.push_back({f.key, f.stat});
    }
    Frame initial() {
        Frame f;
        f.bg = chance(0.8) ? 0 : color();
        bool heavy = chance(0.08);
        newStatic(f, heavy ? 90 : 25);
        int nd = irange(0, heavy ? 110 : 30);
        for (int i = 0; i < nd; i++) f.dyn.push_back(op());
        return f;
    }
    void next(Frame &f) {
        f.invalidate = false;
        double r = u01();
        int nm = r < 0.1 ? 0 : r < 0.6 ? 1 : r < 0.9 ? irange(2, 5) : (int)f.dyn.size();
        for (int k = 0; k < nm && !f.dyn.empty(); k++) mutate(f.dyn[irange(0, (int)f.dyn.size() - 1)]);
        if (chance(0.12)) {
            int n = (int)f.dyn.size();
            switch (irange(0, 4)) {
                case 0: f.dyn.insert(f.dyn.begin() + irange(0, n), op()); break;
                case 1: if (n) f.dyn.erase(f.dyn.begin() + irange(0, n - 1)); break;
                case 2: if (n > 1) std::swap(f.dyn[irange(0, n - 1)], f.dyn[irange(0, n - 1)]); break;
                case 3: f.dyn.push_back(op()); break;
                case 4: if (n) f.dyn.pop_back(); break;
            }
        }
        if (chance(0.02)) f.bg = chance(0.5) ? 0 : color();
        if (chance(0.03)) f.invalidate = true;
        if (chance(0.04)) newStatic(f, 30);
    }
};

// ---------------------------------------------------------------------------
// Probes: odd geometry, each run in a child process so a sanitizer abort is contained.
// ---------------------------------------------------------------------------
static void probeScene(std::vector<Op> a) {
    Runner *R = new Runner;
    R->quiet = true;
    R->simEvery = R->tiledEvery = 0;
    gPngBudget = 0;
    startRun(*R);
    Frame f1, f2;
    f1.dyn = a;
    f2.dyn = a;
    for (Op &o : f2.dyn) { o.f[0] += 0.37f; o.f[4] += 0.37f; }  // exercise markChanges/tileHits too
    step(*R, f1);
    step(*R, f2);
    long lit = 0;
    for (int i = 0; i < NPX; i++) lit += gPanelA[i] != 0;
    printf("      completed: %ld lit px, unmarked %ld, mismatch %ld\n", lit, R->t.unmarkedPx, R->t.mismatchPx);
    delete R;
}

static void runProbes() {
    struct Probe { const char *name; std::vector<Op> ops; };
    std::vector<Probe> probes = {
        {"circle r=2880 centred (expected OK)", {C(180, 180, 2880, PEACH)}},
        {"circle r=2900 centred", {C(180, 180, 2900, PEACH)}},
        {"circle r=3000 at x=-2700", {C(-2700, 180, 3000, PEACH)}},
        {"ring r=2940..2950 centred", {RG(180, 180, 2940, 2950, PEACH)}},
        {"circle r=2100 at (-1900,-1900) (tile distance overflow)", {C(-1900, -1900, 2100, PEACH)}},
        {"ring rIn=-5 rOut=-0.5 (circleConsts divides by zero)", {RG(180, 180, -5, -0.5f, PEACH)}},
        {"arc rIn=-3 rOut=-0.5", {AR(180, 180, -3, -0.5f, 0, 90, PEACH)}},
        {"circle r=1400 at (-1220,-1220) (expected OK)", {C(-1220, -1220, 1400, PEACH)}},
        {"arc r=2940..2950 0..90", {AR(180, 180, 2940, 2950, 0, 90, PEACH)}},
        {"line from (-40000,-30000)", {LN(-40000, -30000, 180, 180, 4, true, PEACH)}},
        {"line width 6000", {LN(0, 180, 360, 180, 6000, true, PEACH)}},
        {"rrect 100000x100000 r=50000", {RR(180, 180, 100000, 100000, 50000, PEACH)}},
        {"text x=65536+180 (int16 wrap)", {TX("88", 65536 + 180, 180, F34)}},
        {"text track=1<<30", {TX("abc", 180, 180, F16, ALIGN_CENTER, 1 << 30)}},
        {"line NaN endpoint", {LN(NAN, 0, 180, 180, 3, true, PEACH)}},
        {"circle cx=INF", {C(INFINITY, 180, 10, PEACH)}},
        {"arc angles 1e9", {AR(180, 180, 100, 120, 1e9f, 1e9f + 90, PEACH)}},
        {"flat-cap line (negative << in C++17)", {LN(60, 60, 300, 300, 5, false, PEACH)}},
        {"rrect (negative << in C++17)", {RR(180, 180, 100, 50, 10, PEACH)}},
    };
    fflush(stdout);
    for (auto &p : probes) {
        printf("  probe: %s\n", p.name);
        fflush(stdout);
        pid_t pid = fork();
        if (pid == 0) {
            probeScene(p.ops);
            fflush(stdout);
            _exit(0);
        }
        int status = 0;
        waitpid(pid, &status, 0);
        if (WIFSIGNALED(status)) printf("      -> child killed by signal %d (sanitizer abort / crash; see report above)\n", WTERMSIG(status));
        else if (WEXITSTATUS(status)) printf("      -> child exited with %d (sanitizer report above)\n", WEXITSTATUS(status));
        fflush(stdout);
    }
}

// ---------------------------------------------------------------------------
// Arc coverage accuracy vs an exact 16x16 supersampled annular sector.
// ---------------------------------------------------------------------------
static void seamReport() {
    struct A { float cx, cy, ri, ro, a0, a1; };
    const A arcs[] = {
        {180, 180, 140, 160, 0, 90},        {180, 180, 140, 160, 0, 359.9f},    {180, 180, 140, 160, 0, 359.5f},
        {180, 180, 140, 160, 0, 359},       {180, 180, 140, 160, 0, 358},       {180, 180, 140, 160, 30, 30.05f},
        {180, 180, 140, 160, 30, 30.2f},    {180, 180, 140, 160, 30, 31},       {180, 180, 0, 60, 30, 30.2f},
        {180, 180, 20, 170, 0, 180},        {180, 180, 20, 170, 0, 179.9f},     {180, 180, 20, 170, 0, 180.1f},
        {180, 180, 150, 162, 0, 359.95f},   {180, 180, 150, 162, 45, 404.9f},   {180, 180, 0, 170, 0, 359.9f},
        {180, 180, 154, 166, 0, 359.99f},   {180, 180, 154, 166, 0, 359.97f},   {180, 180, 154, 166, 0, 359.8f},
        {180, 180, 154, 166, 90, 90.03f},   {180, 180, 100, 170, 200, 200.02f},
    };
    static uint16_t buf[NPX];
    printf("arc coverage vs exact (white on black; hole = exact 100%% but drawn <75%%, ghost = exact 0%% but drawn >25%%)\n");
    for (const A &a : arcs) {
        static Scene sc;
        sc.bg = 0;
        sc.n = sc.ng = 0;
        addArc(sc, a.cx, a.cy, a.ri, a.ro, a.a0, a.a1, 0xFFFF);
        if (sc.prims[0].type == PR_NONE) continue;
        renderRef(sc, buf);
        double span = std::min(360.0, (double)a.a1 - a.a0), start = fmod((double)a.a0, 360.0);
        if (start < 0) start += 360;
        long holes = 0, ghosts = 0;
        double maxErr = 0, minInside = 1, maxOutside = 0;
        int ix = -1, iy = -1, ox = -1, oy = -1;
        int hx = -1, hy = -1, gx = -1, gy = -1, mx = -1, my = -1;
        for (int y = 0; y < W; y++)
            for (int x = 0; x < W; x++) {
                double dxc = x + 0.5 - a.cx, dyc = y + 0.5 - a.cy, dc = sqrt(dxc * dxc + dyc * dyc);
                if (dc < a.ri - 2 || dc > a.ro + 2) continue;
                int in = 0;
                for (int sy = 0; sy < 16; sy++)
                    for (int sx = 0; sx < 16; sx++) {
                        double px = x + (sx + 0.5) / 16 - a.cx, py = y + (sy + 0.5) / 16 - a.cy;
                        double d = sqrt(px * px + py * py);
                        if (d < a.ri || d > a.ro) continue;
                        double ang = atan2(px, -py) * 180 / M_PI;
                        double rel = fmod(ang - start + 720, 360);
                        if (rel <= span) in++;
                    }
                double exact = in / 256.0, drawn = ((buf[y * W + x] >> 5) & 63) / 63.0, err = fabs(drawn - exact);
                if (in == 256 && drawn < 0.75) { holes++; if (hx < 0) { hx = x; hy = y; } }
                if (in == 0 && drawn > 0.25) { ghosts++; if (gx < 0) { gx = x; gy = y; } }
                if (in == 256 && drawn < minInside) { minInside = drawn; ix = x; iy = y; }
                if (in == 0 && drawn > maxOutside) { maxOutside = drawn; ox = x; oy = y; }
                if (err > maxErr) { maxErr = err; mx = x; my = y; }
            }
        printf("  addArc(%g,%g, %g..%g, %g..%g): holes %4ld%s  ghosts %4ld%s  max |err| %.2f at (%d,%d)  darkest fully-inside px %.2f at (%d,%d)  brightest fully-outside px %.2f at (%d,%d)\n",
               a.cx, a.cy, a.ri, a.ro, a.a0, a.a1, holes, holes ? (" e.g. (" + std::to_string(hx) + "," + std::to_string(hy) + ")").c_str() : "",
               ghosts, ghosts ? (" e.g. (" + std::to_string(gx) + "," + std::to_string(gy) + ")").c_str() : "", maxErr, mx, my,
               minInside, ix, iy, maxOutside, ox, oy);
    }
    // Ring with inner radius 0 centred on a pixel centre.
    static Scene sc;
    sc.bg = 0;
    sc.n = sc.ng = 0;
    addRing(sc, 180.5f, 180.5f, 0, 20, 0xFFFF);
    renderRef(sc, buf);
    printf("  addRing(180.5,180.5, 0..20): centre pixel (180,180) = %d/63 green (a filled disc should be 63)\n", (buf[180 * W + 180] >> 5) & 63);
    sc.n = sc.ng = 0;
    addArc(sc, 180.5f, 180.5f, 0, 20, 10, 200, 0xFFFF);
    renderRef(sc, buf);
    printf("  addArc(180.5,180.5, 0..20, 10..200): centre pixel (180,180) = %d/63 green\n", (buf[180 * W + 180] >> 5) & 63);
}

// ---------------------------------------------------------------------------
static void runCases(const char *filter, Totals &all) {
    std::vector<Case> cs = targetedCases();
    int failed = 0;
    for (const Case &c : cs) {
        if (filter && !strstr(c.name.c_str(), filter)) continue;
        Runner *R = new Runner;
        R->name = c.name;
        startRun(*R);
        gSceneOverflow = 0;
        for (const Frame &f : c.frames) step(*R, f);
        printTotals(c.name.c_str(), R->t);
        if (R->t.bad()) {
            failed++;
            if (!R->failures.empty()) {
                const Failure &fl = R->failures.front();
                if (fl.unmarked) minimize(fl, c.name + " frame " + std::to_string(fl.frame));
            }
        }
        all.add(R->t);
        delete R;
    }
    printf("targeted cases: %d failed\n", failed);
}

static void runRandom(int count, uint32_t seed0, Totals &all) {
    // Shared with the child processes: its totals and the minimisation budget.
    struct Shared { Totals t; int minimized; };
    Shared *sh = (Shared *)mmap(nullptr, sizeof(Shared), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANON, -1, 0);
    sh->minimized = 0;
    int failedSeqs = 0;
    auto t0 = std::chrono::steady_clock::now();
    for (int k = 0; k < count; k++) {
        uint32_t seed = seed0 + k;
        sh->t = Totals();
        fflush(stdout);
        fflush(stderr);
        pid_t pid = fork();
        if (pid == 0) {
            Gen g(seed);
            Runner *R = new Runner;
            R->name = "random_" + std::to_string(seed);
            R->quiet = true;
            R->rng.seed(seed);
            startRun(*R);
            Frame f = g.initial();
            int frames = g.irange(20, 70);
            for (int i = 0; i < frames; i++) {
                step(*R, f);
                sh->t = R->t;  // keep partial totals if a later frame aborts
                g.next(f);
            }
            sh->t = R->t;
            if (R->t.bad()) {
                printTotals(R->name.c_str(), R->t);
                if (!R->failures.empty() && sh->minimized < 12) {
                    const Failure &fl = R->failures.front();
                    printf("    first miss at frame %d: %ld unmarked px, %ld mismatched px\n", fl.frame, fl.unmarked, fl.mismatch);
                    if (fl.unmarked) {
                        sh->minimized++;
                        minimize(fl, R->name + " frame " + std::to_string(fl.frame));
                    }
                }
            }
            fflush(stdout);
            _exit(0);
        }
        int status = 0;
        waitpid(pid, &status, 0);
        Totals t = sh->t;
        if (WIFSIGNALED(status) || WEXITSTATUS(status)) {
            t.crashes++;
            printf("random_%u: child %s %d (sanitizer report above); rerun with: tools/sim/out/stress random 1 %u\n", seed,
                   WIFSIGNALED(status) ? "killed by signal" : "exited with", WIFSIGNALED(status) ? WTERMSIG(status) : WEXITSTATUS(status), seed);
        }
        if (t.bad()) failedSeqs++;
        all.add(t);
    }
    munmap(sh, sizeof(Shared));
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    printf("random: %d sequences from seed %u, %d failed (%.1f s)\n", count, seed0, failedSeqs, secs);
}

int main(int argc, char **argv) {
    mkdir(kOutDir, 0755);
    initMask();
    setvbuf(stdout, nullptr, _IOLBF, 0);
    std::string mode = argc > 1 ? argv[1] : "all";
#ifndef STRICT_SHIFT
    printf("(negative left shifts inside render.h are not trapped; build with -DSTRICT_SHIFT to trap them)\n");
#endif
    Totals all;
    if (mode == "probes") {
        runProbes();
        return 0;
    }
    if (mode == "seams") {
        seamReport();
        return 0;
    }
    if (mode == "cases" || mode == "all") runCases(mode == "cases" && argc > 2 ? argv[2] : nullptr, all);
    if (mode == "random" || mode == "all") {
        int n = mode == "random" && argc > 2 ? atoi(argv[2]) : 150;
        uint32_t seed = mode == "random" && argc > 3 ? (uint32_t)strtoul(argv[3], nullptr, 10) : 1;
        runRandom(n, seed, all);
    }
    printTotals("ALL", all);
    printf("window rect errors %ld, window pixel-count errors %ld, gOutOfWindow %ld\n", gBadRectTotal, gBadCountTotal, gOutOfWindow);
    return all.bad() ? 1 : 0;
}
