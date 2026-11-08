// Clock settings and shared state. Plain C++ with no Arduino APIs, so the host
// simulator in tools/sim can build the faces too.
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum FaceId { FACE_WEATHER, FACE_ALARM, FACE_COUNT };

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

struct Settings {
    uint16_t version;
    uint8_t brightness;  // 5..100 %
    uint8_t homeCity;    // index into kCities, sets the clock's local time zone
    int32_t testOffset;  // seconds added to the displayed time ("Custom" mode), 0 = real time
    float lat, lon;      // weather location; general area only
    char place[32];
    uint8_t nightOn;     // dim the backlight between nightStart and nightEnd
    uint8_t nightBrightness;  // 1..100 %
    uint16_t nightStart, nightEnd;  // minutes after midnight
};

static const uint16_t kSettingsVersion = 3;
Settings S;
uint32_t gSettingsRev = 0;  // bumped on every settings change so faces rebuild their static layers

static void defaultSettings() {
    memset(&S, 0, sizeof(S));
    S.version = kSettingsVersion;
    S.brightness = 80;
    S.homeCity = kMelbourne;
    S.lat = -37.81f;
    S.lon = 144.96f;
    strlcpy(S.place, "Melbourne", sizeof(S.place));
    S.nightOn = 1;
    S.nightBrightness = 5;
    S.nightStart = 21 * 60 + 30;
    S.nightEnd = 7 * 60;
}

// True while the backlight should be at its night level. The window may cross midnight.
static bool nightActive(int minuteOfDay) {
    if (!S.nightOn || S.nightStart == S.nightEnd) return false;
    if (S.nightStart < S.nightEnd) return minuteOfDay >= S.nightStart && minuteOfDay < S.nightEnd;
    return minuteOfDay >= S.nightStart || minuteOfDay < S.nightEnd;
}

// ---- Live weather ----------------------------------------------------------
// Kept in RAM only. The network task fills a spare copy and then swaps the
// pointer, so a face never sees half an update.
#define WX_HOURS 12

struct WeatherData {
    int64_t fetchedAt;   // epoch seconds of the last good fetch, 0 = nothing yet
    int64_t hourStart;   // epoch seconds of rain[0]
    float tempC, feelsC;
    float highC[2], lowC[2];  // today, tomorrow
    int16_t code;        // WMO weather code
    uint8_t isDay;
    uint8_t rain[WX_HOURS];   // chance of rain per hour, %
};

static WeatherData gWxSlots[2];
static WeatherData *volatile gWx = &gWxSlots[0];

static void publishWeather(const WeatherData &w) {
    WeatherData *spare = gWx == &gWxSlots[0] ? &gWxSlots[1] : &gWxSlots[0];
    *spare = w;
    gWx = spare;
}

// ---- Self-update -----------------------------------------------------------
static volatile int8_t gUpdatePct = -1;  // -1 = no update running, else 0..100

// True if version a is newer than b. Accepts "1.3.0" or "v1.3.0".
static bool versionNewer(const char *a, const char *b) {
    int x[3] = {0, 0, 0}, y[3] = {0, 0, 0};
    sscanf(a + (*a == 'v' || *a == 'V'), "%d.%d.%d", &x[0], &x[1], &x[2]);
    sscanf(b + (*b == 'v' || *b == 'V'), "%d.%d.%d", &y[0], &y[1], &y[2]);
    for (int i = 0; i < 3; i++)
        if (x[i] != y[i]) return x[i] > y[i];
    return false;
}

struct NetInfo { bool ap; char ip[16]; char ssid[33]; };
NetInfo gNet = {false, "", ""};

// ---- Home Assistant status -------------------------------------------------
// Sent by Home Assistant (POST /api/ha) and kept in RAM only. Each part has an
// expiry time, so the face can never show a stale state if Home Assistant stops.
// Everything from HA_ARMED_HOME on shows the Alarm face.
enum HaAlarm : uint8_t {
    HA_NONE, HA_DISARMED, HA_ARMED_HOME, HA_ARMED_AWAY, HA_ARMED_NIGHT, HA_ARMED_OTHER, HA_ARMING, HA_PENDING, HA_TRIGGERED
};
static const char *const kHaAlarmNames[] = {"none", "disarmed", "armed_home", "armed_away", "armed_night",
                                            "armed", "arming", "pending", "triggered"};

struct HaStatus {
    uint8_t alarm;
    int64_t alarmUntil;   // epoch seconds
    char notice[48];
    int64_t noticeUntil;
};
static HaStatus gHa = {};

// Maps a Home Assistant alarm panel state to what the clock shows.
static uint8_t haAlarmFromState(const char *s) {
    if (!strcmp(s, "disarmed")) return HA_DISARMED;
    if (!strcmp(s, "armed_home")) return HA_ARMED_HOME;
    if (!strcmp(s, "armed_away")) return HA_ARMED_AWAY;
    if (!strcmp(s, "armed_night")) return HA_ARMED_NIGHT;
    if (!strncmp(s, "armed_", 6)) return HA_ARMED_OTHER;  // vacation, custom bypass
    if (!strcmp(s, "arming")) return HA_ARMING;
    if (!strcmp(s, "pending")) return HA_PENDING;
    if (!strcmp(s, "triggered")) return HA_TRIGGERED;
    return HA_NONE;  // unknown, unavailable, disarming
}

// The alarm state to show right now, or HA_NONE once Home Assistant's last report expired.
static uint8_t haAlarmAt(int64_t nowS) { return nowS < gHa.alarmUntil ? gHa.alarm : HA_NONE; }
