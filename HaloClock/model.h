// Clock settings and shared state. Plain C++ with no Arduino APIs, so the host
// simulator in tools/sim can build the faces too.
#pragma once
#include <stdint.h>
#include <string.h>

enum FaceId { FACE_CLASSIC, FACE_LEARN, FACE_EXAM, FACE_ZONES, FACE_FOCUS, FACE_SCHEDULE, FACE_COUNT };
static const char *kFaceIds[FACE_COUNT] = {"classic", "learn", "exam", "zones", "focus", "schedule"};

struct City { const char *label; const char *posix; };
static const City kCities[] = {
    {"London", "GMT0BST,M3.5.0/1,M10.5.0"},
    {"Paris", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Berlin", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"New York", "EST5EDT,M3.2.0,M11.1.0"},
    {"Los Angeles", "PST8PDT,M3.2.0,M11.1.0"},
    {"Tokyo", "JST-9"},
    {"Beijing", "CST-8"},
    {"Singapore", "<+08>-8"},
    {"Dubai", "<+04>-4"},
    {"Mumbai", "IST-5:30"},
    {"Sydney", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
    {"Melbourne", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
    {"Auckland", "NZST-12NZDT,M9.5.0,M4.1.0/3"},
    {"Honolulu", "HST10"},
};
static const uint8_t kNumCities = sizeof(kCities) / sizeof(kCities[0]);
static const uint8_t kMelbourne = 11;

#define MAX_PERIODS 12
#define NUM_ZONES 3

struct Period { char name[24]; uint16_t start, end; };  // minutes after midnight

struct Settings {
    uint16_t version;
    uint8_t face;
    uint8_t brightness;  // 5..100 %
    uint8_t homeCity;    // index into kCities, sets the clock's local time zone
    uint8_t zones[NUM_ZONES];
    char examSubject[40];
    uint16_t examMin;
    int64_t examEnd;     // epoch seconds, 0 = not started
    char focusLabel[32];
    uint16_t focusMin;
    int64_t focusEnd;
    int32_t testOffset;  // seconds added to the displayed time ("Custom" mode), 0 = real time
    uint8_t numPeriods;
    Period periods[MAX_PERIODS];
};

static const uint16_t kSettingsVersion = 1;
Settings S;
uint32_t gSettingsRev = 0;  // bumped on every settings change so faces rebuild their static layers

static void defaultSettings() {
    memset(&S, 0, sizeof(S));
    S.version = kSettingsVersion;
    S.face = FACE_CLASSIC;
    S.brightness = 80;
    S.homeCity = kMelbourne;
    S.zones[0] = 3;  // New York
    S.zones[1] = 0;  // London
    S.zones[2] = 5;  // Tokyo
    strlcpy(S.examSubject, "Mathematics · Paper 1", sizeof(S.examSubject));
    S.examMin = 45;
    strlcpy(S.focusLabel, "Silent reading", sizeof(S.focusLabel));
    S.focusMin = 20;
    const Period defaults[] = {
        {"Maths", 540, 600}, {"English", 600, 660}, {"Recess", 660, 690}, {"Science", 690, 750},
        {"Lunch", 750, 795},  {"Art", 795, 855},     {"Sport", 855, 930},
    };
    S.numPeriods = sizeof(defaults) / sizeof(defaults[0]);
    memcpy(S.periods, defaults, sizeof(defaults));
}

struct NetInfo { bool ap; char ip[16]; char ssid[33]; };
NetInfo gNet = {false, "", ""};
