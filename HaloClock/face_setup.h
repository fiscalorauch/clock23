// Setup screen, shown until the clock knows the time.
#pragma once
#include "faces_common.h"

extern const char *AP_PASS;

static void setupDynamic(Scene &s) {
    addRing(s, SCR_C, SCR_C, 163, 171, C_TRACK);
    addText(s, "Halo", SCR_C, 92, F40, C_TEXT);
    addText(s, "CLOCK SETUP", SCR_C, 130, F13, C_FAINT, ALIGN_CENTER, 3);
    if (!gNet.ip[0]) {
        addText(s, "Connecting to Wi-Fi...", SCR_C, 190, F16, C_DIM);
    } else if (gNet.ap) {
        char pw[40];
        snprintf(pw, sizeof(pw), "password  %s", AP_PASS);
        addText(s, "1. Join Wi-Fi", SCR_C, 170, F16, C_DIM);
        addText(s, gNet.ssid, SCR_C, 195, F21, C_MINT);
        addText(s, pw, SCR_C, 218, F13, C_DIM);
        addText(s, "2. Open", SCR_C, 246, F16, C_DIM);
        addText(s, gNet.ip, SCR_C, 270, F21, C_TEXT);
    } else {
        addText(s, "Open the dashboard", SCR_C, 176, F16, C_DIM);
        addText(s, "halo.local", SCR_C, 204, F21, C_MINT);
        addText(s, gNet.ip, SCR_C, 230, F16, C_DIM);
        addText(s, "to set the time", SCR_C, 256, F13, C_FAINT);
    }
}
