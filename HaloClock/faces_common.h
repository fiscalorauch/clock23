// Shared palette and helpers for the clock faces.
// Layouts follow Code/index.html (400 px prototype) scaled to the 360 px screen.
#pragma once
#include <stdio.h>
#include <ctype.h>
#include <time.h>
#include "model.h"
#include "render.h"

#define HEX565(h) (uint16_t)((((h) >> 16 & 0xF8) << 8) | (((h) >> 8 & 0xFC) << 3) | (((h) & 0xFF) >> 3))

static const uint16_t C_BLACK = 0x0000;
static const uint16_t C_WHITE = 0xFFFF;
static const uint16_t C_TEXT = HEX565(0xf5f5f7);
static const uint16_t C_DIM = HEX565(0x98989d);
static const uint16_t C_FAINT = HEX565(0x5a5a5f);
static const uint16_t C_TRACK = HEX565(0x242426);
static const uint16_t C_PEACH = HEX565(0xffd3b6);
static const uint16_t C_CORAL = HEX565(0xffaaa5);
static const uint16_t C_MINT = HEX565(0xa8e6cf);

// Everything a face needs to know about "now", read once per frame.
struct FrameTime {
    struct tm local;  // displayed local time (includes the test offset)
    int ms;           // milliseconds into the current second
    int64_t realMs;   // real epoch milliseconds, for timers
};

static void upperCopy(char *dst, const char *src, size_t n) {
    size_t i = 0;
    for (; src[i] && i + 1 < n; i++) dst[i] = toupper((uint8_t)src[i]);
    dst[i] = 0;
}

// Rounds an angle to a step so slow hands only change when they visibly move.
static float snapDeg(float deg, float step) { return roundf(deg / step) * step; }
