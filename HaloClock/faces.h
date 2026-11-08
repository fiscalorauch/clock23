// Chooses the screen and builds one scene per frame: Weather normally, Alarm
// while Home Assistant reports the alarm armed, the setup screen until the clock knows the time, the update screen during an
// update. The renderer sends only what changed.
//
// The includer must define, before including this file:
//   bool timeValid();  int64_t realNowMs();
//   void displayTime(struct tm &out, int &ms);
//
// A face provides <name>Static(Scene &) for the parts that only change with
// settings, and <name>Dynamic(Scene &, const FrameTime &) for the rest.
#pragma once
#include "faces_common.h"
#include "face_setup.h"
#include "face_update.h"
#include "face_weather.h"
#include "face_alarm.h"

static const int FACE_SETUP = FACE_COUNT;
static const int FACE_UPDATE = FACE_COUNT + 1;
static int gShownFace = -1;

// Forces a full repaint, e.g. after the display was re-initialised.
static void requestRedraw() { renderInvalidate(); }

#define FACE_LAYERS(name)          \
    if (needStatic) {              \
        name##Static(s);           \
        staticDone(s);             \
    }                              \
    name##Dynamic(s, ft)

static void drawClock() {
    FrameTime ft;
    displayTime(ft.local, ft.ms);
    ft.realMs = realNowMs();

    uint8_t alarm = haAlarmAt(ft.realMs / 1000);
    int face = gUpdatePct >= 0 ? FACE_UPDATE
             : !timeValid() ? FACE_SETUP
             : alarm >= HA_ARMED_HOME ? FACE_ALARM
                                      : FACE_WEATHER;
    if (face != gShownFace) {
        renderInvalidate();
        gShownFace = face;
    }
    bool needStatic;
    Scene &s = frameBegin(face == FACE_ALARM ? alarmBg(alarm) : C_BLACK, (uint32_t)face | gSettingsRev << 4, needStatic);
    switch (face) {
        case FACE_WEATHER: FACE_LAYERS(weather); break;
        case FACE_ALARM: FACE_LAYERS(alarm); break;
        case FACE_UPDATE: updateDynamic(s); break;
        default: setupDynamic(s); break;
    }
    frameEnd();
}
