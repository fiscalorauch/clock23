// Wi-Fi settings, saved on the clock so they can be changed without recompiling.
//
// Two kinds of network:
//   Home      an ordinary network with one password (WPA2-PSK).
//   School    WPA2-Enterprise (802.1X), where each person signs in with a
//             username and password. Schools normally use PEAP; a few use TTLS.
//
// Credentials live in the ESP32's flash (NVS), which is NOT encrypted: anyone
// who takes the clock and reads its flash can recover them. Ask your IT
// department for a device account rather than using your own login.
#pragma once
#include <WiFi.h>
#include <Preferences.h>

#define WIFI_KIND_HOME 0
#define WIFI_KIND_SCHOOL 1

struct WifiConfig {
    uint16_t version;
    uint8_t kind;       // WIFI_KIND_HOME or WIFI_KIND_SCHOOL
    uint8_t eapMethod;  // 0 = PEAP, 1 = TTLS
    char ssid[33];
    char pass[65];      // home network password
    char identity[64];  // outer identity; blank means "same as username"
    char username[64];
    char eapPass[64];
};

static const uint16_t kWifiVersion = 1;
WifiConfig W;
Preferences wifiPrefs;
unsigned long gRestartAt = 0;  // set after saving Wi-Fi settings, so the reply is sent first

// Falls back to the network written into the sketch (WIFI_SSID / WIFI_PASS).
static void wifiDefaults() {
    memset(&W, 0, sizeof(W));
    W.version = kWifiVersion;
    W.kind = WIFI_KIND_HOME;
    strlcpy(W.ssid, WIFI_SSID, sizeof(W.ssid));
    strlcpy(W.pass, WIFI_PASS, sizeof(W.pass));
}

static void wifiLoad() {
    wifiPrefs.begin("halowifi", false);
    if (wifiPrefs.getBytesLength("cfg") != sizeof(W) || wifiPrefs.getBytes("cfg", &W, sizeof(W)) != sizeof(W) ||
        W.version != kWifiVersion) {
        wifiDefaults();
    }
}

static void wifiSave() { wifiPrefs.putBytes("cfg", &W, sizeof(W)); }

// Joins the configured network. Returns false if it can't, so the caller can
// fall back to the clock's own hotspot.
static bool wifiJoin(uint32_t timeoutMs) {
    if (!strlen(W.ssid)) return false;
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(HOSTNAME);
    // If the C3 struggles to connect, or the screen glitches while Wi-Fi is busy:
    // WiFi.setTxPower(WIFI_POWER_8_5dBm);
    if (W.kind == WIFI_KIND_SCHOOL) {
        const char *identity = strlen(W.identity) ? W.identity : W.username;
        Serial.printf("Joining %s as %s (802.1X %s)", W.ssid, W.username, W.eapMethod ? "TTLS" : "PEAP");
        WiFi.begin(W.ssid, W.eapMethod ? WPA2_AUTH_TTLS : WPA2_AUTH_PEAP, identity, W.username, W.eapPass);
    } else {
        Serial.printf("Joining %s", W.ssid);
        WiFi.begin(W.ssid, W.pass);
    }
    for (unsigned long t0 = millis(); WiFi.status() != WL_CONNECTED && millis() - t0 < timeoutMs;) {
        delay(250);
        Serial.print('.');
    }
    Serial.println();
    return WiFi.status() == WL_CONNECTED;
}
