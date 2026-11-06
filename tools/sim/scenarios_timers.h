// Simulator scenarios for the Exam and Focus faces.
// Start times are whole seconds, so startMs / 1000 is exact.
#pragma once

static void addTimersScenarios(std::vector<Scenario> &v) {
    // ---- Exam ----------------------------------------------------------------
    // Not started: full white ring, full duration, "Ready" line.
    v.push_back({"exam_ready", FACE_EXAM, "2026-09-15 09:00:00", 3, 100, nullptr, nullptr});

    // Running mid-exam at 30 fps: smooth drain, one countdown tick a second.
    v.push_back({"exam_running", FACE_EXAM, "2026-09-15 09:15:00", 6, 33,
                 +[](int64_t start) { S.examEnd = start / 1000 + 30 * 60; }, nullptr});

    // Crossing 05:00 (turns coral) at 30 fps.
    v.push_back({"exam_warn", FACE_EXAM, "2026-09-15 09:40:00", 6, 33,
                 +[](int64_t start) { S.examEnd = start / 1000 + 5 * 60 + 3; }, nullptr});

    // Last seconds: the arc shrinks into 12 o'clock, then "Time's up".
    v.push_back({"exam_end", FACE_EXAM, "2026-09-15 09:44:55", 8, 33,
                 +[](int64_t start) { S.examEnd = start / 1000 + 4; }, nullptr});

    // Past the end for a while (arc stays empty, nothing repaints).
    v.push_back({"exam_over", FACE_EXAM, "2026-09-15 10:00:00", 3, 100,
                 +[](int64_t start) { S.examEnd = start / 1000 - 600; }, nullptr});

    // 45 min exam crossing 22:30 (the arc passes 6 o'clock, where it's split).
    v.push_back({"exam_half", FACE_EXAM, "2026-09-15 09:22:28", 6, 33,
                 +[](int64_t start) { S.examEnd = start / 1000 + 22 * 60 + 32; }, nullptr});

    // Started, stopped and restarted from the dashboard mid-run.
    v.push_back({"exam_startstop", FACE_EXAM, "2026-09-15 09:00:00", 8, 33, nullptr,
                 +[](int64_t now, int64_t start) {
                     int64_t t = now - start;
                     int64_t want = t < 1000 ? 0 : t < 4000 ? start / 1000 + 1 + S.examMin * 60
                                  : t < 6000 ? 0 : start / 1000 + 6 + S.examMin * 60;
                     if (S.examEnd != want) { S.examEnd = want; settingsChanged(); }
                 }});

    // Subject and duration edited while running (static layer rebuilt, arc rescaled).
    v.push_back({"exam_edit", FACE_EXAM, "2026-09-15 09:00:00", 4, 33,
                 +[](int64_t start) { S.examEnd = start / 1000 + 20 * 60; },
                 +[](int64_t now, int64_t start) {
                     if (now - start == 1980 && S.examMin == 45) {
                         S.examMin = 30;
                         strlcpy(S.examSubject, "Chemistry", sizeof(S.examSubject));
                         settingsChanged();
                     }
                 }});

    // Long exam: 240 min, 3-digit minutes, very slow drain.
    v.push_back({"exam_long", FACE_EXAM, "2026-09-15 09:00:00", 10, 33,
                 +[](int64_t start) {
                     S.examMin = 240;
                     S.examEnd = start / 1000 + 240 * 60 - 61;
                 }, nullptr});
    v.push_back({"exam_long_ready", FACE_EXAM, "2026-09-15 09:00:00", 2, 100,
                 +[](int64_t start) {
                     S.examMin = 240;
                     strlcpy(S.examSubject, "Extended Investigation Paper 2 Wowzers", sizeof(S.examSubject));
                 }, nullptr});

    // 1 minute exam, start to finish at 30 fps: full ring, 6 o'clock, empty at 12.
    v.push_back({"exam_1min", FACE_EXAM, "2026-09-15 09:00:00", 63, 33,
                 +[](int64_t start) {
                     S.examMin = 1;
                     S.examEnd = start / 1000 + 60;
                 }, nullptr});
    // The same 1 minute exam snapshotted just past half way.
    v.push_back({"exam_1min_half", FACE_EXAM, "2026-09-15 09:00:00", 31.5, 33,
                 +[](int64_t start) {
                     S.examMin = 1;
                     S.examEnd = start / 1000 + 60;
                 }, nullptr});

    // "Finishes at" uses the dashboard's test offset.
    v.push_back({"exam_offset", FACE_EXAM, "2026-09-15 09:00:00", 2, 100,
                 +[](int64_t start) {
                     S.testOffset = 3 * 3600 + 17 * 60;
                     S.examEnd = start / 1000 + 25 * 60;
                 }, nullptr});

    // ---- Focus ---------------------------------------------------------------
    // Not started: the halo still breathes (over a full 5 s cycle).
    v.push_back({"focus_ready", FACE_FOCUS, "2026-09-15 09:00:00", 6, 33, nullptr, nullptr});

    // Running at 30 fps for more than one breath.
    v.push_back({"focus_breathe", FACE_FOCUS, "2026-09-15 09:00:00", 11, 33,
                 +[](int64_t start) { S.focusEnd = start / 1000 + 12 * 60 + 30; }, nullptr});

    // Completing.
    v.push_back({"focus_complete", FACE_FOCUS, "2026-09-15 09:19:55", 8, 33,
                 +[](int64_t start) { S.focusEnd = start / 1000 + 4; }, nullptr});

    // Started and stopped from the dashboard mid-run.
    v.push_back({"focus_startstop", FACE_FOCUS, "2026-09-15 09:00:00", 7, 33, nullptr,
                 +[](int64_t now, int64_t start) {
                     int64_t t = now - start;
                     int64_t want = t < 1500 ? 0 : t < 5000 ? start / 1000 + 2 + S.focusMin * 60 : 0;
                     if (S.focusEnd != want) { S.focusEnd = want; settingsChanged(); }
                 }});

    // 1 minute focus, start to finish; and a 180 minute one with a long label.
    v.push_back({"focus_1min", FACE_FOCUS, "2026-09-15 09:00:00", 62, 33,
                 +[](int64_t start) {
                     S.focusMin = 1;
                     S.focusEnd = start / 1000 + 60;
                 }, nullptr});
    v.push_back({"focus_long", FACE_FOCUS, "2026-09-15 09:00:00", 3, 33,
                 +[](int64_t start) {
                     S.focusMin = 180;
                     S.focusEnd = start / 1000 + 180 * 60 - 1;
                     strlcpy(S.focusLabel, "Independent research block", sizeof(S.focusLabel));
                 }, nullptr});
}
