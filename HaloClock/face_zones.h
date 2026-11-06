// Zones — local time in white, a coloured hour hand per city, and a legend of
// pills (Local plus each city's 24 h time) painted over the hands.
//
// Static layer: the 12 hour ticks and the 12/3/6/9 numerals.
// Dynamic layer, in paint order: each city hand (stem, dot, initial), the local
// hour and minute hands, the centre dot, then the four legend pills.
//
// Keeping frames cheap:
//  - Local hands are snapped to 0.25 deg with integer maths: the minute hand moves
//    every 2.5 s, the hour hand at 15 and 45 s past each minute.
//  - City hands and the legend times only change on the minute (at :00, when the
//    local hands never move). City times come from cityTime() (setenv/tzset), so
//    they are only fetched when the displayed minute or the settings change.
//  - Non-text primitives are rebuilt only when their inputs change and are copied
//    otherwise, so an ordinary frame does almost no (software) float maths.
#pragma once
#include "faces_common.h"

static const uint16_t Z_PILL = HEX565(0x1c1c1e), Z_LABEL = HEX565(0xd6d6da), Z_INK = HEX565(0x141416);
static const float Z_CITY_LEN[NUM_ZONES] = {115, 101, 86};

// Legend: 2 x 2 pills. 132 px wide rather than the old 142 px, which covered the
// inner ends of the 4 and 8 o'clock ticks; this leaves ~3 px of black around
// them. With 24 h times the longest label ("Los Angeles") keeps a 12 px gap.
static const float Z_PILL_W = 132, Z_PILL_H = 23, Z_PILL_R = 11, Z_PILL_DX = 68;
static const float Z_PILL_Y[2] = {250, 279};

static struct {
    bool inited, citiesReady;
    uint32_t rev;                       // settings revision the city data belongs to
    int32_t minuteKey;                  // local minute the city data belongs to
    char localTxt[12];
    char cityTxt[NUM_ZONES][12];
    char initial[NUM_ZONES][2];
    int16_t cityMin[NUM_ZONES];         // city minutes into the half day (0..719): hand angle
    int16_t builtMin[NUM_ZONES];        // cityMin the cached stem/dot were built for
    float dotX[NUM_ZONES], dotY[NUM_ZONES];
    Prim stem[NUM_ZONES], dot[NUM_ZONES];
    int16_t hourStep, minStep;          // 0.25 deg steps the cached local hands were built for
    Prim hourHand, minHand;
    bool fixedReady;
    Prim centre, pill[4], pillDot[4];
} zonesState;

// Non-text primitives don't point into the scene's glyph storage, so one built
// on an earlier frame can be copied back in as it is.
static void zonesSave(const Scene &s, Prim &slot) {
    if (s.n) slot = s.prims[s.n - 1];
}
static void zonesCopy(Scene &s, const Prim &slot) {
    Prim *p = primAdd(s, PR_NONE, 0, 0);
    if (p) *p = slot;
}

static uint8_t zonesCity(int i) { return S.zones[i] < kNumCities ? S.zones[i] : 0; }

static void zonesStatic(Scene &s) {
    for (int i = 0; i < 12; i++) {
        float x0, y0, x1, y1;
        polarPt(158, i * 30, x0, y0);
        polarPt(171, i * 30, x1, y1);
        addLine(s, x0, y0, x1, y1, 4, C_TEXT);
    }
    addText(s, "12", SCR_C, 40, F21, C_DIM);
    addText(s, "3", 320, SCR_C, F21, C_DIM);
    addText(s, "6", SCR_C, 320, F21, C_DIM);
    addText(s, "9", 40, SCR_C, F21, C_DIM);
}

// Fetches every city's time; only called when the local minute or the settings change.
static void zonesUpdateCities(const FrameTime &ft, int32_t key) {
    zonesState.citiesReady = true;
    zonesState.rev = gSettingsRev;
    zonesState.minuteKey = key;
    snprintf(zonesState.localTxt, sizeof(zonesState.localTxt), "%02d:%02d", ft.local.tm_hour, ft.local.tm_min);
    for (int i = 0; i < NUM_ZONES; i++) {
        uint8_t c = zonesCity(i);
        struct tm t;
        cityTime(c, t);
        zonesState.cityMin[i] = (t.tm_hour % 12) * 60 + t.tm_min;
        snprintf(zonesState.cityTxt[i], sizeof(zonesState.cityTxt[i]), "%02d:%02d", t.tm_hour, t.tm_min);
        zonesState.initial[i][0] = (char)toupper((uint8_t)kCities[c].label[0]);
        zonesState.initial[i][1] = 0;
    }
}

static void zonesDynamic(Scene &s, const FrameTime &ft) {
    if (!zonesState.inited) {
        zonesState.inited = true;
        zonesState.hourStep = zonesState.minStep = -1;
        for (int i = 0; i < NUM_ZONES; i++) zonesState.builtMin[i] = -1;
    }
    int32_t key = ((ft.local.tm_year * 366 + ft.local.tm_yday) * 24 + ft.local.tm_hour) * 60 + ft.local.tm_min;
    if (!zonesState.citiesReady || zonesState.rev != gSettingsRev || zonesState.minuteKey != key) zonesUpdateCities(ft, key);

    // City hands: stem (slightly translucent, like the prototype), dot, initial.
    for (int i = 0; i < NUM_ZONES; i++) {
        uint16_t col = PASTELS[i % 6];
        if (zonesState.builtMin[i] != zonesState.cityMin[i]) {
            float deg = zonesState.cityMin[i] * 0.5f;
            addHand(s, deg, Z_CITY_LEN[i] - 7, 13, 5, col, 230);
            zonesSave(s, zonesState.stem[i]);
            polarPt(Z_CITY_LEN[i], deg, zonesState.dotX[i], zonesState.dotY[i]);
            addCircle(s, zonesState.dotX[i], zonesState.dotY[i], 10, col);
            zonesSave(s, zonesState.dot[i]);
            zonesState.builtMin[i] = zonesState.cityMin[i];
        } else {
            zonesCopy(s, zonesState.stem[i]);
            zonesCopy(s, zonesState.dot[i]);
        }
        addText(s, zonesState.initial[i], zonesState.dotX[i], zonesState.dotY[i], F13, Z_INK);
    }

    // Local hands, snapped to 0.25 deg steps.
    int32_t msHour = (ft.local.tm_min * 60 + ft.local.tm_sec) * 1000 + ft.ms;
    int32_t ms12 = (ft.local.tm_hour % 12) * 3600000 + msHour;
    int hourStep = ((ms12 + 15000) / 30000) % 1440;  // 0.5 deg a minute
    int minStep = ((msHour + 1250) / 2500) % 1440;   // 6 deg a minute
    if (hourStep != zonesState.hourStep) {
        addHand(s, hourStep * 0.25f, 81, 16, 8, C_TEXT);
        zonesSave(s, zonesState.hourHand);
        zonesState.hourStep = hourStep;
    } else {
        zonesCopy(s, zonesState.hourHand);
    }
    if (minStep != zonesState.minStep) {
        addHand(s, minStep * 0.25f, 137, 16, 4.5f, C_TEXT);
        zonesSave(s, zonesState.minHand);
        zonesState.minStep = minStep;
    } else {
        zonesCopy(s, zonesState.minHand);
    }

    // Centre dot and the legend. Pills paint over the hands.
    bool build = !zonesState.fixedReady;
    if (build) {
        addCircle(s, SCR_C, SCR_C, 7.5f, C_TEXT);
        zonesSave(s, zonesState.centre);
    } else {
        zonesCopy(s, zonesState.centre);
    }
    for (int slot = 0; slot < 4; slot++) {
        float x = SCR_C + (slot % 2 ? Z_PILL_DX : -Z_PILL_DX), y = Z_PILL_Y[slot / 2];
        float left = x - Z_PILL_W / 2, right = x + Z_PILL_W / 2;
        if (build) {
            addRRect(s, x, y, Z_PILL_W, Z_PILL_H, Z_PILL_R, Z_PILL);
            zonesSave(s, zonesState.pill[slot]);
            addCircle(s, left + 12, y, 4, slot ? PASTELS[(slot - 1) % 6] : C_TEXT);
            zonesSave(s, zonesState.pillDot[slot]);
        } else {
            zonesCopy(s, zonesState.pill[slot]);
            zonesCopy(s, zonesState.pillDot[slot]);
        }
        const char *label = slot ? kCities[zonesCity(slot - 1)].label : "Local";
        addText(s, label, left + 22, y, F13, Z_LABEL, ALIGN_LEFT);
        addText(s, slot ? zonesState.cityTxt[slot - 1] : zonesState.localTxt, right - 9, y, F13, C_DIM, ALIGN_RIGHT);
    }
    zonesState.fixedReady = true;
}
