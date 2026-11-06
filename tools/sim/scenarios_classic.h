// Simulator scenarios for the Classic face.
#pragma once

static void addClassicScenarios(std::vector<Scenario> &v) {
    v.push_back({"classic_sweep", FACE_CLASSIC, "2026-09-15 10:09:50", 20, 33, nullptr, nullptr});
    v.push_back({"classic_hour", FACE_CLASSIC, "2026-09-15 10:58:00", 150, 1000, nullptr, nullptr});
    v.push_back({"classic_midnight", FACE_CLASSIC, "2026-09-15 23:59:40", 40, 250, nullptr, nullptr});
}
