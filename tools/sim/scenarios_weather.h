// Simulator scenarios for the Weather face and the update screen.
#pragma once

// A real Open-Meteo response, trimmed only in its "generationtime" digits.
static const char *kWxSample =
    "{\"latitude\":-37.785587,\"longitude\":144.93976,\"generationtime_ms\":0.229,\"utc_offset_seconds\":39600,"
    "\"timezone\":\"Australia/Melbourne\",\"timezone_abbreviation\":\"GMT+11\",\"elevation\":21.0,"
    "\"current_units\":{\"time\":\"unixtime\",\"interval\":\"seconds\",\"temperature_2m\":\"C\","
    "\"apparent_temperature\":\"C\",\"weather_code\":\"wmo code\",\"is_day\":\"\"},"
    "\"current\":{\"time\":1793925900,\"interval\":900,\"temperature_2m\":18.3,\"apparent_temperature\":16.4,"
    "\"weather_code\":2,\"is_day\":1},"
    "\"hourly_units\":{\"time\":\"unixtime\",\"precipitation_probability\":\"%\"},"
    "\"hourly\":{\"time\":[1793923200,1793926800,1793930400,1793934000,1793937600,1793941200,1793944800,"
    "1793948400,1793952000,1793955600,1793959200,1793962800],"
    "\"precipitation_probability\":[0,0,0,0,0,0,0,0,0,2,16,34]},"
    "\"daily_units\":{\"time\":\"unixtime\",\"temperature_2m_max\":\"C\",\"temperature_2m_min\":\"C\"},"
    "\"daily\":{\"time\":[1793883600,1793970000],\"temperature_2m_max\":[21.1,12.6],\"temperature_2m_min\":[9.5,9.1]}}";

static WeatherData wxSample(int64_t startMs, int code, float temp, bool day) {
    WeatherData w = {};
    w.fetchedAt = startMs / 1000 - 300;
    w.hourStart = startMs / 1000 / 3600 * 3600;
    w.tempC = temp;
    w.feelsC = temp - 2;
    w.highC[0] = temp + 4; w.lowC[0] = temp - 8;
    w.highC[1] = temp + 1; w.lowC[1] = temp - 6;
    w.code = code;
    w.isDay = day;
    const uint8_t rain[WX_HOURS] = {5, 10, 25, 45, 70, 90, 60, 30, 15, 5, 0, 0};
    memcpy(w.rain, rain, sizeof(rain));
    return w;
}

static void wxPartly(int64_t t) { publishWeather(wxSample(t, 2, 18.3f, true)); }
static void wxSun(int64_t t) { publishWeather(wxSample(t, 0, 27, true)); }
static void wxMoon(int64_t t) { publishWeather(wxSample(t, 0, 12, false)); }
static void wxRain(int64_t t) { publishWeather(wxSample(t, 63, 14, true)); }
static void wxStorm(int64_t t) { publishWeather(wxSample(t, 99, -3, false)); }
static void wxFog(int64_t t) { publishWeather(wxSample(t, 45, 7, true)); }
static void wxSnow(int64_t t) { publishWeather(wxSample(t, 73, -12, true)); }
static void wxOvercast(int64_t t) { publishWeather(wxSample(t, 3, 11, true)); }

static void wxStale(int64_t t) {
    WeatherData w = wxSample(t, 80, 16, true);
    w.fetchedAt = t / 1000 - 3 * 3600;
    w.hourStart = t / 1000 / 3600 * 3600 - 3 * 3600;
    publishWeather(w);
}

// Every code the service can send, one per second, as a new fetch would arrive.
static void wxCycle(int64_t now, int64_t start) {
    static const int codes[] = {0, 1, 2, 3, 45, 48, 51, 53, 55, 56, 57, 61, 63, 65, 66, 67, 71, 73, 75, 77, 80, 81, 82, 85, 86, 95, 96, 99};
    long step = (long)((now - start) / 1000);
    int code = codes[step % (sizeof(codes) / sizeof(codes[0]))];
    if ((now - start) % 1000 == 0) publishWeather(wxSample(start, code, -20 + step * 2, step % 2));
}

static void wxArrives(int64_t now, int64_t start) {
    if (now - start == 3000) publishWeather(wxSample(start, 61, 15, true));
}

static void updateRun(int64_t now, int64_t start) { gUpdatePct = (int8_t)((now - start) / 200 > 100 ? 100 : (now - start) / 200); }

static void addWeatherScenarios(std::vector<Scenario> &v) {
    v.push_back({"weather_partly", FACE_WEATHER, "2026-09-15 10:09:50", 20, 33, wxPartly, nullptr});
    v.push_back({"weather_sun", FACE_WEATHER, "2026-12-20 13:30:00", 2, 100, wxSun, nullptr});
    v.push_back({"weather_moon", FACE_WEATHER, "2026-09-15 22:45:00", 2, 100, wxMoon, nullptr});
    v.push_back({"weather_rain", FACE_WEATHER, "2026-09-15 16:20:00", 2, 100, wxRain, nullptr});
    v.push_back({"weather_storm", FACE_WEATHER, "2026-09-15 19:05:00", 2, 100, wxStorm, nullptr});
    v.push_back({"weather_fog", FACE_WEATHER, "2026-09-15 07:12:00", 2, 100, wxFog, nullptr});
    v.push_back({"weather_snow", FACE_WEATHER, "2026-09-15 11:11:00", 2, 100, wxSnow, nullptr});
    v.push_back({"weather_overcast", FACE_WEATHER, "2026-09-15 12:00:00", 2, 100, wxOvercast, nullptr});
    v.push_back({"weather_stale", FACE_WEATHER, "2026-09-15 09:15:00", 2, 100, wxStale, nullptr});
    v.push_back({"weather_waiting", FACE_WEATHER, "2026-09-15 09:15:00", 2, 100, nullptr, nullptr});
    v.push_back({"weather_arrives", FACE_WEATHER, "2026-09-15 09:15:00", 6, 100, nullptr, wxArrives});
    v.push_back({"weather_codes", FACE_WEATHER, "2026-09-15 15:00:00", 28, 100, nullptr, wxCycle});
    v.push_back({"weather_hours", FACE_WEATHER, "2026-09-15 23:30:00", 14 * 3600, 60000, wxRain, nullptr});
    v.push_back({"update_progress", FACE_WEATHER, "2026-09-15 09:15:00", 21, 100, wxPartly, updateRun});
}

// Checks the parser against a real response and against damaged ones.
static int weatherParserTests() {
    int bad = 0;
    auto check = [&](bool ok, const char *what) { if (!ok) { printf("  ! parser: %s\n", what); bad++; } };
    WeatherData w;
    check(parseWeather(kWxSample, w), "real response rejected");
    check(fabsf(w.tempC - 18.3f) < 0.01f, "temperature");
    check(fabsf(w.feelsC - 16.4f) < 0.01f, "feels like");
    check(w.code == 2 && w.isDay == 1, "code and day");
    check(fabsf(w.highC[0] - 21.1f) < 0.01f && fabsf(w.lowC[1] - 9.1f) < 0.01f, "high and low");
    check(w.hourStart == 1793923200LL, "first hour");
    check(w.rain[9] == 2 && w.rain[11] == 34, "rain");
    check(!parseWeather("", w), "empty text accepted");
    check(!parseWeather("{\"error\":true,\"reason\":\"limit\"}", w), "error reply accepted");
    std::string cut(kWxSample);
    for (size_t n = 0; n < cut.size(); n += 7) {
        WeatherData t;
        std::string part = cut.substr(0, n);
        if (part.find("\"daily\":{") == std::string::npos) check(!parseWeather(part.c_str(), t), "cut-off reply accepted");
    }
    check(versionNewer("v1.3.0", "1.2.9"), "version newer");
    check(versionNewer("1.10.0", "1.9.9"), "version numeric");
    check(!versionNewer("v1.3.0", "1.3.0"), "version same");
    check(!versionNewer("1.2.0", "1.3.0"), "version older");
    S.nightOn = 1; S.nightStart = 1290; S.nightEnd = 420;
    check(nightActive(1320) && nightActive(60) && !nightActive(420) && !nightActive(720), "night across midnight");
    S.nightStart = 60; S.nightEnd = 120;
    check(nightActive(60) && !nightActive(120), "night same day");
    S.nightOn = 0;
    check(!nightActive(90), "night off");
    printf("parser and logic tests: %s\n", bad ? "FAIL" : "PASS");
    return bad;
}
