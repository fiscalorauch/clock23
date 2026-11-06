// Host stand-ins for the clock's platform layer, so the real face and renderer
// code runs on a Mac. The "panel" is a 360x360 RGB565 framebuffer.
#pragma once
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include "../../HaloClock/model.h"

// ---- Fake clock ------------------------------------------------------------
static int64_t gSimEpochMs = 0;  // set by the simulator before each frame

static bool timeValid() { return gSimEpochMs / 1000 > 1700000000; }
static int64_t realNowMs() { return gSimEpochMs; }

static void applyHomeZone() {
    setenv("TZ", kCities[S.homeCity].posix, 1);
    tzset();
}

static void displayTime(struct tm &out, int &ms) {
    time_t t = (time_t)(gSimEpochMs / 1000) + S.testOffset;
    localtime_r(&t, &out);
    ms = (int)(gSimEpochMs % 1000);
}

const char *AP_PASS = "halo1234";

// ---- Fake panel ------------------------------------------------------------
static uint16_t gPanelA[360 * 360], gPanelB[360 * 360];
static uint16_t *gPanel = gPanelA;
static int gWinX, gWinY, gWinW, gWinH;
static long gWinPos;
static long gOutOfWindow;  // pixels sent past the end of a window (should stay 0)

static void sinkBegin() {}
static void sinkEnd() {}
static void sinkWindow(int x, int y, int w, int h) {
    gWinX = x; gWinY = y; gWinW = w; gWinH = h;
    gWinPos = 0;
}
static void sinkPixels(uint16_t *px, int n) {
    for (int i = 0; i < n; i++, gWinPos++) {
        if (gWinPos >= (long)gWinW * gWinH) { gOutOfWindow++; continue; }
        int x = gWinX + gWinPos % gWinW, y = gWinY + gWinPos / gWinW;
        if (x >= 0 && x < 360 && y >= 0 && y < 360) gPanel[y * 360 + x] = px[i];
    }
}
