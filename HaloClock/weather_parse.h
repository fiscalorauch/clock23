// Reads an Open-Meteo forecast response and names WMO weather codes.
// Plain C++ with no Arduino APIs, so the simulator tests the same code.
// A small field extractor rather than a JSON library: the response is about
// 1 KB with a fixed shape, and every section is one flat object.
#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "model.h"

// Finds "name":{ and returns the text after it, with *end at the closing brace.
static const char *wxSection(const char *json, const char *name, const char **end) {
    char key[24];
    snprintf(key, sizeof(key), "\"%s\":{", name);
    const char *p = strstr(json, key);
    if (!p) return nullptr;
    p += strlen(key);
    *end = strchr(p, '}');
    return *end ? p : nullptr;
}

static const char *wxField(const char *sec, const char *end, const char *name) {
    char key[40];
    snprintf(key, sizeof(key), "\"%s\":", name);
    const char *p = strstr(sec, key);
    if (!p || p >= end) return nullptr;
    return p + strlen(key);
}

static bool wxNum(const char *sec, const char *end, const char *name, float &out) {
    const char *p = wxField(sec, end, name);
    if (!p) return false;
    char *stop;
    out = strtof(p, &stop);
    return stop != p;
}

// Reads a number array into out and returns how many were read. A null counts
// as 0 so later hours keep their place.
static int wxArray(const char *sec, const char *end, const char *name, float *out, int max) {
    const char *p = wxField(sec, end, name);
    if (!p || *p != '[') return 0;
    p++;
    int n = 0;
    while (n < max && p < end && *p != ']') {
        char *stop;
        float v = strtof(p, &stop);
        if (stop == p) {
            if (strncmp(p, "null", 4) != 0) break;
            stop = (char *)p + 4;
            v = 0;
        }
        out[n++] = v;
        p = stop;
        if (*p == ',') p++;
    }
    return n;
}

// Needs timeformat=unixtime. Leaves out.fetchedAt for the caller. Returns false
// unless everything the face depends on is present and sensible.
static bool parseWeather(const char *json, WeatherData &out) {
    memset(&out, 0, sizeof(out));
    const char *cur, *curEnd, *hr, *hrEnd, *day, *dayEnd;
    if (!(cur = wxSection(json, "current", &curEnd))) return false;
    if (!(hr = wxSection(json, "hourly", &hrEnd))) return false;
    if (!(day = wxSection(json, "daily", &dayEnd))) return false;

    float code = 0, isDay = 1;
    if (!wxNum(cur, curEnd, "temperature_2m", out.tempC)) return false;
    if (!wxNum(cur, curEnd, "weather_code", code)) return false;
    if (!wxNum(cur, curEnd, "apparent_temperature", out.feelsC)) out.feelsC = out.tempC;
    wxNum(cur, curEnd, "is_day", isDay);
    if (out.tempC < -60 || out.tempC > 70 || code < 0 || code > 99) return false;
    out.code = (int16_t)code;
    out.isDay = isDay != 0;

    if (wxArray(day, dayEnd, "temperature_2m_max", out.highC, 2) < 1) return false;
    if (wxArray(day, dayEnd, "temperature_2m_min", out.lowC, 2) < 1) return false;

    const char *t = wxField(hr, hrEnd, "time");
    if (!t || *t != '[') return false;
    out.hourStart = strtoll(t + 1, nullptr, 10);
    if (out.hourStart <= 0) return false;
    float rain[WX_HOURS];
    int n = wxArray(hr, hrEnd, "precipitation_probability", rain, WX_HOURS);
    if (n < 1) return false;
    for (int i = 0; i < n; i++) out.rain[i] = rain[i] < 0 ? 0 : rain[i] > 100 ? 100 : (uint8_t)rain[i];
    return true;
}

enum WxIcon : uint8_t { WI_CLEAR, WI_PARTLY, WI_CLOUD, WI_FOG, WI_RAIN, WI_SNOW, WI_STORM };

static WxIcon wxIcon(int c) {
    if (c <= 1) return WI_CLEAR;
    if (c == 2) return WI_PARTLY;
    if (c == 3) return WI_CLOUD;
    if (c == 45 || c == 48) return WI_FOG;
    if ((c >= 71 && c <= 77) || c == 85 || c == 86) return WI_SNOW;
    if (c >= 95) return WI_STORM;
    return WI_RAIN;
}

static const char *wxWord(int c, bool day) {
    switch (c) {
        case 0: return day ? "Sunny" : "Clear";
        case 1: return day ? "Mostly sunny" : "Mostly clear";
        case 2: return "Partly cloudy";
        case 3: return "Overcast";
        case 45: case 48: return "Fog";
        case 51: case 53: case 55: return "Drizzle";
        case 56: case 57: return "Freezing drizzle";
        case 61: return "Light rain";
        case 63: return "Rain";
        case 65: return "Heavy rain";
        case 66: case 67: return "Freezing rain";
        case 71: return "Light snow";
        case 73: return "Snow";
        case 75: return "Heavy snow";
        case 77: return "Snow grains";
        case 80: return "Light showers";
        case 81: return "Showers";
        case 82: return "Heavy showers";
        case 85: case 86: return "Snow showers";
        case 95: return "Thunderstorm";
        case 96: case 99: return "Storm with hail";
        default: return "Unknown";
    }
}
