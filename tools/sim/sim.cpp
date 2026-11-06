// Halo clock simulator: runs the real faces and renderer on a Mac.
//
// For each scenario it plays frames forward in time exactly like the clock,
// and regularly checks the incrementally updated panel against a fresh full
// repaint of the same frame. Any difference is a drawing artifact (something
// changed that the renderer didn't repaint), so the run fails.
//
// It also reports how many pixels and SPI windows each frame needed, and
// writes PNG snapshots to tools/sim/out/.
//
// Build and run from the "OLED Code" folder:
//   tools/sim/run.sh [name-filter]              all faces
//   ONLY=learn tools/sim/run.sh [name-filter]   one face file (classic, learn, timers, zones, schedule)
#include <sys/stat.h>
#include <chrono>
#include <string>
#include <vector>

#include "host_platform.h"
#include "../../HaloClock/faces.h"

static const char *kOutDir = "tools/sim/out";

struct Scenario {
    const char *name;    // start with the face name so filters work
    int face;
    const char *start;   // local time "YYYY-MM-DD HH:MM:SS" in the home zone (Melbourne by default)
    double seconds;      // how long to run
    int stepMs;          // frame interval
    void (*setup)(int64_t startMs);                     // optional: adjust S before the first frame
    void (*everyFrame)(int64_t nowMs, int64_t startMs);  // optional: change S mid-run (call settingsChanged())
};

// Call after changing S mid-run, like the web dashboard does.
static void settingsChanged() { gSettingsRev++; }

static void savePNG(const uint16_t *fb, const std::string &name) {
    std::string bmp = std::string(kOutDir) + "/" + name + ".bmp";
    std::string png = std::string(kOutDir) + "/" + name + ".png";
    FILE *f = fopen(bmp.c_str(), "wb");
    if (!f) return;
    const int W = 360, H = 360, rowSize = (W * 3 + 3) & ~3, dataSize = rowSize * H;
    uint8_t hdr[54] = {'B', 'M'};
    auto put32 = [&](int off, uint32_t v) { for (int i = 0; i < 4; i++) hdr[off + i] = v >> (8 * i); };
    put32(2, 54 + dataSize); put32(10, 54); put32(14, 40); put32(18, W); put32(22, H);
    hdr[26] = 1; hdr[28] = 24; put32(34, dataSize);
    fwrite(hdr, 1, 54, f);
    std::vector<uint8_t> row(rowSize);
    for (int y = H - 1; y >= 0; y--) {
        for (int x = 0; x < W; x++) {
            uint16_t c = fb[y * W + x];
            row[x * 3 + 2] = ((c >> 11) & 31) * 255 / 31;
            row[x * 3 + 1] = ((c >> 5) & 63) * 255 / 63;
            row[x * 3 + 0] = (c & 31) * 255 / 31;
        }
        fwrite(row.data(), 1, rowSize, f);
    }
    fclose(f);
    std::string cmd = "sips -s format png '" + bmp + "' --out '" + png + "' >/dev/null 2>&1 && rm '" + bmp + "'";
    system(cmd.c_str());
}

static bool inCircle(int x, int y) {
    int dx = 2 * x + 1 - 360, dy = 2 * y + 1 - 360;
    return dx * dx + dy * dy <= 358 * 358;
}

// Repaints the current frame from scratch into panel B and counts differing pixels.
static long checkAgainstFull(const std::string &label) {
    RendererState saved = gR;
    int savedShown = gShownFace;
    memset(&gR, 0, sizeof(gR));
    gR.forceFull = true;
    for (int i = 0; i < 360 * 360; i++) gPanelB[i] = 0xF81F;  // magenta: anything unpainted stands out
    RenderStats savedStats = gStats;
    gPanel = gPanelB;
    drawClock();
    gPanel = gPanelA;
    gStats = savedStats;
    gR = saved;
    gShownFace = savedShown;

    long diff = 0;
    static uint16_t diffImg[360 * 360];
    for (int y = 0; y < 360; y++)
        for (int x = 0; x < 360; x++) {
            int i = y * 360 + x;
            bool bad = inCircle(x, y) && gPanelA[i] != gPanelB[i];
            diff += bad;
            diffImg[i] = bad ? 0xF800 : (gPanelA[i] >> 2 & 0x39E7);
        }
    if (diff) {
        savePNG(gPanelA, label + "_incremental");
        savePNG(gPanelB, label + "_full");
        savePNG(diffImg, label + "_diff");
    }
    return diff;
}

static int64_t parseLocal(const char *s) {
    struct tm t = {};
    sscanf(s, "%d-%d-%d %d:%d:%d", &t.tm_year, &t.tm_mon, &t.tm_mday, &t.tm_hour, &t.tm_min, &t.tm_sec);
    t.tm_year -= 1900;
    t.tm_mon -= 1;
    t.tm_isdst = -1;
    return (int64_t)mktime(&t) * 1000;
}

static bool runScenario(const Scenario &sc) {
    defaultSettings();
    S.face = sc.face;
    applyHomeZone();
    int64_t start = parseLocal(sc.start);
    if (sc.setup) sc.setup(start);
    gSettingsRev++;

    memset(&gR, 0, sizeof(gR));
    gShownFace = -1;
    gSceneOverflow = 0;
    gOutOfWindow = 0;
    memset(gPanelA, 0, sizeof(gPanelA));

    long frames = (long)(sc.seconds * 1000 / sc.stepMs) + 1;
    long badFrames = 0, worstDiff = 0, checks = 0;
    uint64_t totalPx = 0, totalWin = 0;
    uint32_t maxPx = 0, maxWin = 0;
    uint16_t maxPrims = 0, maxGlyphs = 0;
    double hostMs = 0;
    for (long f = 0; f < frames; f++) {
        int64_t now = start + f * (int64_t)sc.stepMs;
        gSimEpochMs = now;
        if (sc.everyFrame) sc.everyFrame(now, start);
        RenderStats before = gStats;
        auto t0 = std::chrono::steady_clock::now();
        drawClock();
        hostMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        const Scene &shown = gR.scenes[gR.cur];
        if (shown.n > maxPrims) maxPrims = shown.n;
        if (shown.ng > maxGlyphs) maxGlyphs = shown.ng;
        uint32_t px = gStats.pixels - before.pixels, win = gStats.windows - before.windows;
        if (f > 0) {  // frame 0 is always a full paint
            totalPx += px; totalWin += win;
            if (px > maxPx) maxPx = px;
            if (win > maxWin) maxWin = win;
        }
        if (f % 7 == 3 || f == frames - 1) {
            checks++;
            long d = checkAgainstFull(std::string(sc.name) + "_f" + std::to_string(f));
            if (d) {
                badFrames++;
                if (d > worstDiff) worstDiff = d;
                if (badFrames <= 3) printf("  ! frame %ld: %ld pixels differ from a full repaint\n", f, d);
            }
        }
    }
    savePNG(gPanelA, sc.name);

    double avgPx = frames > 1 ? (double)totalPx / (frames - 1) : 0, avgWin = frames > 1 ? (double)totalWin / (frames - 1) : 0;
    // Rough ESP32-C3 cost at 20 MHz SPI: ~1.8 us per pixel (render + send), ~0.15 ms per window.
    double estAvg = avgPx * 0.0018 + avgWin * 0.15, estMax = maxPx * 0.0018 + maxWin * 0.15;
    bool ok = badFrames == 0 && gSceneOverflow == 0 && gOutOfWindow == 0;
    printf("%-24s %s  frames %4ld  checks %3ld  avg %6.0f px %4.1f win (~%4.1f ms)  max %6u px %3u win (~%5.1f ms)  prims %3u glyphs %3u  host %.2f ms/frame%s%s\n",
           sc.name, ok ? "PASS" : "FAIL", frames, checks, avgPx, avgWin, estAvg, maxPx, maxWin, estMax, maxPrims, maxGlyphs,
           hostMs / frames, gSceneOverflow ? "  SCENE OVERFLOW" : "", gOutOfWindow ? "  WINDOW OVERRUN" : "");
    return ok;
}

// ---- Scenarios (one file per face) ----------------------------------------
#if !defined(ONLY_FACE) || defined(ONLY_CLASSIC)
#include "scenarios_classic.h"
#endif
#if !defined(ONLY_FACE) || defined(ONLY_LEARN)
#include "scenarios_learn.h"
#endif
#if !defined(ONLY_FACE) || defined(ONLY_TIMERS)
#include "scenarios_timers.h"
#endif
#if !defined(ONLY_FACE) || defined(ONLY_ZONES)
#include "scenarios_zones.h"
#endif
#if !defined(ONLY_FACE) || defined(ONLY_SCHEDULE)
#include "scenarios_schedule.h"
#endif

int main(int argc, char **argv) {
    mkdir(kOutDir, 0755);
    std::vector<Scenario> all;
    all.push_back({"setup", FACE_CLASSIC, "2000-01-01 00:00:00", 2, 100, nullptr, nullptr});
#if !defined(ONLY_FACE) || defined(ONLY_CLASSIC)
    addClassicScenarios(all);
#endif
#if !defined(ONLY_FACE) || defined(ONLY_LEARN)
    addLearnScenarios(all);
#endif
#if !defined(ONLY_FACE) || defined(ONLY_TIMERS)
    addTimersScenarios(all);
#endif
#if !defined(ONLY_FACE) || defined(ONLY_ZONES)
    addZonesScenarios(all);
#endif
#if !defined(ONLY_FACE) || defined(ONLY_SCHEDULE)
    addScheduleScenarios(all);
#endif

    const char *only = argc > 1 ? argv[1] : nullptr;
    int failed = 0, ran = 0;
    for (const Scenario &sc : all) {
        if (only && !strstr(sc.name, only)) continue;
        ran++;
        if (!runScenario(sc)) failed++;
    }
    printf("\n%d scenario(s), %d failed\n", ran, failed);
    return failed ? 1 : 0;
}
