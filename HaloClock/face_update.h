// Shown while the clock downloads and installs new firmware.
#pragma once
#include "faces_common.h"

static void updateDynamic(Scene &s) {
    int pct = gUpdatePct < 0 ? 0 : gUpdatePct;
    char b[8];
    snprintf(b, sizeof(b), "%d%%", pct);
    addRing(s, SCR_C, SCR_C, 163, 171, C_TRACK);
    addRoundArc(s, 167, 8, 0, pct * 3.6f, C_MINT);
    addText(s, "Updating", SCR_C, 146, F34, C_TEXT);
    addText(s, b, SCR_C, 192, F26, C_MINT);
    addText(s, "Please keep the clock plugged in", SCR_C, 234, F13, C_DIM);
}
