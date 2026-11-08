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
    o.reserve(1600);
    o += "{\"firmware\":\"" FIRMWARE_VERSION "\"";
    o += ",\"deviceName\":"; jsonStr(o, gDeviceName);
    o += ",\"brightness\":"; o += S.brightness;
    o += ",\"home\":"; o += S.homeCity;
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
    o += ",\"rssi\":"; o += gNet.ap ? 0 : (int)WiFi.RSSI();
    o += ",\"uptime\":"; o += uptimeSeconds();
    {
        int64_t now = realNow();
        bool alarmOn = now < gHa.alarmUntil, noticeOn = now < gHa.noticeUntil && gHa.notice[0];
        o += ",\"ha\":{\"alarm\":\""; o += kHaAlarmNames[alarmOn ? gHa.alarm : HA_NONE]; o += '"';
        o += ",\"alarmFor\":"; o += alarmOn ? (long)(gHa.alarmUntil - now) : 0L;
        o += ",\"countdown\":"; o += haCountdownLeft(realNowMs());
        o += ",\"notice\":"; jsonStr(o, noticeOn ? gHa.notice : "");
        o += ",\"noticeFor\":"; o += noticeOn ? (long)(gHa.noticeUntil - now) : 0L;
        o += '}';
    }
    o += ",\"update\":{\"state\":\""; o += kUpdateStates[gUpState]; o += '"';
    o += ",\"latest\":"; jsonStr(o, gUpLatest);
    o += ",\"pct\":"; o += (int)gUpdatePct;
    o += ",\"error\":"; jsonStr(o, gUpError); o += '}';
    o += ",\"cities\":[";
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

// POST /api/set — any subset of: brightness, home, testOffset, lat with lon, place, nightOn,
// nightBrightness, nightStart, nightEnd, deviceName.
// No explicit redraw is needed: the next frame's scene differs and only that is repainted.
static void handleSet() {
    if (server.hasArg("brightness")) {
        S.brightness = constrain(server.arg("brightness").toInt(), 5, 100);
        applyBrightness();
    }
    if (server.hasArg("home")) {
        S.homeCity = constrain(server.arg("home").toInt(), 0, kNumCities - 1);
        applyHomeZone();
    }
    if (server.hasArg("testOffset")) S.testOffset = constrain(server.arg("testOffset").toInt(), -604800L, 604800L);
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

// POST /api/ha — status from Home Assistant: alarm=<alarm panel state>, optionally
// with countdown=<exit delay seconds left>&total=<its full length>, and/or
// notice=<short text> (empty clears it). Each lasts ttl seconds (default 15 min),
// so Home Assistant resends them; if it stops, the face clears itself.
static void handleHa() {
    long ttl = server.hasArg("ttl") ? constrain(server.arg("ttl").toInt(), 30L, 86400L) : 900L;
    int64_t now = realNow();
    if (server.hasArg("alarm")) {
        gHa.alarm = haAlarmFromState(server.arg("alarm").c_str());
        gHa.alarmUntil = now + ttl;
        // Sent with every state, so a stale countdown can't outlive the arming it belonged to.
        long countdown = server.arg("countdown").toInt();
        gHa.countdownUntil = countdown > 0 ? now + countdown : 0;
        gHa.countdownTotal = constrain(server.arg("total").toInt(), 0L, 600L);
    }
    if (server.hasArg("notice")) {
        copyArg("notice", gHa.notice, sizeof(gHa.notice));
        gHa.noticeUntil = gHa.notice[0] ? now + ttl : 0;
    }
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
    o += ']';
    o += '}';
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", o);
}

static void webBegin() {
    server.on("/", HTTP_GET, [] { server.send_P(200, "text/html", DASHBOARD_HTML); });
    server.on("/api/state", HTTP_GET, sendState);
    server.on("/api/set", HTTP_POST, handleSet);
    server.on("/api/time", HTTP_POST, handleTime);
    server.on("/api/wifi", HTTP_POST, handleWifi);
    server.on("/api/info", HTTP_GET, handleInfo);
    server.on("/api/weather", HTTP_ANY, handleWeather);
    server.on("/api/update", HTTP_POST, handleUpdate);
    server.on("/api/ha", HTTP_POST, handleHa);
    server.onNotFound([] {
        server.sendHeader("Location", "/");
        server.send(302, "text/plain", "");
    });
    server.begin();
}
