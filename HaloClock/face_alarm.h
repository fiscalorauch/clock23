// Alarm — shown instead of Weather while Home Assistant reports the alarm
// armed, arming, in entry delay or triggered. Entry delay and triggered fill
// the screen with colour so they read from across the room.
#pragma once
#include "faces_common.h"
#include "face_weather.h"

static uint16_t alarmBg(uint8_t a) { return a == HA_TRIGGERED ? C_CORAL : a == HA_PENDING ? C_PEACH : C_BLACK; }

static void alarmStatic(Scene &s) {}

static void alarmDynamic(Scene &s, const FrameTime &ft) {
    uint8_t a = haAlarmAt(ft.realMs / 1000);
    bool loud = a == HA_PENDING || a == HA_TRIGGERED;
    uint16_t bg = alarmBg(a);
    uint16_t ink = loud ? C_BLACK : C_TEXT;
    uint16_t accent = loud ? C_BLACK : a == HA_ARMING ? C_PEACH : C_MINT;
    uint16_t soft = loud ? C_BLACK : C_DIM;
    int left = a == HA_ARMING ? haCountdownLeft(ft.realMs) : 0;
    bool counting = left > 0;

    const char *label = a == HA_TRIGGERED ? "ALARM"
                        : a == HA_PENDING ? "ENTRY DELAY"
                        : a == HA_ARMING ? "ARMING"
                        : a == HA_ARMED_HOME ? "ARMED \u00b7 HOME"
                        : a == HA_ARMED_AWAY ? "ARMED \u00b7 AWAY"
                        : a == HA_ARMED_NIGHT ? "ARMED \u00b7 NIGHT"
                                              : "ARMED";
    addRing(s, SCR_C, SCR_C, 164, 170, accent, loud ? 255 : counting ? 45 : 110);
    // While counting down, the ring empties clockwise towards 12 o'clock.
    float total = gHa.countdownTotal > 0 ? gHa.countdownTotal : 1.0f;
    float frac = counting ? (float)(gHa.countdownUntil * 1000 - ft.realMs) / (total * 1000.0f) : 0;
    addRoundArc(s, 167, 6, 0, snapDeg((frac > 1 ? 1 : frac) * 360.0f, 1.0f), accent, counting ? 255 : 0);
    addText(s, label, SCR_C, 64, F16, accent, ALIGN_CENTER, 3);

    // Lock: shackle, body, keyhole. The exit delay countdown takes its place.
    uint8_t lockAlpha = counting ? 0 : 255;
    addArc(s, SCR_C, 106, 13, 19, 250, 470, accent, lockAlpha);
    addRRect(s, SCR_C, 134, 54, 40, 8, accent, lockAlpha);
    addCircle(s, SCR_C, 130, 5, bg, lockAlpha);
    addRRect(s, SCR_C, 139, 4, 12, 2, bg, lockAlpha);

    char b[56];
    if (counting) snprintf(b, sizeof(b), "%d", left);
    else b[0] = 0;
    addText(s, b, SCR_C, 124, D70, accent, ALIGN_CENTER, 0, counting ? 255 : 0);

    fmtClock(b, sizeof(b), ft.local);
    addText(s, b, SCR_C, 207, D70, ink);

    // One weather line: temperature, degree ring and conditions, centred as a group.
    const WeatherData &w = *gWx;
    bool have = w.fetchedAt != 0;
    char temp[8];
    if (have) snprintf(temp, sizeof(temp), "%ld", lroundf(w.tempC));
    else temp[0] = 0;
    const char *word = have ? wxWord(w.code, w.isDay) : "Waiting for weather";
    int tw = have ? textWidth(temp, F21) : 0, gap = have ? 18 : 0, ww = textWidth(word, F21);
    float x = SCR_C - (tw + gap + ww) / 2.0f;
    addText(s, temp, x, 262, F21, ink, ALIGN_LEFT);
    addRing(s, x + tw + 5, 255, 2.0f, 3.6f, ink, have ? 255 : 0);
    addText(s, word, x + tw + gap, 262, F21, have ? ink : soft, ALIGN_LEFT);

    bool notice = ft.realMs / 1000 < gHa.noticeUntil && gHa.notice[0];
    if (notice) fitNotice(b, sizeof(b));
    else b[0] = 0;
    addText(s, b, SCR_C, 298, F13, loud ? C_BLACK : C_PEACH, ALIGN_CENTER, 0, notice ? 255 : 0);
}
