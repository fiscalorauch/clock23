// Wi-Fi dashboard: serves the control page and a small JSON/form API.
#pragma once
#include <WebServer.h>
#include "settings.h"
#include "wifi_config.h"
#include "faces.h"
#include "analytics.h"
#include "dashboard.h"

WebServer server(80);

static void jsonStr(String &o, const char *s) {
    o += '"';
    for (; *s; s++) {
        char c = *s;
        if (c == '"' || c == '\\') { o += '\\'; o += c; }
        else if ((uint8_t)c < 0x20) { char b[8]; snprintf(b, sizeof(b), "\\u%04x", c); o += b; }
        else o += c;
    }
    o += '"';
}

static long timerLeft(int64_t end) {
    if (end <= 0) return -1;
    return max<long>(0, (long)(end - realNow()));
}

// Current weather, in /api/state and /api/weather (handy for Home Assistant).
static void wxJson(String &o) {
    const WeatherData &w = *gWx;
    bool have = w.fetchedAt != 0;
    o += "{\"have\":"; o += have ? "true" : "false";
    if (have) {
        o += ",\"temp\":"; o += String(w.tempC, 1);
        o += ",\"feels\":"; o += String(w.feelsC, 1);
        o += ",\"code\":"; o += w.code;
        o += ",\"word\":"; jsonStr(o, wxWord(w.code, w.isDay));
        o += ",\"high\":"; o += String(w.highC[0], 1);
        o += ",\"low\":"; o += String(w.lowC[0], 1);
        o += ",\"age\":"; o += (long)(realNow() - w.fetchedAt);
        o += ",\"rain\":[";
        for (int i = 0; i < WX_HOURS; i++) { if (i) o += ','; o += w.rain[i]; }
        o += ']';
    }
    o += '}';
}

static void sendState() {
    A.requests++;
    String o;
    o.reserve(2800);
    o += "{\"face\":\""; o += kFaceIds[S.face]; o += '"';
    o += ",\"firmware\":\"" FIRMWARE_VERSION "\"";
    o += ",\"deviceName\":"; jsonStr(o, gDeviceName);
    o += ",\"brightness\":"; o += S.brightness;
    o += ",\"home\":"; o += S.homeCity;
    o += ",\"zones\":["; for (int i = 0; i < NUM_ZONES; i++) { if (i) o += ','; o += S.zones[i]; } o += ']';
    o += ",\"examSubject\":"; jsonStr(o, S.examSubject);
    o += ",\"examMin\":"; o += S.examMin;
    o += ",\"examLeft\":"; o += timerLeft(S.examEnd);
    o += ",\"focusLabel\":"; jsonStr(o, S.focusLabel);
    o += ",\"focusMin\":"; o += S.focusMin;
    o += ",\"focusLeft\":"; o += timerLeft(S.focusEnd);
    o += ",\"testOffset\":"; o += S.testOffset;
    o += ",\"lat\":"; o += String(S.lat, 3);
    o += ",\"lon\":"; o += String(S.lon, 3);
    o += ",\"place\":"; jsonStr(o, S.place);
    o += ",\"nightOn\":"; o += S.nightOn ? "true" : "false";
    o += ",\"nightBrightness\":"; o += S.nightBrightness;
    o += ",\"nightStart\":"; o += S.nightStart;
    o += ",\"nightEnd\":"; o += S.nightEnd;
    o += ",\"backlight\":"; o += gBacklightPct;
    o += ",\"wx\":"; wxJson(o);
    o += ",\"update\":{\"state\":\""; o += kUpdateStates[gUpState]; o += '"';
    o += ",\"latest\":"; jsonStr(o, gUpLatest);
    o += ",\"pct\":"; o += (int)gUpdatePct;
    o += ",\"error\":"; jsonStr(o, gUpError); o += '}';
    o += ",\"schedule\":[";
    for (int i = 0; i < S.numPeriods; i++) {
        if (i) o += ',';
        o += "{\"name\":"; jsonStr(o, S.periods[i].name);
        o += ",\"start\":"; o += S.periods[i].start;
        o += ",\"end\":"; o += S.periods[i].end; o += '}';
    }
    o += "],\"cities\":[";
    for (int i = 0; i < kNumCities; i++) { if (i) o += ','; jsonStr(o, kCities[i].label); }
    o += "],\"timeValid\":"; o += timeValid() ? "true" : "false";
    o += ",\"now\":"; o += String((double)realNow(), 0);
    o += ",\"ap\":"; o += gNet.ap ? "true" : "false";
    o += ",\"ip\":"; jsonStr(o, gNet.ip);
    o += ",\"ssid\":"; jsonStr(o, gNet.ssid);
    // Wi-Fi settings. Passwords are never sent back, only whether one is stored.
    o += ",\"wifiKind\":"; o += W.kind;
    o += ",\"wifiSsid\":"; jsonStr(o, W.ssid);
    o += ",\"wifiIdentity\":"; jsonStr(o, W.identity);
    o += ",\"wifiUsername\":"; jsonStr(o, W.username);
    o += ",\"wifiEapMethod\":"; o += W.eapMethod;
    o += ",\"wifiHasPass\":"; o += strlen(W.pass) ? "true" : "false";
    o += ",\"wifiHasEapPass\":"; o += strlen(W.eapPass) ? "true" : "false";
    o += '}';
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", o);
}

static void copyArg(const char *name, char *dst, size_t n) {
    String v = server.arg(name);
    v.trim();
    strlcpy(dst, v.c_str(), n);
}

static void parseSchedule(const String &text) {
    char buf[1024];
    strlcpy(buf, text.c_str(), sizeof(buf));
    S.numPeriods = 0;
    char *rowSave;
    for (char *row = strtok_r(buf, "\n", &rowSave); row && S.numPeriods < MAX_PERIODS; row = strtok_r(nullptr, "\n", &rowSave)) {
        char *fieldSave;
        char *name = strtok_r(row, "|", &fieldSave);
        char *start = strtok_r(nullptr, "|", &fieldSave);
        char *end = strtok_r(nullptr, "|", &fieldSave);
        if (!name || !start || !end) continue;
        Period &p = S.periods[S.numPeriods];
        strlcpy(p.name, name, sizeof(p.name));
        p.start = constrain(atoi(start), 0, 1439);
        p.end = constrain(atoi(end), 1, 1440);
        if (p.end > p.start) S.numPeriods++;
    }
}

// POST /api/set — any subset of: face, brightness, home, zones, examSubject, examMin,
// focusLabel, focusMin, testOffset, schedule ("name|start|end" rows, minutes after midnight).
// No explicit redraw is needed: the next frame's scene differs and only that is repainted.
static void handleSet() {
    if (server.hasArg("face"))
        for (int i = 0; i < FACE_COUNT; i++)
            if (server.arg("face") == kFaceIds[i]) S.face = i;
    if (server.hasArg("brightness")) {
        S.brightness = constrain(server.arg("brightness").toInt(), 5, 100);
        applyBrightness();
    }
    if (server.hasArg("home")) {
        S.homeCity = constrain(server.arg("home").toInt(), 0, kNumCities - 1);
        applyHomeZone();
    }
    if (server.hasArg("zones")) {
        String z = server.arg("zones");
        int from = 0;
        for (int i = 0; i < NUM_ZONES; i++) {
            int comma = z.indexOf(',', from);
            String part = comma < 0 ? z.substring(from) : z.substring(from, comma);
            if (part.length()) S.zones[i] = constrain(part.toInt(), 0, kNumCities - 1);
            if (comma < 0) break;
            from = comma + 1;
        }
    }
    if (server.hasArg("examSubject")) copyArg("examSubject", S.examSubject, sizeof(S.examSubject));
    if (server.hasArg("examMin")) S.examMin = constrain(server.arg("examMin").toInt(), 1, 240);
    if (server.hasArg("focusLabel")) copyArg("focusLabel", S.focusLabel, sizeof(S.focusLabel));
    if (server.hasArg("focusMin")) S.focusMin = constrain(server.arg("focusMin").toInt(), 1, 180);
    if (server.hasArg("testOffset")) S.testOffset = constrain(server.arg("testOffset").toInt(), -604800L, 604800L);
    if (server.hasArg("schedule")) parseSchedule(server.arg("schedule"));
    if (server.hasArg("lat") && server.hasArg("lon")) {
        S.lat = constrain(server.arg("lat").toFloat(), -90.0f, 90.0f);
        S.lon = constrain(server.arg("lon").toFloat(), -180.0f, 180.0f);
        gWxRefresh = true;
    }
    if (server.hasArg("place")) copyArg("place", S.place, sizeof(S.place));
    if (server.hasArg("nightOn")) S.nightOn = server.arg("nightOn").toInt() ? 1 : 0;
    if (server.hasArg("nightBrightness")) S.nightBrightness = constrain(server.arg("nightBrightness").toInt(), 1, 100);
    if (server.hasArg("nightStart")) S.nightStart = constrain(server.arg("nightStart").toInt(), 0, 1439);
    if (server.hasArg("nightEnd")) S.nightEnd = constrain(server.arg("nightEnd").toInt(), 0, 1439);
    if (server.hasArg("deviceName")) {
        char name[32];
        copyArg("deviceName", name, sizeof(name));
        saveDeviceName(name);
    }
    saveSettings();
    sendState();
}

// POST /api/action?a=examStart|examStop|focusStart|focusStop
static void handleAction() {
    String a = server.arg("a");
    if (a == "examStart") { S.examEnd = realNow() + S.examMin * 60L; S.face = FACE_EXAM; analyticsExamStarted(); }
    else if (a == "examStop") S.examEnd = 0;
    else if (a == "focusStart") { S.focusEnd = realNow() + S.focusMin * 60L; S.face = FACE_FOCUS; analyticsFocusStarted(); }
    else if (a == "focusStop") S.focusEnd = 0;
    saveSettings();
    sendState();
}

// GET /api/weather — just the weather. POST also asks for a fresh fetch.
static void handleWeather() {
    if (server.method() == HTTP_POST) gWxRefresh = true;
    String o;
    o.reserve(300);
    wxJson(o);
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", o);
}

// POST /api/update?a=check|install — installs only the newest release of this project.
static void handleUpdate() {
    String a = server.arg("a");
    bool busy = gUpState == UP_CHECKING || gUpState == UP_INSTALLING || gUpState == UP_DONE;
    if (!busy && a == "check") gUpCheckReq = true;
    else if (!busy && a == "install") gUpInstallReq = true;
    sendState();
}

// POST /api/time?epoch=<unix seconds> — sets the clock from the browser (no internet needed).
static void handleTime() {
    int64_t epoch = strtoll(server.arg("epoch").c_str(), nullptr, 10);
    if (epoch > 1700000000LL) setClockEpoch(epoch);
    sendState();
}

// POST /api/wifi — kind (0 home, 1 school), ssid, pass, identity, username, eappass, eapMethod.
// Blank passwords keep the stored ones. The clock restarts to join the new network.
static void handleWifi() {
    if (server.hasArg("kind")) W.kind = server.arg("kind").toInt() ? WIFI_KIND_SCHOOL : WIFI_KIND_HOME;
    if (server.hasArg("eapMethod")) W.eapMethod = server.arg("eapMethod").toInt() ? 1 : 0;
    if (server.hasArg("ssid")) copyArg("ssid", W.ssid, sizeof(W.ssid));
    if (server.hasArg("identity")) copyArg("identity", W.identity, sizeof(W.identity));
    if (server.hasArg("username")) copyArg("username", W.username, sizeof(W.username));
    if (server.hasArg("pass") && server.arg("pass").length()) copyArg("pass", W.pass, sizeof(W.pass));
    if (server.hasArg("eappass") && server.arg("eappass").length()) copyArg("eappass", W.eapPass, sizeof(W.eapPass));
    wifiSave();
    sendState();
    gRestartAt = millis() + 800;  // let the reply reach the browser first
}

// GET /api/info — firmware, hardware, memory, network, drawing performance and usage.
static void handleInfo() {
    A.requests++;
    String o;
    o.reserve(1600);
    char buf[48];
    o += "{\"firmware\":\"" FIRMWARE_VERSION "\"";
    o += ",\"built\":\"" __DATE__ " " __TIME__ "\"";
    o += ",\"sdk\":"; jsonStr(o, ESP.getSdkVersion());
    snprintf(buf, sizeof(buf), "%d.%d.%d", ESP_ARDUINO_VERSION_MAJOR, ESP_ARDUINO_VERSION_MINOR, ESP_ARDUINO_VERSION_PATCH);
    o += ",\"arduino\":"; jsonStr(o, buf);
    o += ",\"name\":"; jsonStr(o, gDeviceName);

    // Hardware and memory
    o += ",\"chip\":"; jsonStr(o, ESP.getChipModel());
    o += ",\"chipRev\":"; o += (int)ESP.getChipRevision();
    o += ",\"cores\":"; o += (int)ESP.getChipCores();
    o += ",\"cpuMhz\":"; o += getCpuFrequencyMhz();
    o += ",\"flashSize\":"; o += ESP.getFlashChipSize();
    o += ",\"sketchSize\":"; o += ESP.getSketchSize();
    o += ",\"sketchFree\":"; o += ESP.getFreeSketchSpace();
    o += ",\"heapSize\":"; o += ESP.getHeapSize();
    o += ",\"heapFree\":"; o += ESP.getFreeHeap();
    o += ",\"heapMin\":"; o += ESP.getMinFreeHeap();
    o += ",\"heapMaxBlock\":"; o += ESP.getMaxAllocHeap();
    o += ",\"tempC\":"; o += String(temperatureRead(), 1);
    o += ",\"uptime\":"; o += uptimeSeconds();
    o += ",\"boots\":"; o += A.boots;
    o += ",\"resetReason\":"; jsonStr(o, resetReasonText());

    // Network
    o += ",\"ap\":"; o += gNet.ap ? "true" : "false";
    o += ",\"hostname\":"; jsonStr(o, HOSTNAME);
    o += ",\"ip\":"; jsonStr(o, gNet.ip);
    if (gNet.ap) {
        o += ",\"mac\":"; jsonStr(o, WiFi.softAPmacAddress().c_str());
        o += ",\"stations\":"; o += (int)WiFi.softAPgetStationNum();
    } else {
        o += ",\"mac\":"; jsonStr(o, WiFi.macAddress().c_str());
        o += ",\"rssi\":"; o += (int)WiFi.RSSI();
        o += ",\"channel\":"; o += (int)WiFi.channel();
        o += ",\"bssid\":"; jsonStr(o, WiFi.BSSIDstr().c_str());
        o += ",\"gateway\":"; jsonStr(o, WiFi.gatewayIP().toString().c_str());
        o += ",\"subnet\":"; jsonStr(o, WiFi.subnetMask().toString().c_str());
        o += ",\"dns\":"; jsonStr(o, WiFi.dnsIP().toString().c_str());
    }
    o += ",\"requests\":"; o += A.requests;

    // Drawing
    o += ",\"spiHz\":"; o += (uint32_t)TFT_SPI_HZ;
    o += ",\"frameMs\":"; o += (uint32_t)FRAME_MS;
    o += ",\"fps\":"; o += String(A.fps, 1);
    o += ",\"avgMs\":"; o += String(A.avgMs, 2);
    o += ",\"maxMs\":"; o += String(A.maxMs, 2);
    o += ",\"busyPct\":"; o += String(A.busyPct, 1);
    o += ",\"pxPerSec\":"; o += A.pxPerSec;
    o += ",\"winPerSec\":"; o += A.winPerSec;
    o += ",\"framesTotal\":"; o += gStats.frames;
    o += ",\"pixelsTotal\":"; o += gStats.pixels;

    // Usage
    o += ",\"faceSeconds\":[";
    for (int i = 0; i <= FACE_COUNT; i++) {
        if (i) o += ',';
        o += (uint32_t)(A.faceMs[i] / 1000);
    }
    o += "],\"examsStarted\":"; o += A.examsStarted;
    o += ",\"focusStarted\":"; o += A.focusStarted;
    o += '}';
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", o);
}

static void webBegin() {
    server.on("/", HTTP_GET, [] { server.send_P(200, "text/html", DASHBOARD_HTML); });
    server.on("/api/state", HTTP_GET, sendState);
    server.on("/api/set", HTTP_POST, handleSet);
    server.on("/api/action", HTTP_POST, handleAction);
    server.on("/api/time", HTTP_POST, handleTime);
    server.on("/api/wifi", HTTP_POST, handleWifi);
    server.on("/api/info", HTTP_GET, handleInfo);
    server.on("/api/weather", HTTP_ANY, handleWeather);
    server.on("/api/update", HTTP_POST, handleUpdate);
    server.onNotFound([] {
        server.sendHeader("Location", "/");
        server.send(302, "text/plain", "");
    });
    server.begin();
}
