// Everything that talks to the internet, on one low-priority FreeRTOS task, so
// a slow request never freezes the screen or the dashboard. One task also means
// a weather fetch and a firmware download can never run at once.
//
// Included only by the sketch. The faces read gWx and gUpdatePct from model.h.
#pragma once
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <Update.h>
#include "weather_parse.h"
#include "update_roots.h"

#define WX_EVERY_MS (20UL * 60000UL)  // well inside Open-Meteo's free limits
#define WX_RETRY_MS (5UL * 60000UL)

// Fixed on purpose: the dashboard can ask for an update, never choose where it comes from.
#define UPDATE_BASE "https://github.com/fiscalorauch/clock23/releases"
#define UPDATE_ASSET "halo-firmware.bin"

enum UpdateState : uint8_t { UP_IDLE, UP_CHECKING, UP_CURRENT, UP_AVAILABLE, UP_INSTALLING, UP_DONE, UP_FAILED };
static const char *const kUpdateStates[] = {"idle", "checking", "current", "available", "installing", "done", "failed"};

static volatile UpdateState gUpState = UP_IDLE;
static char gUpLatest[16] = "";
static char gUpError[64] = "";

// Requests from the web server, picked up by the task.
static volatile bool gWxRefresh = false, gUpCheckReq = false, gUpInstallReq = false;

static bool weatherFetch() {
    char url[340];
    snprintf(url, sizeof(url),
             "http://api.open-meteo.com/v1/forecast?latitude=%.3f&longitude=%.3f"
             "&current=temperature_2m,apparent_temperature,weather_code,is_day"
             "&hourly=precipitation_probability&daily=temperature_2m_max,temperature_2m_min"
             "&forecast_days=2&forecast_hours=%d&timezone=auto&timeformat=unixtime",
             S.lat, S.lon, WX_HOURS);
    WiFiClient client;
    HTTPClient http;
    http.setConnectTimeout(6000);
    http.setTimeout(6000);
    if (!http.begin(client, url)) return false;
    int code = http.GET();
    bool ok = false;
    if (code == 200) {
        String body = http.getString();
        WeatherData w;
        if (parseWeather(body.c_str(), w)) {
            w.fetchedAt = time(nullptr);
            publishWeather(w);
            ok = true;
        }
    }
    http.end();
    if (!ok) Serial.printf("Weather fetch failed (HTTP %d)\n", code);
    return ok;
}

static void updateFail(const char *why) {
    Serial.printf("Update: %s\n", why);
    strlcpy(gUpError, why, sizeof(gUpError));
    gUpdatePct = -1;
    gUpState = UP_FAILED;
}

// github.com/.../releases/latest redirects to .../releases/tag/<newest tag>.
static bool updateCheck() {
    gUpError[0] = 0;
    gUpState = UP_CHECKING;
    NetworkClientSecure client;
    client.setCACert(UPDATE_ROOTS);
    HTTPClient http;
    http.setConnectTimeout(10000);
    http.setTimeout(10000);
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    const char *headers[] = {"Location"};
    http.collectHeaders(headers, 1);
    if (!http.begin(client, UPDATE_BASE "/latest")) {
        updateFail("Couldn't reach GitHub");
        return false;
    }
    int code = http.GET();
    String where = http.header("Location");
    http.end();
    if (code != 301 && code != 302) {
        updateFail(code < 0 ? "Couldn't reach GitHub" : "GitHub gave an unexpected reply");
        return false;
    }
    int at = where.indexOf("/tag/");
    if (at < 0) {
        updateFail("No releases published yet");
        return false;
    }
    String tag = where.substring(at + 5);
    bool clean = tag.length() > 0 && tag.length() < (int)sizeof(gUpLatest);
    for (size_t i = 0; clean && i < tag.length(); i++)
        clean = isdigit((uint8_t)tag[i]) || tag[i] == '.' || tag[i] == 'v';
    if (!clean) {
        updateFail("Release has an unexpected name");
        return false;
    }
    strlcpy(gUpLatest, tag.c_str(), sizeof(gUpLatest));
    gUpState = versionNewer(gUpLatest, FIRMWARE_VERSION) ? UP_AVAILABLE : UP_CURRENT;
    return true;
}

// Writes to the unused app slot, so a failed download leaves this firmware
// untouched. The new image's checksum is verified before it can ever boot.
static void updateInstall() {
    if (gUpState != UP_AVAILABLE && (!updateCheck() || gUpState != UP_AVAILABLE)) return;
    if (ESP.getFreeHeap() < 70000) {
        updateFail("Not enough free memory");
        return;
    }
    gUpState = UP_INSTALLING;
    gUpdatePct = 0;

    char url[140];
    snprintf(url, sizeof(url), UPDATE_BASE "/download/%s/" UPDATE_ASSET, gUpLatest);
    NetworkClientSecure client;
    client.setCACert(UPDATE_ROOTS);
    HTTPClient http;
    http.setConnectTimeout(10000);
    http.setTimeout(15000);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);  // release files live on another host
    if (!http.begin(client, url)) {
        updateFail("Couldn't reach GitHub");
        return;
    }
    int code = http.GET();
    int total = http.getSize();
    if (code != 200 || total <= 0) {
        http.end();
        updateFail("Download failed");
        return;
    }
    if (!Update.begin(total, U_FLASH)) {
        http.end();
        updateFail("Firmware is too big for this clock");
        return;
    }
    static uint8_t buf[1024];
    WiFiClient *stream = http.getStreamPtr();
    int done = 0;
    unsigned long lastData = millis();
    while (done < total) {
        size_t waiting = stream->available();
        if (waiting) {
            int n = stream->readBytes(buf, waiting > sizeof(buf) ? sizeof(buf) : waiting);
            if (n <= 0 || Update.write(buf, n) != (size_t)n) break;
            done += n;
            gUpdatePct = (int8_t)((int64_t)done * 100 / total);
            lastData = millis();
        } else if (millis() - lastData > 20000 || !http.connected()) {
            break;
        } else {
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }
    http.end();
    if (done != total) {
        Update.abort();
        updateFail("Download was interrupted");
        return;
    }
    if (!Update.end()) {
        updateFail("Firmware failed its check");
        return;
    }
    gUpState = UP_DONE;
    gRestartAt = millis() + 1500;
}

static void netTask(void *) {
    unsigned long nextWx = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(250));
        if (gNet.ap || WiFi.status() != WL_CONNECTED) continue;
        if (gUpCheckReq) {
            gUpCheckReq = false;
            updateCheck();
        }
        if (gUpInstallReq) {
            gUpInstallReq = false;
            updateInstall();
        }
        if (gUpState == UP_DONE || !timeValid()) continue;
        if (gWxRefresh || (long)(millis() - nextWx) >= 0) {
            gWxRefresh = false;
            nextWx = millis() + (weatherFetch() ? WX_EVERY_MS : WX_RETRY_MS);
        }
    }
}

static void netBegin() {
    xTaskCreate(netTask, "net", 9000, nullptr, tskIDLE_PRIORITY, nullptr);
}
