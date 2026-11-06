// Face switcher. Builds one scene per frame; the renderer sends only what changed.
//
// The includer must define, before including this file:
//   bool timeValid();  int64_t realNowMs();
//   void displayTime(struct tm &out, int &ms);  void cityTime(uint8_t city, struct tm &out);
//
// Each face file provides <name>Static(Scene &) for the parts that only change
// with settings, and <name>Dynamic(Scene &, const FrameTime &) for the rest.
// (ONLY_FACE / ONLY_<NAME> are for building one face at a time in the simulator.)
#pragma once
#include "faces_common.h"
#include "face_setup.h"
#if !defined(ONLY_FACE) || defined(ONLY_CLASSIC)
#include "face_classic.h"
#endif
#if !defined(ONLY_FACE) || defined(ONLY_LEARN)
#include "face_learn.h"
#endif
#if !defined(ONLY_FACE) || defined(ONLY_TIMERS)
#include "face_timers.h"
#endif
#if !defined(ONLY_FACE) || defined(ONLY_ZONES)
#include "face_zones.h"
#endif
#if !defined(ONLY_FACE) || defined(ONLY_SCHEDULE)
#include "face_schedule.h"
#endif

static const int FACE_SETUP = FACE_COUNT;
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

    int face = timeValid() ? S.face : FACE_SETUP;
    if (face != gShownFace) {
        renderInvalidate();
        gShownFace = face;
    }
    uint16_t bg = C_BLACK;
#if !defined(ONLY_FACE) || defined(ONLY_LEARN)
    if (face == FACE_LEARN) bg = LEARN_BG;
#endif
    bool needStatic;
    Scene &s = frameBegin(bg, (uint32_t)face | gSettingsRev << 4, needStatic);
    switch (face) {
#if !defined(ONLY_FACE) || defined(ONLY_CLASSIC)
        case FACE_CLASSIC: FACE_LAYERS(classic); break;
#endif
#if !defined(ONLY_FACE) || defined(ONLY_LEARN)
        case FACE_LEARN: FACE_LAYERS(learn); break;
#endif
#if !defined(ONLY_FACE) || defined(ONLY_TIMERS)
        case FACE_EXAM: FACE_LAYERS(exam); break;
        case FACE_FOCUS: FACE_LAYERS(focus); break;
#endif
#if !defined(ONLY_FACE) || defined(ONLY_ZONES)
        case FACE_ZONES: FACE_LAYERS(zones); break;
#endif
#if !defined(ONLY_FACE) || defined(ONLY_SCHEDULE)
        case FACE_SCHEDULE: FACE_LAYERS(schedule); break;
#endif
        default: setupDynamic(s); break;
    }
    frameEnd();
}
