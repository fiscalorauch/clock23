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

    const char *label = a == HA_TRIGGERED ? "ALARM"
                        : a == HA_PENDING ? "ENTRY DELAY"
                        : a == HA_ARMING ? "ARMING"
                        : a == HA_ARMED_HOME ? "ARMED \u00b7 HOME"
                        : a == HA_ARMED_AWAY ? "ARMED \u00b7 AWAY"
                        : a == HA_ARMED_NIGHT ? "ARMED \u00b7 NIGHT"
                                              : "ARMED";
    addRing(s, SCR_C, SCR_C, 164, 170, accent, loud ? 255 : 110);
    addText(s, label, SCR_C, 64, F16, accent, ALIGN_CENTER, 3);

    // Lock: shackle, body, keyhole.
    addArc(s, SCR_C, 106, 13, 19, 250, 470, accent);
    addRRect(s, SCR_C, 134, 54, 40, 8, accent);
    addCircle(s, SCR_C, 130, 5, bg);
    addRRect(s, SCR_C, 139, 4, 12, 2, bg);

    char b[56];
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
