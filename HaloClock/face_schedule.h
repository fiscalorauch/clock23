// Schedule — the school day as a ring of coloured arcs, with what's on now.
//
// The day (first start to last end) maps to the whole circle, clockwise from 12.
// Static layer (settings only): one round-ended arc per period in PASTELS order
// (sorted by start), always MAX_PERIODS slots of 3 primitives with unused slots
// hidden, so the dynamic primitives keep their indices when the timetable is
// edited; then the "NOW" label.
// Dynamic layer: three text lines and the marker dot. The strings are rebuilt
// only when what they say changes. The marker position is integer maths into
// 1800 steps (0.2 degrees, ~0.55 px on the ring), so it glides in sub-pixel
// steps, costs nothing between them, and trig only runs when it moves.
#pragma once
#include "faces_common.h"

static const float SCH_R = 158.0f, SCH_W = 13.0f, SCH_GAP = 2.5f;
static const int SCH_STEPS = 1800;   // marker positions round the ring
static const int SCH_MAIN_W = 260;   // widest main line (y 162) that stays clear of the ring
static const int SCH_NEXT_W = 228;   // widest "Next:" line (y 247)

static struct {
    uint32_t rev;
    bool ready;
    int n;
    Period p[MAX_PERIODS];     // valid periods, sorted by start
    int32_t start, span;       // minutes after midnight, minutes (>= 1)
    int32_t key[4];            // phase, cur, next, minutes left of the text below
    bool textValid;
    char main[40], sub[24], next[64];
    FontId mainFont;
    uint16_t mainCol;
    int markerStep;
    float mx, my;
} sch;

static void schPrepare() {
    if (sch.ready && sch.rev == gSettingsRev) return;
    sch.ready = true;
    sch.rev = gSettingsRev;
    sch.n = 0;
    int count = S.numPeriods < MAX_PERIODS ? S.numPeriods : MAX_PERIODS;
    for (int i = 0; i < count; i++) {
        const Period &p = S.periods[i];
        if (p.end <= p.start || p.end > 1440) continue;
        Period &q = sch.p[sch.n++];
        q = p;
        q.name[sizeof(q.name) - 1] = 0;
    }
    for (int i = 1; i < sch.n; i++)  // insertion sort by start (stable)
        for (int j = i; j > 0 && sch.p[j].start < sch.p[j - 1].start; j--) {
            Period t = sch.p[j];
            sch.p[j] = sch.p[j - 1];
            sch.p[j - 1] = t;
        }
    int32_t end = 0;
    for (int i = 0; i < sch.n; i++) end = imax(end, sch.p[i].end);
    sch.start = sch.n ? sch.p[0].start : 0;
    sch.span = imax(1, end - sch.start);
    sch.textValid = false;
    sch.markerStep = -1;
}

// Writes prefix + src + suffix into out, cutting src at a character boundary
// (with "...") until the whole line fits maxW px in font f.
static void schFit(char *out, size_t n, const char *prefix, const char *src, const char *suffix, FontId f, int maxW) {
    char name[32];
    strlcpy(name, src, sizeof(name));
    size_t len = strlen(name);
    bool cut = false;
    for (;;) {
        snprintf(out, n, "%s%s%s%s", prefix, name, cut ? "..." : "", suffix);
        if (len == 0 || textWidth(out, f) <= maxW) return;
        do len--; while (len > 0 && ((uint8_t)name[len] & 0xC0) == 0x80);
        while (len > 0 && name[len - 1] == ' ') len--;
        name[len] = 0;
        cut = true;
    }
}

// The big line: shrinks through F40, F34 and F26, then truncates.
static FontId schFitMain(char *out, size_t n, const char *src, FontId biggest = F40) {
    static const FontId sizes[] = {F40, F34, F26};
    for (FontId f : sizes) {
        if (f > biggest) continue;  // FontId runs small to large
        if (textWidth(src, f) <= SCH_MAIN_W) {
            strlcpy(out, src, n);
            return f;
        }
    }
    schFit(out, n, "", src, "", F26, SCH_MAIN_W);
    return F26;
}

static void schUpdateText(int32_t nowSec) {
    // phase: 0 before school, 1 in a period, 2 home time, 3 break, 4 no timetable
    int phase, cur = -1, next = -1;
    int32_t left = 0;
    if (!sch.n) {
        phase = 4;
    } else if (nowSec < sch.start * 60) {
        phase = 0;
        next = 0;
    } else if (nowSec >= (sch.start + sch.span) * 60) {
        phase = 2;
    } else {
        for (int i = 0; i < sch.n; i++) {
            if (nowSec >= sch.p[i].start * 60 && nowSec < sch.p[i].end * 60) cur = i;  // latest started
            if (next < 0 && sch.p[i].start * 60 > nowSec) next = i;
        }
        phase = cur >= 0 ? 1 : 3;
        int32_t until = cur >= 0 ? sch.p[cur].end * 60 : (next >= 0 ? sch.p[next].start * 60 : nowSec);
        left = imax(1, (until - nowSec + 59) / 60);
    }
    if (sch.textValid && sch.key[0] == phase && sch.key[1] == cur && sch.key[2] == next && sch.key[3] == left) return;
    sch.textValid = true;
    sch.key[0] = phase; sch.key[1] = cur; sch.key[2] = next; sch.key[3] = left;

    sch.mainCol = C_TEXT;
    sch.sub[0] = sch.next[0] = 0;
    switch (phase) {
        case 0: sch.mainFont = schFitMain(sch.main, sizeof(sch.main), "Before school"); break;
        case 2: sch.mainFont = schFitMain(sch.main, sizeof(sch.main), "Home time"); break;
        case 3: sch.mainFont = schFitMain(sch.main, sizeof(sch.main), "Break"); break;
        case 4: sch.mainFont = schFitMain(sch.main, sizeof(sch.main), "No timetable", F34); break;
        default:
            sch.mainFont = schFitMain(sch.main, sizeof(sch.main), sch.p[cur].name);
            sch.mainCol = PASTELS[cur % 6];
            break;
    }
    if (phase == 1 || phase == 3) snprintf(sch.sub, sizeof(sch.sub), "%ld min left", (long)left);
    if (next >= 0) {
        char tail[16];
        snprintf(tail, sizeof(tail), " \xC2\xB7 %02d:%02d", sch.p[next].start / 60, sch.p[next].start % 60);
        schFit(sch.next, sizeof(sch.next), phase == 0 ? "First: " : "Next: ", sch.p[next].name, tail, F16, SCH_NEXT_W);
    } else if (phase == 1) {
        strlcpy(sch.next, "Next: Home time", sizeof(sch.next));
    } else if (phase == 4) {
        strlcpy(sch.next, "Add periods on the dashboard", sizeof(sch.next));
    }
}

static void scheduleStatic(Scene &s) {
    sch.ready = false;
    schPrepare();
    // An empty timetable still shows where the day would go.
    addRing(s, SCR_C, SCR_C, SCH_R - SCH_W / 2, SCH_R + SCH_W / 2, C_TRACK, sch.n ? 0 : 255);
    float degPerMin = 360.0f / sch.span;
    for (int i = 0; i < MAX_PERIODS; i++) {
        if (i >= sch.n) {
            addRoundArc(s, SCH_R, SCH_W, 0, 0, C_BLACK, 0);
            continue;
        }
        float a0 = (sch.p[i].start - sch.start) * degPerMin + SCH_GAP;
        float a1 = (sch.p[i].end - sch.start) * degPerMin - SCH_GAP;
        if (a1 - a0 < 0.1f) {  // too short for an arc between the gaps: a dot in the middle
            float mid = (a0 + a1) / 2;
            a0 = mid - 0.05f;
            a1 = mid + 0.05f;
        }
        addRoundArc(s, SCH_R, SCH_W, a0, a1, PASTELS[i % 6]);
    }
    addText(s, "NOW", SCR_C, 124, F13, C_FAINT, ALIGN_CENTER, 4);
}

static void scheduleDynamic(Scene &s, const FrameTime &ft) {
    schPrepare();
    int32_t nowSec = ft.local.tm_hour * 3600 + ft.local.tm_min * 60 + ft.local.tm_sec;
    schUpdateText(nowSec);
    addText(s, sch.main, SCR_C, 162, sch.mainFont, sch.mainCol);
    addText(s, sch.sub, SCR_C, 202, F16, C_DIM);
    addText(s, sch.next, SCR_C, 247, F16, C_FAINT);

    int step = 0;
    if (sch.n) {
        int64_t spanMs = (int64_t)sch.span * 60000;
        int64_t t = (int64_t)nowSec * 1000 + ft.ms - (int64_t)sch.start * 60000;
        t = t < 0 ? 0 : t > spanMs ? spanMs : t;
        step = (int)((t * SCH_STEPS + spanMs / 2) / spanMs);
    }
    if (step != sch.markerStep) {
        sch.markerStep = step;
        polarPt(SCH_R, step * 360.0f / SCH_STEPS, sch.mx, sch.my);
    }
    uint8_t a = sch.n ? 255 : 0;
    addCircle(s, sch.mx, sch.my, 12, C_BLACK, a);
    addCircle(s, sch.mx, sch.my, 9, C_WHITE, a);
}
