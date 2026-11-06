// Simulator scenarios for the Learn face.
#pragma once

static void addLearnScenarios(std::vector<Scenario> &v) {
    // Minute hand ticking over several minutes in 1 s steps, crossing "five past" -> "ten past" -> "quarter past".
    v.push_back({"learn_ticking", FACE_LEARN, "2026-09-15 10:07:30", 400, 1000, nullptr, nullptr});
    // "five to ten" -> "ten o'clock" (10:58) -> "five past ten" (11:03).
    v.push_back({"learn_oclock", FACE_LEARN, "2026-09-15 09:57:40", 350, 1000, nullptr, nullptr});
    // "half past four" -> "twenty-five to five": the named hour rolls forward.
    v.push_back({"learn_half_to", FACE_LEARN, "2026-09-15 04:32:20", 90, 500, nullptr, nullptr});
    // 11:5x -> 12:0x: "ten to twelve", "five to twelve", "twelve o'clock", "five past twelve".
    v.push_back({"learn_noon", FACE_LEARN, "2026-09-15 11:51:30", 750, 1000, nullptr, nullptr});
    // Midnight: both hands wrap through 0 degrees and the date changes.
    v.push_back({"learn_midnight", FACE_LEARN, "2026-09-15 23:59:30", 60, 250, nullptr, nullptr});
    // Smooth 33 ms frames: only the snapped hand steps should cost anything.
    v.push_back({"learn_smooth", FACE_LEARN, "2026-09-15 08:21:10", 8, 33, nullptr, nullptr});
    // Twelve hours in 30 s steps: every phrase and every hand position.
    v.push_back({"learn_day", FACE_LEARN, "2026-09-15 06:00:00", 12 * 3600, 30000, nullptr, nullptr});
    // Dashboard saves every 3 s rebuild the static layer, which must cost nothing.
    v.push_back({"learn_settings", FACE_LEARN, "2026-09-15 14:44:00", 20, 100, nullptr,
                 +[](int64_t now, int64_t start) {
                     if (now > start && (now - start) % 3000 == 0) {
                         S.brightness = S.brightness == 80 ? 60 : 80;
                         settingsChanged();
                     }
                 }});
    // Switching to another face and back forces full repaints with a different background.
    v.push_back({"learn_switch", FACE_LEARN, "2026-09-15 13:14:50", 12, 250, nullptr,
                 +[](int64_t now, int64_t start) {
                     int64_t t = now - start;
                     S.face = (t >= 4000 && t < 7000) ? FACE_CLASSIC : FACE_LEARN;
                 }});
    // Snapshots for checking the layout of the longest phrases.
    v.push_back({"learn_look_1010", FACE_LEARN, "2026-09-15 10:09:58", 2, 500, nullptr, nullptr});
    v.push_back({"learn_look_0737", FACE_LEARN, "2026-09-15 07:36:58", 2, 500, nullptr, nullptr});
    v.push_back({"learn_look_1127", FACE_LEARN, "2026-09-15 11:26:58", 2, 500, nullptr, nullptr});
    v.push_back({"learn_look_0802", FACE_LEARN, "2026-09-15 08:01:58", 2, 500, nullptr, nullptr});
}
