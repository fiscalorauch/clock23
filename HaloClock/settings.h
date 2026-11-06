// Saving settings to flash, the clock's time, and the backlight.
#pragma once
#include <Preferences.h>
#include <time.h>
#include <sys/time.h>
#include "model.h"

Preferences prefs;
char gDeviceName[32] = "Halo";  // the clock's own name, e.g. "Room 12"

static void loadSettings() {
    prefs.begin("halo", false);
    if (prefs.isKey("name")) prefs.getString("name", gDeviceName, sizeof(gDeviceName));
    if (prefs.getBytesLength("cfg") != sizeof(S) || prefs.getBytes("cfg", &S, sizeof(S)) != sizeof(S) ||
        S.version != kSettingsVersion) {
        defaultSettings();
    }
}

static void saveSettings() {
    prefs.putBytes("cfg", &S, sizeof(S));
    gSettingsRev++;  // faces rebuild their static layers; the renderer repaints only what changed
}

static void saveDeviceName(const char *name) {
    strlcpy(gDeviceName, name, sizeof(gDeviceName));
    if (!gDeviceName[0]) strlcpy(gDeviceName, "Halo", sizeof(gDeviceName));
    prefs.putString("name", gDeviceName);
}

// ---------------------------------------------------------------------------
// Time
// ---------------------------------------------------------------------------
static bool timeValid() { return time(nullptr) > 1700000000; }

static void applyHomeZone() {
    setenv("TZ", kCities[S.homeCity].posix, 1);
    tzset();
}

// Real time, used for timers.
static time_t realNow() { return time(nullptr); }

static int64_t realNowMs() {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

// Time shown on the clock faces (real time plus the optional test offset).
static void displayTime(struct tm &out, int &ms) {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    time_t t = tv.tv_sec + S.testOffset;
    localtime_r(&t, &out);
    ms = tv.tv_usec / 1000;
}

static void cityTime(uint8_t city, struct tm &out) {
    time_t t = time(nullptr) + S.testOffset;
    setenv("TZ", kCities[city].posix, 1);
    tzset();
    localtime_r(&t, &out);
    applyHomeZone();
}

static void setClockEpoch(int64_t epoch) {
    struct timeval tv = {(time_t)epoch, 0};
    settimeofday(&tv, nullptr);
}

// ---------------------------------------------------------------------------
// Backlight
// ---------------------------------------------------------------------------
static uint8_t gBacklightPct = 0;

static uint8_t targetBrightness() {
    if (!timeValid()) return S.brightness;
    struct tm t;
    int ms;
    displayTime(t, ms);
    return nightActive(t.tm_hour * 60 + t.tm_min) ? S.nightBrightness : S.brightness;
}

static void applyBrightness() {
    gBacklightPct = targetBrightness();
    ledcWrite(TFT_BL, map(gBacklightPct, 0, 100, 0, 255));
}

// Called about once a second, so night mode starts and ends on time.
static void brightnessTick() {
    if (targetBrightness() != gBacklightPct) applyBrightness();
}
