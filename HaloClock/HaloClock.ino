// ---------------------------------------------------------------------------
// Halo — Smart Classroom Clock
// 2.1" 360x360 GC9B72 round TFT + Waveshare ESP32-C3 Zero, controlled from a
// Wi-Fi dashboard on any phone or laptop.
//
// Board: ESP32C3 Dev Module, USB CDC On Boot: Enabled, Partition Scheme: Huge APP
// Libraries: GFX Library for Arduino, Arduino_GC9B72
//
// Wi-Fi: put your network in WIFI_SSID / WIFI_PASS. If they're blank or the
// clock can't join, it makes its own network (AP_SSID / AP_PASS): join it and
// open http://192.168.4.1. Opening the dashboard also sets the clock's time
// from your device, so it works without internet.
//
// Drawing: each face describes its picture as a scene (faces.h) and the
// renderer (render.h) sends only the pixels that changed. tools/sim runs the
// same code on a computer to check for artifacts.
// ---------------------------------------------------------------------------

#include <WiFi.h>
#include <ESPmDNS.h>
#include <Arduino_GFX_Library.h>
#include <Arduino_GC9B72.h>

#define FIRMWARE_VERSION "1.2.0"

// ---- Wi-Fi -----------------------------------------------------------------
#if __has_include("secrets.h")
#include "secrets.h"   // git-ignored; see secrets.example.h
#else
const char *WIFI_SSID = "";
const char *WIFI_PASS = "";
#endif
const char *AP_SSID = "Halo-Clock";
const char *AP_PASS = "halo1234";  // at least 8 characters
const char *HOSTNAME = "halo";     // dashboard at http://halo.local

// ---- Display pins (ESP32-C3 Zero) -----------------------------------------
#define TFT_SCLK 4  // SCL
#define TFT_MOSI 6  // SDA
#define TFT_DC   5
#define TFT_CS   7
#define TFT_RST  3
#define TFT_BL   1

// The library defaults to 40 MHz, which corrupts pixels over jumper wires.
// 20 MHz is what the GC9B72 driver was tested at; try 40000000 once it's soldered with short wires.
#define TFT_SPI_HZ 40000000

#define FRAME_MS 16       // up to ~60 frames a second; frames with nothing new cost almost nothing
#define PERF_LOG 1        // print drawing stats to Serial every 10 s

Arduino_DataBus *bus = new Arduino_ESP32SPI(TFT_DC, TFT_CS, TFT_SCLK, TFT_MOSI, GFX_NOT_DEFINED);
Arduino_GC9B72 *tft = new Arduino_GC9B72(bus, TFT_RST, 0 /*rotation*/, false /*IPS*/, 360, 360);

// The renderer pushes pixels through these: one address window, then its rows in order.
static void sinkBegin() { tft->startWrite(); }
static void sinkWindow(int x, int y, int w, int h) { tft->writeAddrWindow(x, y, w, h); }
static void sinkPixels(uint16_t *px, int n) { tft->writePixels(px, n); }
static void sinkEnd() { tft->endWrite(); }

#include "settings.h"
#include "wifi_config.h"
#include "faces.h"
#include "analytics.h"
#include "web.h"

static void startWifi() {
    if (wifiJoin(25000)) {
        gNet.ap = false;
        strlcpy(gNet.ssid, W.ssid, sizeof(gNet.ssid));
        strlcpy(gNet.ip, WiFi.localIP().toString().c_str(), sizeof(gNet.ip));
        configTzTime(kCities[S.homeCity].posix, "pool.ntp.org", "time.google.com");
    } else {
        // Can't join: run our own network so the dashboard can fix the settings.
        WiFi.mode(WIFI_AP);
        WiFi.softAP(AP_SSID, AP_PASS);
        gNet.ap = true;
        strlcpy(gNet.ssid, AP_SSID, sizeof(gNet.ssid));
        strlcpy(gNet.ip, WiFi.softAPIP().toString().c_str(), sizeof(gNet.ip));
    }
    if (MDNS.begin(HOSTNAME)) MDNS.addService("http", "tcp", 80);
    Serial.printf("Dashboard: http://%s  (or http://%s.local)\n", gNet.ip, HOSTNAME);
}

void setup() {
    Serial.begin(115200);
    loadSettings();
    wifiLoad();
    analyticsBegin();
    applyHomeZone();

    ledcAttach(TFT_BL, 5000, 8);
    applyBrightness();

    if (!tft->begin(TFT_SPI_HZ)) Serial.println("Display init failed");
    tft->fillScreen(0x0000);
    drawClock();  // shows "Connecting to Wi-Fi..."

    startWifi();
    webBegin();
}

void loop() {
    server.handleClient();
    if (gRestartAt && (long)(millis() - gRestartAt) >= 0) ESP.restart();

    static unsigned long lastFrame = 0;
    unsigned long now = millis();
    if (now - lastFrame < FRAME_MS) {
        delay(1);  // let Wi-Fi and the idle task run
        return;
    }
    uint32_t gap = lastFrame ? now - lastFrame : 0;
    lastFrame = now;

    unsigned long t0 = micros();
    drawClock();
    analyticsFrame(micros() - t0, gap, timeValid() ? S.face : FACE_COUNT);
}
