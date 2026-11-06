// Learn — colour-matched hands and the time in words ("quarter to one").
//
// Static layer: cream rim, minute numbers and ticks, the 12 coloured hour discs.
// Dynamic layer (paint order): hour hand, minute hand, the white centre disc and
// its rim on top of the hands, then "It's" and the three phrase lines.
//
// Cost control: both hands snap to 0.25 degree steps computed in integers, so the
// minute hand moves every 2.5 s (0.55 px at its tip) and the hour hand every 30 s,
// half a step out of phase so the two never move in the same frame. Their trig is
// only redone when the snapped angle changes. The hidden inner end of each hand
// starts just inside the centre disc instead of at the centre, so a move doesn't
// dirty the tiles the disc covers anyway. The phrase lines are three fixed text
// primitives whose strings change once every five minutes.
#pragma once
#include "faces_common.h"

static const uint16_t LEARN_BG = HEX565(0xfff8ef);
static const uint16_t L_RIM = HEX565(0xf3e5d3), L_NUM = HEX565(0xa8906f), L_TICK = HEX565(0xdcc9b0),
                      L_MIN = HEX565(0x3d7fc1), L_HOUR = HEX565(0x7d5fc0), L_DISCNUM = HEX565(0x4a4238),
                      L_CRIM = HEX565(0xf0e2cf);
// Discs cycle blue, red, purple: the three colours children rank as favourites.
static const uint16_t L_DISC[3] = {HEX565(0x8db8e8), HEX565(0xf0a3a3), HEX565(0xb9a3ec)};

static const char *const LEARN_MIN_WORDS[12] = {"", "five", "ten", "quarter", "twenty", "twenty-five", "half",
                                                "twenty-five", "twenty", "quarter", "ten", "five"};
static const char *const LEARN_HOUR_WORDS[12] = {"twelve", "one", "two", "three", "four", "five",
                                                 "six", "seven", "eight", "nine", "ten", "eleven"};

// Centre disc: white r58 with a rim out to r61. Hands start at LEARN_HAND_IN, where
// even their round inner cap and anti-aliasing stay under the rim's solid part.
static const float LEARN_DISC_R = 58, LEARN_CRIM_R = 61, LEARN_HAND_IN = 54;

static void learnStatic(Scene &s) {
    addRing(s, SCR_C, SCR_C, 176, 180, L_RIM);
    for (int i = 0; i < 60; i++) {
        float x, y;
        if (i % 5 == 0) {
            polarPt(166, i * 6, x, y);
            char b[3];
            snprintf(b, sizeof(b), "%d", i ? i : 60);
            addText(s, b, x, y, F13, L_NUM);
        } else {
            float x1, y1;
            polarPt(168, i * 6, x, y);
            polarPt(173, i * 6, x1, y1);
            addLine(s, x, y, x1, y1, 2, L_TICK, true);
        }
    }
    for (int n = 1; n <= 12; n++) {
        float x, y;
        polarPt(128, n * 30, x, y);
        addCircle(s, x, y, 22, L_DISC[(n - 1) % 3]);
        char b[3];
        snprintf(b, sizeof(b), "%d", n);
        addText(s, b, x, y, F26, L_DISCNUM);
    }
}

// A hand's geometry for one snapped angle (index in quarter degrees).
struct LearnHand {
    int idx = -1;
    float x0, y0, x1, y1;
};

static void learnHandAt(LearnHand &h, int idx, float len) {
    if (idx == h.idx) return;
    h.idx = idx;
    float deg = idx * 0.25f;
    polarPt(LEARN_HAND_IN, deg, h.x0, h.y0);
    polarPt(len, deg, h.x1, h.y1);
}

static void learnDynamic(Scene &s, const FrameTime &ft) {
    const struct tm &t = ft.local;

    // Minute hand: 6 deg per minute = 1 quarter degree per 2.5 s, rounded to nearest.
    int32_t msIntoHour = (t.tm_min * 60 + t.tm_sec) * 1000 + ft.ms;
    int minIdx = ((msIntoHour + 1250) / 2500) % 1440;
    // Hour hand: 30 deg per hour = 1 quarter degree per 30 s, rounded to nearest.
    int32_t secInto12 = (t.tm_hour % 12) * 3600 + t.tm_min * 60 + t.tm_sec;
    int hourIdx = ((secInto12 + 15) / 30) % 1440;

    static LearnHand hourHand, minHand;
    learnHandAt(hourHand, hourIdx, 88);
    learnHandAt(minHand, minIdx, 119);

    addLine(s, hourHand.x0, hourHand.y0, hourHand.x1, hourHand.y1, 13, L_HOUR, true);
    addCircle(s, hourHand.x1, hourHand.y1, 8, L_HOUR);
    addLine(s, minHand.x0, minHand.y0, minHand.x1, minHand.y1, 9, L_MIN, true);
    addCircle(s, minHand.x1, minHand.y1, 6, L_MIN);

    addCircle(s, SCR_C, SCR_C, LEARN_CRIM_R, L_CRIM);
    addCircle(s, SCR_C, SCR_C, LEARN_DISC_R, C_WHITE);
    addText(s, "It's", SCR_C, SCR_C - 38, F16, L_NUM);

    // Nearest five minutes; "to" phrases name the next hour.
    int q = (t.tm_min + 2) / 5;  // 0..12
    int idx = q % 12;
    int hh = t.tm_hour % 12;
    if (q >= 7) hh = (hh + 1) % 12;
    if (idx == 0) {
        addText(s, LEARN_HOUR_WORDS[hh], SCR_C, SCR_C - 14, F21, L_HOUR);
        addText(s, "o'clock", SCR_C, SCR_C + 8, F16, L_NUM);
        addText(s, "", SCR_C, SCR_C + 32, F21, L_HOUR);
    } else {
        addText(s, LEARN_MIN_WORDS[idx], SCR_C, SCR_C - 14, F21, L_MIN);
        addText(s, idx <= 6 ? "past" : "to", SCR_C, SCR_C + 8, F16, L_NUM);
        addText(s, LEARN_HOUR_WORDS[hh], SCR_C, SCR_C + 32, F21, L_HOUR);
    }
}
