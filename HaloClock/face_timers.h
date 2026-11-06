// Exam and Focus — countdown faces.
//
// Exam: a white ring drains clockwise from 12 o'clock as time runs out and
// turns coral in the last five minutes. Focus: a mint ring drains the same way
// inside a slowly breathing halo.
//
// Keeping frames cheap:
// - The progress angle is snapped to 0.1 degree (about 0.3 px at the rim), so a
//   change only repaints the few tiles round the arc's moving end and cap.
// - The arc is two half arcs split at 6 o'clock. Neither is ever a "full ring"
//   or wider than 180 degrees, so draining never switches arc kind (which would
//   repaint the whole ring). Both split edges fall exactly on pixel boundaries,
//   so there's no seam.
// - The arc primitives are cached and replayed while the angle and colour are
//   unchanged, so the trig behind them (software floats on the C3) only runs
//   when the arc actually moves.
// - The countdown only changes glyphs once a second, the status line only when
//   its text changes ("Finishes at" is formatted once per start).
// - The halo breathes in HALO_STEPS radius/opacity steps. Radius and opacity
//   change together, in one frame, and a step never lands on the same frame as
//   a countdown tick, so the thin ring band is the only big repaint.
//
// Everything here is a pure function of (time, settings): the simulator redraws
// frames from scratch to compare, so no hidden state may change the picture.
#pragma once
#include "faces_common.h"

static const uint16_t FOCUS_TRACK = HEX565(0x1d2a24);

static const int TIMER_ARC_STEPS = 3600;  // progress angle resolution: 0.1 degree

// Where a countdown is.
struct TimerNow {
    uint8_t phase;   // 0 ready (not started), 1 running, 2 done
    long secs;       // whole seconds shown, rounded up so 00:00 means time's up
    int32_t angC;    // progress angle in centidegrees, snapped (0 only when done)
};

static TimerNow timerNow(int64_t endSec, uint16_t minutes, int64_t nowMs) {
    int64_t dur = (int64_t)(minutes ? minutes : 1) * 60000;
    int64_t left = endSec > 0 ? endSec * 1000 - nowMs : dur;
    if (left < 0) left = 0;
    if (left > dur) left = dur;
    TimerNow t;
    t.phase = endSec <= 0 ? 0 : left > 0 ? 1 : 2;
    t.secs = (long)((left + 999) / 1000);
    t.angC = (int32_t)((left * TIMER_ARC_STEPS + dur - 1) / dur) * (36000 / TIMER_ARC_STEPS);
    return t;
}

// Progress arc on radius r with stroke w and round ends: always 4 primitives
// (two half arcs, start cap, end cap), replayed from the cache when unchanged.
struct ArcCache {
    bool valid;
    uint32_t key;
    Prim prims[4];
};

static void addProgress(Scene &s, ArcCache &c, float r, float w, int32_t angC, uint16_t col) {
    uint32_t key = (uint32_t)angC | (uint32_t)col << 16;
    if (c.valid && c.key == key && s.n + 4 <= MAX_PRIMS) {
        memcpy(s.prims + s.n, c.prims, sizeof(c.prims));
        s.n += 4;
        return;
    }
    int first = s.n;
    float ang = angC / 100.0f;
    uint8_t a = angC > 0 ? 255 : 0;
    addArc(s, SCR_C, SCR_C, r - w / 2, r + w / 2, 0, angC < 18000 ? ang : 180.0f, col, a);
    addArc(s, SCR_C, SCR_C, r - w / 2, r + w / 2, 180.0f, ang, col, angC > 18000 ? 255 : 0);
    float x, y;
    polarPt(r, 0, x, y);
    addCircle(s, x, y, w / 2, col, a);
    polarPt(r, ang, x, y);
    addCircle(s, x, y, w / 2, col, a);
    if (s.n == first + 4) {
        memcpy(c.prims, s.prims + first, sizeof(c.prims));
        c.key = key;
        c.valid = true;
    }
}

static void addCountdown(Scene &s, long secs, uint16_t col) {
    char buf[12];
    fmtMMSS(buf, sizeof(buf), secs);
    addText(s, buf, SCR_C, SCR_C, D70, col);
}

// Uppercases src into dst so it fits maxW px: tracking shrinks first, then the
// end is cut (whole UTF-8 characters, no dangling space or dot) and "..." added.
static void fitUpper(char *dst, size_t n, const char *src, FontId font, int &track, int maxW) {
    char full[64];
    upperCopy(full, src, sizeof(full));
    while (track > 0 && textWidth(full, font, track) > maxW) track--;
    size_t len = strlen(full);
    strlcpy(dst, full, n);
    if (len < n && textWidth(dst, font, track) <= maxW) return;
    while (len > 0) {
        do len--; while (len > 0 && ((uint8_t)full[len] & 0xC0) == 0x80);
        size_t cut = len;
        for (;;) {
            if (cut > 0 && full[cut - 1] == ' ') cut--;
            else if (cut > 1 && (uint8_t)full[cut - 2] == 0xC2 && (uint8_t)full[cut - 1] == 0xB7) cut -= 2;
            else break;
        }
        if (cut + 4 > n) continue;
        memcpy(dst, full, cut);
        memcpy(dst + cut, "...", 4);
        if (textWidth(dst, font, track) <= maxW) return;
    }
    dst[0] = 0;
}

// ===========================================================================
// Exam
// ===========================================================================
static const float EXAM_R = 167, EXAM_W = 8;  // band 163..171, same as the track
static const int EXAM_SUBJECT_W = 260;        // stays well inside the track at y 112

static void examStatic(Scene &s) {
    addRing(s, SCR_C, SCR_C, EXAM_R - EXAM_W / 2, EXAM_R + EXAM_W / 2, C_TRACK);
    char up[48];
    int track = 2;
    fitUpper(up, sizeof(up), S.examSubject, F16, track, EXAM_SUBJECT_W);
    addText(s, up, SCR_C, 112, F16, C_DIM, ALIGN_CENTER, track);
    addText(s, "TIME REMAINING", SCR_C, 270, F13, C_FAINT, ALIGN_CENTER, 3);
}

static void examDynamic(Scene &s, const FrameTime &ft) {
    TimerNow t = timerNow(S.examEnd, S.examMin, ft.realMs);
    bool warn = t.phase == 1 && t.secs <= 300;  // coral from 05:00 down

    static ArcCache arc;
    addProgress(s, arc, EXAM_R, EXAM_W, t.angC, warn ? C_CORAL : C_WHITE);
    addCountdown(s, t.secs, C_TEXT);

    // Status line, formatted only when what it says changes.
    static struct { bool valid; uint8_t phase; int64_t end; int32_t offset; uint32_t rev; char text[40]; } st;
    if (!st.valid || st.phase != t.phase || st.end != S.examEnd || st.offset != S.testOffset || st.rev != gSettingsRev) {
        if (t.phase == 0) {
            strlcpy(st.text, "Ready \xC2\xB7 start from dashboard", sizeof(st.text));
        } else if (t.phase == 1) {
            time_t end = (time_t)(S.examEnd + S.testOffset);
            struct tm e;
            localtime_r(&end, &e);
            char t12[12];
            fmt12(t12, sizeof(t12), e.tm_hour, e.tm_min);
            snprintf(st.text, sizeof(st.text), "Finishes at %s", t12);
        } else {
            strlcpy(st.text, "Time's up", sizeof(st.text));
        }
        st.valid = true;
        st.phase = t.phase;
        st.end = S.examEnd;
        st.offset = S.testOffset;
        st.rev = gSettingsRev;
    }
    addText(s, st.text, SCR_C, 245, F16, t.phase == 2 ? C_CORAL : C_DIM);
}

// ===========================================================================
// Focus
// ===========================================================================
static const float FOCUS_R = 151.5f, FOCUS_W = 9;  // band 147..156, same as the track
static const int FOCUS_LABEL_W = 194;             // stays inside the halo's smallest radius at y 119

// Halo (prototype: r 138 + 26 * br, opacity .3 + .3 * br, scaled to 360 px) kept
// clear of the text inside it and of the progress ring outside it.
static const int HALO_STEPS = 42;                 // 1 px radius steps
static const float HALO_R_MIN = 124, HALO_R_MAX = 145, HALO_HALF_W = 0.75f;
static const int HALO_PERIOD_MS = 5000;

// Breathing step at a real time: br = (1 - cos(2 pi t / 5 s)) / 2, rounded to a
// step. The edges (ms into the rising half where each step begins) are worked
// out once, so a frame needs no float maths to find its step.
static int haloStep(int64_t realMs) {
    static int16_t edge[HALO_STEPS + 1];
    static bool ready = false;
    if (!ready) {
        for (int k = 1; k <= HALO_STEPS; k++) {
            float br = (k - 0.5f) / HALO_STEPS;
            edge[k] = (int16_t)lroundf(acosf(1.0f - 2.0f * br) / (2.0f * (float)M_PI) * HALO_PERIOD_MS);
        }
        ready = true;
    }
    int ph = (int)(realMs % HALO_PERIOD_MS);
    if (ph < 0) ph += HALO_PERIOD_MS;
    if (ph > HALO_PERIOD_MS / 2) ph = HALO_PERIOD_MS - ph;
    int k = 0;
    while (k < HALO_STEPS && ph >= edge[k + 1]) k++;
    return k;
}

static void focusStatic(Scene &s) {
    addRing(s, SCR_C, SCR_C, FOCUS_R - FOCUS_W / 2, FOCUS_R + FOCUS_W / 2, FOCUS_TRACK);
    char up[40];
    int track = 3;
    fitUpper(up, sizeof(up), S.focusLabel, F16, track, FOCUS_LABEL_W);
    addText(s, up, SCR_C, 119, F16, C_DIM, ALIGN_CENTER, track);
}

static void focusDynamic(Scene &s, const FrameTime &ft) {
    TimerNow t = timerNow(S.focusEnd, S.focusMin, ft.realMs);

    // The halo holds still from HALO_HOLD_MS before each countdown tick until
    // HALO_HOLD_MS after it, so a tick frame and the frame before it always show
    // the same step: the ring band and the digits are never repainted together.
    static const int HALO_HOLD_MS = 40;  // a bit over one frame
    int64_t haloMs = ft.realMs;
    if (S.focusEnd > 0 && ft.realMs < S.focusEnd * 1000 + HALO_HOLD_MS) {  // running, or the final tick
        int sinceTick = (int)((ft.realMs - S.focusEnd * 1000) % 1000);
        if (sinceTick < 0) sinceTick += 1000;
        if (sinceTick < HALO_HOLD_MS) haloMs -= sinceTick + HALO_HOLD_MS;
        else if (sinceTick >= 1000 - HALO_HOLD_MS) haloMs -= sinceTick - (1000 - HALO_HOLD_MS);
    }
    int k = haloStep(haloMs);
    float r = HALO_R_MIN + (HALO_R_MAX - HALO_R_MIN) * k / HALO_STEPS;
    uint8_t alpha = (uint8_t)(77 + 76 * k / HALO_STEPS);  // 30 % .. 60 %
    addRing(s, SCR_C, SCR_C, r - HALO_HALF_W, r + HALO_HALF_W, C_MINT, alpha);

    static ArcCache arc;
    addProgress(s, arc, FOCUS_R, FOCUS_W, t.angC, C_MINT);
    addCountdown(s, t.secs, C_MINT);

    static const char *const kLines[3] = {"Ready to start", "Quiet focus time", "Focus time complete"};
    addText(s, kLines[t.phase], SCR_C, 241, F16, t.phase == 1 ? C_FAINT : C_DIM);
}
