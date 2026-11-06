// Simulator scenarios for the Zones face.
#pragma once

static void addZonesScenarios(std::vector<Scenario> &v) {
    // Default cities (New York, London, Tokyo), home Melbourne. Several minute
    // changes in 1 s steps.
    v.push_back({"zones_minutes", FACE_ZONES, "2026-09-15 10:08:50", 200, 1000, nullptr, nullptr});

    // Local hour changes (10:59 -> 11:00), and every city's hour with it.
    v.push_back({"zones_hour", FACE_ZONES, "2026-09-15 10:58:30", 120, 1000, nullptr, nullptr});

    // Smooth 30 fps across a minute boundary.
    v.push_back({"zones_smooth", FACE_ZONES, "2026-09-15 10:09:50", 20, 33, nullptr, nullptr});

    // Half-hour offset (Mumbai) and the date line (Auckland ahead, Honolulu a day
    // behind), across local midnight.
    v.push_back({"zones_dateline", FACE_ZONES, "2026-09-15 23:58:30", 120, 1000,
                 +[](int64_t) {
                     S.zones[0] = 9;   // Mumbai
                     S.zones[1] = 12;  // Auckland
                     S.zones[2] = 13;  // Honolulu
                 },
                 nullptr});

    // A city leaves daylight saving: New York falls back from 01:59 EDT to 01:00
    // EST (06:00 UTC), so its hand jumps back 30 deg while local time runs on.
    v.push_back({"zones_dst", FACE_ZONES, "2026-11-01 16:58:30", 120, 1000, nullptr, nullptr});

    // Longest labels, with the local hands sweeping underneath the legend.
    // (Start times are Melbourne time; 15:28:40 there is 06:28:40 in London.)
    v.push_back({"zones_longnames", FACE_ZONES, "2026-09-15 15:28:40", 150, 1000,
                 +[](int64_t) {
                     S.zones[0] = 4;   // Los Angeles
                     S.zones[1] = 7;   // Singapore
                     S.zones[2] = 11;  // Melbourne
                     S.homeCity = 0;   // London
                     applyHomeZone();
                 },
                 nullptr});

    // Cities and then the home city change while the face is showing.
    v.push_back({"zones_settings", FACE_ZONES, "2026-09-15 10:09:20", 60, 500, nullptr,
                 +[](int64_t now, int64_t start) {
                     int64_t t = now - start;
                     if (t == 10000) {
                         S.zones[0] = 9;   // Mumbai
                         S.zones[1] = 4;   // Los Angeles
                         S.zones[2] = 12;  // Auckland
                         settingsChanged();
                     } else if (t == 25000) {
                         S.homeCity = 3;   // New York
                         applyHomeZone();
                         settingsChanged();
                     } else if (t == 40000) {
                         S.zones[1] = 7;   // Singapore
                         S.homeCity = 13;  // Honolulu
                         applyHomeZone();
                         settingsChanged();
                     }
                 }});
}
