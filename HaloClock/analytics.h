// Diagnostics and usage stats, reported by GET /api/info for the app's analytics page.
//
// Drawing stats cover the last complete 10 second window. Face usage is since
// boot; boots are kept in flash across restarts.
#pragma once
#include <esp_system.h>
#include <esp_timer.h>
#include <Preferences.h>

struct Analytics {
    // Last complete window
    float fps, avgMs, maxMs, busyPct;
    uint32_t pxPerSec, winPerSec;
    // Window in progress
    unsigned long winStart;
    uint32_t frames, busy, maxUs, lastWindows;
    uint64_t totalUs;
    RenderStats winStats;
    // Since boot
    uint64_t faceMs[FACE_COUNT + 1];  // last slot: the setup screen
    uint32_t requests;
    // Kept in flash
    uint32_t boots;
};

Analytics A;
Preferences statPrefs;

static void analyticsBegin() {
    statPrefs.begin("halostats", false);
    A.boots = statPrefs.getUInt("boots", 0) + 1;
    statPrefs.putUInt("boots", A.boots);
    A.winStart = millis();
    A.winStats = gStats;
    A.lastWindows = gStats.windows;
}

// Called after every frame: how long drawing took, the time since the last
// frame, and which face was on screen.
static void analyticsFrame(uint32_t drawUs, uint32_t gapMs, int face) {
    A.faceMs[face] += gapMs;
    A.frames++;
    if (gStats.windows != A.lastWindows) {  // this frame sent pixels
        A.busy++;
        A.totalUs += drawUs;
        if (drawUs > A.maxUs) A.maxUs = drawUs;
        A.lastWindows = gStats.windows;
    }
    unsigned long now = millis(), span = now - A.winStart;
    if (span < 10000) return;

    A.fps = A.frames * 1000.0f / span;
    A.avgMs = A.busy ? A.totalUs / 1000.0f / A.busy : 0;
    A.maxMs = A.maxUs / 1000.0f;
    A.busyPct = A.frames ? 100.0f * A.busy / A.frames : 0;
    A.pxPerSec = (uint32_t)((uint64_t)(gStats.pixels - A.winStats.pixels) * 1000 / span);
    A.winPerSec = (uint32_t)((uint64_t)(gStats.windows - A.winStats.windows) * 1000 / span);
#if PERF_LOG
    Serial.printf("draw: %.1f fps, %.0f%% frames drawing, avg %.1f ms, max %.1f ms, %u px/s, %u windows/s, heap %u\n",
                  A.fps, A.busyPct, A.avgMs, A.maxMs, A.pxPerSec, A.winPerSec, ESP.getFreeHeap());
#endif
    A.winStart = now;
    A.frames = A.busy = A.maxUs = 0;
    A.totalUs = 0;
    A.winStats = gStats;
}

static uint32_t uptimeSeconds() { return (uint32_t)(esp_timer_get_time() / 1000000); }

static const char *resetReasonText() {
    switch (esp_reset_reason()) {
        case ESP_RST_POWERON:  return "Power on";
        case ESP_RST_SW:       return "Restarted by software";
        case ESP_RST_PANIC:    return "Crash";
        case ESP_RST_INT_WDT:
        case ESP_RST_TASK_WDT:
        case ESP_RST_WDT:      return "Watchdog";
        case ESP_RST_BROWNOUT: return "Brownout (power dip)";
        case ESP_RST_DEEPSLEEP:return "Woke from sleep";
        case ESP_RST_EXT:      return "Reset pin";
        case ESP_RST_USB:      return "USB reset";
        default:               return "Unknown";
    }
}
