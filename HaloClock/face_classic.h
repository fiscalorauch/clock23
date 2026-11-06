// Classic — plain analogue face with a sweeping second hand.
#pragma once
#include "faces_common.h"

static void classicStatic(Scene &s) {
    for (int i = 0; i < 60; i++) {
        bool major = i % 5 == 0;
        float x0, y0, x1, y1;
        polarPt(major ? 151 : 162, i * 6, x0, y0);
        polarPt(171, i * 6, x1, y1);
        addLine(s, x0, y0, x1, y1, major ? 4.5f : 2.0f, major ? C_TEXT : C_MINOR, major);
    }
    for (int n = 1; n <= 12; n++) {
        float x, y;
        polarPt(122, n * 30, x, y);
        char b[3];
        snprintf(b, sizeof(b), "%d", n);
        addText(s, b, x, y, F34, C_TEXT);
    }
}

static void classicDynamic(Scene &s, const FrameTime &ft) {
    float sec = ft.local.tm_sec + ft.ms / 1000.0f;
    float minute = ft.local.tm_min + sec / 60.0f;
    float hour = (ft.local.tm_hour % 12) + minute / 60.0f;
    addHand(s, snapDeg(hour * 30.0f, 0.25f), 79, 23, 10, C_TEXT);
    addHand(s, snapDeg(minute * 6.0f, 0.25f), 119, 23, 6, C_TEXT);
    addHand(s, snapDeg(sec * 6.0f, 0.1f), 137, 31, 2.2f, C_PEACH);
    addCircle(s, SCR_C, SCR_C, 7, C_TEXT);
    addCircle(s, SCR_C, SCR_C, 3, C_PEACH);
}
