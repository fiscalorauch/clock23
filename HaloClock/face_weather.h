// Weather — digital time with current conditions. The rim is the next 12 hours
// of rain chance: each hour sits at its place on the dial, so rain at 3 pm shows
// between the 3 and the 4, and a thicker, brighter bar means a higher chance.
#pragma once
#include "faces_common.h"
#include "weather_parse.h"

static const uint16_t WX_BLUE = HEX565(0x7cc4ff);
static const uint16_t WX_BLUE_DIM = HEX565(0x46698a);
static const uint16_t WX_SUN = HEX565(0xffe8a3);
static const float WX_RIM_IN = 162.0f;

static void weatherStatic(Scene &s) {
    for (int i = 0; i < 12; i++) addArc(s, SCR_C, SCR_C, WX_RIM_IN, WX_RIM_IN + 3, i * 30 + 1.5f, i * 30 + 28.5f, C_TRACK);
}

static void wxRainRim(Scene &s, const FrameTime &ft, const WeatherData &w, bool have) {
    long cur = 0;
    if (have) {
        int64_t since = ft.realMs / 1000 - w.hourStart;
        cur = since < 0 ? 0 : (long)(since / 3600);
    }
    for (int k = 0; k < WX_HOURS; k++) {
        long ahead = k - cur;
        int chance = w.rain[k];
        bool show = have && ahead >= 0 && chance >= 10;
        int hour = (int)((ft.local.tm_hour + (ahead > 0 ? ahead : 0)) % 12);
        addArc(s, SCR_C, SCR_C, WX_RIM_IN, WX_RIM_IN + 4 + chance * 0.11f, hour * 30 + 1.5f, hour * 30 + 28.5f,
               chance >= 50 ? WX_BLUE : WX_BLUE_DIM, show ? 255 : 0);
    }
    float x, y;
    float hourPos = (ft.local.tm_hour % 12) + ft.local.tm_min / 60.0f;
    polarPt(153, snapDeg(hourPos * 30.0f, 1.0f), x, y);
    addCircle(s, x, y, 3.5f, C_PEACH);
}

// A lock above the date while the alarm is armed, arming, in entry delay or
// triggered. Nothing shows while it's disarmed or Home Assistant is silent.
static void wxLock(Scene &s, uint8_t alarm) {
    bool show = alarm >= HA_ARMED;
    uint16_t col = alarm == HA_TRIGGERED ? C_CORAL : alarm == HA_ARMED ? C_MINT : C_PEACH;
    const float cx = SCR_C, cy = 37;
    addArc(s, cx, cy - 3, 3.5f, 6.0f, 250, 470, col, show ? 255 : 0);
    addRRect(s, cx, cy + 4, 17, 12, 3, col, show ? 255 : 0);
}

// Always the same primitives in the same order; the unused ones have alpha 0.
static void wxIconDraw(Scene &s, float cx, float cy, const WeatherData &w, bool have) {
    WxIcon ic = wxIcon(w.code);
    bool clear = have && ic == WI_CLEAR, partly = have && ic == WI_PARTLY;
    bool sky = clear || partly, night = !w.isDay;
    float bx = clear ? cx : cx + 13, by = clear ? cy : cy - 13, r = clear ? 17 : 12;

    addCircle(s, bx, by, r, night ? C_TEXT : WX_SUN, sky ? 255 : 0);
    addCircle(s, bx + r * 0.5f, by - r * 0.35f, r * 0.85f, C_BLACK, sky && night ? 255 : 0);
    for (int i = 0; i < 8; i++) {
        float a = i * 45 * (float)M_PI / 180.0f, c = cosf(a), n = sinf(a);
        addLine(s, bx + c * (r + 5), by + n * (r + 5), bx + c * (r + 11), by + n * (r + 11), 3, WX_SUN, true,
                sky && !night ? 255 : 0);
    }

    bool cloud = have && !clear;
    uint16_t cc = ic == WI_PARTLY ? C_TEXT : C_DIM;
    uint8_t ca = cloud ? 255 : 0;
    addCircle(s, cx - 14, cy + 4, 11, C_BLACK, partly ? 255 : 0);  // gap between sun and cloud
    addCircle(s, cx + 1, cy - 5, 16, C_BLACK, partly ? 255 : 0);
    addCircle(s, cx - 14, cy + 4, 9, cc, ca);
    addCircle(s, cx + 1, cy - 5, 14, cc, ca);
    addCircle(s, cx + 15, cy + 3, 10, cc, ca);
    addRRect(s, cx, cy + 7, 46, 13, 6, cc, ca);

    bool wet = have && (ic == WI_RAIN || ic == WI_STORM);
    bool snow = have && ic == WI_SNOW, fog = have && ic == WI_FOG, storm = have && ic == WI_STORM;
    for (int i = 0; i < 3; i++) {
        float px = cx - 13 + i * 13;
        addLine(s, px + 3, cy + 20, px - 2, cy + 31, 3, WX_BLUE, true, wet ? 255 : 0);
        addCircle(s, px, cy + 25, 2.8f, C_TEXT, snow ? 255 : 0);
    }
    addLine(s, cx - 22, cy + 22, cx + 22, cy + 22, 3, C_DIM, true, fog ? 255 : 0);
    addLine(s, cx - 15, cy + 30, cx + 15, cy + 30, 3, C_FAINT, true, fog ? 255 : 0);
    addLine(s, cx + 3, cy + 17, cx - 4, cy + 28, 3.5f, WX_SUN, true, storm ? 255 : 0);
    addLine(s, cx - 4, cy + 28, cx + 3, cy + 37, 3.5f, WX_SUN, true, storm ? 255 : 0);
}

static void weatherDynamic(Scene &s, const FrameTime &ft) {
    const WeatherData &w = *gWx;
    bool have = w.fetchedAt != 0;
    char b[56], up[24];
    int64_t nowS = ft.realMs / 1000;
    uint8_t alarm = nowS < gHa.alarmUntil ? gHa.alarm : HA_NONE;
    bool notice = nowS < gHa.noticeUntil && gHa.notice[0];

    wxRainRim(s, ft, w, have);
    wxLock(s, alarm);

    strftime(b, sizeof(b), "%a %e %b", &ft.local);
    upperCopy(up, b, sizeof(up));
    char *dst = up;  // %e pads single-digit days
    for (char *c = up; *c; c++)
        if (*c != ' ' || c[1] != ' ') *dst++ = *c;
    *dst = 0;
    addText(s, up, SCR_C, 62, F13, C_DIM, ALIGN_CENTER, 3);

    snprintf(b, sizeof(b), "%d:%02d", (ft.local.tm_hour % 12) ? ft.local.tm_hour % 12 : 12, ft.local.tm_min);
    addText(s, b, SCR_C, 116, D70, C_TEXT);

    const float rowY = 196;
    wxIconDraw(s, 120, rowY - 6, w, have);
    if (have) snprintf(b, sizeof(b), "%ld", lroundf(w.tempC));
    else strlcpy(b, "--", sizeof(b));
    const float tempX = 164;
    addText(s, b, tempX, rowY, F40, C_TEXT, ALIGN_LEFT);
    addRing(s, tempX + textWidth(b, F40) + 9, rowY - 12, 3.5f, 6.5f, C_TEXT, have ? 255 : 0);

    const char *word = !have ? (gNet.ap ? "No internet" : "Waiting for weather") : wxWord(w.code, w.isDay);
    uint16_t wordCol = have ? C_TEXT : C_DIM;
    if (alarm == HA_TRIGGERED) word = "Alarm triggered", wordCol = C_CORAL;
    else if (alarm == HA_PENDING) word = "Alarm entry delay", wordCol = C_PEACH;
    addText(s, word, SCR_C, 244, F21, wordCol);

    // Today's range until 6 pm, then tomorrow's.
    int d = ft.local.tm_hour >= 18 ? 1 : 0;
    if (have) snprintf(b, sizeof(b), "%sHigh %ld  Low %ld", d ? "Tomorrow  " : "", lroundf(w.highC[d]), lroundf(w.lowC[d]));
    else b[0] = 0;
    addText(s, b, SCR_C, 274, F16, C_DIM, ALIGN_CENTER, 0, have ? 255 : 0);

    long age = have ? (long)(ft.realMs / 1000 - w.fetchedAt) : 0;
    bool stale = age >= 45 * 60;
    if (stale && age < 2 * 3600) snprintf(b, sizeof(b), "Updated %ld min ago", age / 60);
    else if (stale && age < 48 * 3600) snprintf(b, sizeof(b), "Updated %ld h ago", age / 3600);
    else if (stale) snprintf(b, sizeof(b), "Updated %ld days ago", age / 86400);
    else if (have) snprintf(b, sizeof(b), "Feels like %ld", lroundf(w.feelsC));
    else b[0] = 0;
    uint16_t lineCol = stale ? C_CORAL : C_FAINT;
    if (notice) {  // a message from Home Assistant wins, trimmed to fit the curve
        strlcpy(b, gHa.notice, sizeof(b));
        size_t full = strlen(b);
        for (size_t n = full; n > 0 && textWidth(b, F13) > 166; n--) b[n - 1] = 0;
        char *space = strrchr(b, ' ');
        if (strlen(b) < full && space && space - b > (long)strlen(b) / 2) *space = 0;  // end on a whole word
        lineCol = C_PEACH;
    }
    addText(s, b, SCR_C, 300, F13, lineCol, ALIGN_CENTER, 0, have || notice ? 255 : 0);
}
