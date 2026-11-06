// Simulator scenarios for the Schedule face.
// Default timetable: Maths 9:00, English 10:00, Recess 11:00, Science 11:30,
// Lunch 12:30, Art 13:15, Sport 14:15-15:30.
#pragma once

static void schSetPeriod(int i, const char *name, int start, int end) {
    strlcpy(S.periods[i].name, name, sizeof(S.periods[i].name));
    S.periods[i].start = start;
    S.periods[i].end = end;
}

static void addScheduleScenarios(std::vector<Scenario> &v) {
    // Snapshots of each state (last frame is the PNG).
    v.push_back({"schedule_early", FACE_SCHEDULE, "2026-09-15 07:30:00", 3, 250, nullptr, nullptr});
    v.push_back({"schedule_glide", FACE_SCHEDULE, "2026-09-15 09:20:00", 20, 33, nullptr, nullptr});

    // Before school -> first period.
    v.push_back({"schedule_first", FACE_SCHEDULE, "2026-09-15 08:59:30", 60, 250, nullptr, nullptr});
    // Period transition.
    v.push_back({"schedule_transition", FACE_SCHEDULE, "2026-09-15 09:59:30", 60, 250, nullptr, nullptr});

    // A break: English ends 10:45, Recess starts 11:00.
    auto gap = +[](int64_t) { S.periods[1].end = 645; };
    v.push_back({"schedule_break_start", FACE_SCHEDULE, "2026-09-15 10:44:30", 60, 250, gap, nullptr});
    v.push_back({"schedule_break", FACE_SCHEDULE, "2026-09-15 10:52:00", 3, 250, gap, nullptr});
    v.push_back({"schedule_break_end", FACE_SCHEDULE, "2026-09-15 10:59:30", 60, 250, gap, nullptr});

    // Last period -> home time.
    v.push_back({"schedule_home", FACE_SCHEDULE, "2026-09-15 15:29:30", 60, 250, nullptr, nullptr});

    // The whole day at 10 s steps (every transition, the marker all the way round).
    v.push_back({"schedule_day", FACE_SCHEDULE, "2026-09-15 08:55:00", 24300, 10000, nullptr, nullptr});

    // Timetable edited mid-run, on check frames (f % 7 == 3).
    v.push_back({"schedule_edit", FACE_SCHEDULE, "2026-09-15 10:20:00", 45, 250, nullptr,
                 +[](int64_t now, int64_t start) {
                     int64_t t = now - start;
                     if (t == 6000) { strlcpy(S.periods[1].name, "Drama", sizeof(S.periods[1].name)); settingsChanged(); }
                     else if (t == 13000) { S.periods[1].end = 650; settingsChanged(); }    // a gap appears
                     else if (t == 20000) { S.periods[6].end = 1000; settingsChanged(); }   // longer day: every arc moves
                     else if (t == 27000) { S.numPeriods = 3; settingsChanged(); }          // fewer periods
                     else if (t == 34000) { S.numPeriods = 0; settingsChanged(); }          // empty
                     else if (t == 41000) { defaultSettings(); S.face = FACE_SCHEDULE; settingsChanged(); }
                 }});

    // Empty timetable.
    v.push_back({"schedule_empty", FACE_SCHEDULE, "2026-09-15 10:00:00", 5, 250, +[](int64_t) { S.numPeriods = 0; }, nullptr});

    // A single period: nearly a full ring.
    auto single = +[](int64_t) {
        S.numPeriods = 1;
        schSetPeriod(0, "Maths", 540, 600);
    };
    v.push_back({"schedule_single", FACE_SCHEDULE, "2026-09-15 09:41:00", 20, 250, single, nullptr});
    v.push_back({"schedule_single_day", FACE_SCHEDULE, "2026-09-15 08:59:00", 3720, 5000, single, nullptr});

    // 12 periods (MAX_PERIODS) with short 5-minute ones and a gap.
    auto twelve = +[](int64_t) {
        S.numPeriods = MAX_PERIODS;
        schSetPeriod(0, "Roll call", 520, 525);
        schSetPeriod(1, "Maths", 525, 575);
        schSetPeriod(2, "English", 575, 625);
        schSetPeriod(3, "Move", 625, 630);
        schSetPeriod(4, "Science", 630, 680);
        schSetPeriod(5, "Recess", 680, 700);
        schSetPeriod(6, "History", 700, 750);
        schSetPeriod(7, "Lunch", 750, 800);
        schSetPeriod(8, "Art", 810, 860);
        schSetPeriod(9, "Music", 860, 905);
        schSetPeriod(10, "Sport", 905, 950);
        schSetPeriod(11, "Pack up", 950, 955);
    };
    v.push_back({"schedule_twelve", FACE_SCHEDULE, "2026-09-15 10:26:00", 5, 250, twelve, nullptr});
    v.push_back({"schedule_twelve_day", FACE_SCHEDULE, "2026-09-15 08:35:00", 24300, 15000, twelve, nullptr});

    // Unsorted input: must sort by start (colours follow the sorted order).
    auto unsorted = +[](int64_t) {
        S.numPeriods = 7;
        schSetPeriod(0, "Sport", 855, 930);
        schSetPeriod(1, "Recess", 660, 690);
        schSetPeriod(2, "Maths", 540, 600);
        schSetPeriod(3, "Art", 795, 855);
        schSetPeriod(4, "English", 600, 660);
        schSetPeriod(5, "Lunch", 750, 795);
        schSetPeriod(6, "Science", 690, 750);
    };
    v.push_back({"schedule_unsorted", FACE_SCHEDULE, "2026-09-15 10:59:30", 60, 250, unsorted, nullptr});

    // Overlapping periods (the dashboard allows them): Assembly inside Maths.
    // The latest-started period is "now"; runs Maths -> Assembly -> Maths.
    auto overlap = +[](int64_t) {
        S.numPeriods = 3;
        schSetPeriod(0, "Maths", 540, 600);
        schSetPeriod(1, "English", 600, 660);
        schSetPeriod(2, "Assembly", 550, 570);
    };
    v.push_back({"schedule_overlap", FACE_SCHEDULE, "2026-09-15 09:09:00", 1320, 1000, overlap, nullptr});

    // Very long names, current and next (23 bytes, the most a Period holds).
    auto longNames = +[](int64_t) {
        schSetPeriod(0, "Physical Education Prac", 540, 600);
        schSetPeriod(1, "Literature and Language", 600, 660);
    };
    v.push_back({"schedule_longname", FACE_SCHEDULE, "2026-09-15 09:59:30", 20, 250, longNames, nullptr});
    auto wideNames = +[](int64_t) {
        schSetPeriod(0, "WWWWWWWWWWWWWWWWWWWWWWW", 540, 600);
        schSetPeriod(1, "Maths \xC2\xB7 Extension MMMM", 600, 660);
    };
    v.push_back({"schedule_widename", FACE_SCHEDULE, "2026-09-15 09:30:00", 5, 250, wideNames, nullptr});
    auto medium = +[](int64_t) { schSetPeriod(0, "Design Technology", 540, 600); };
    v.push_back({"schedule_mediumname", FACE_SCHEDULE, "2026-09-15 09:30:00", 5, 250, medium, nullptr});

    // A period ending at midnight, crossing into the next day (before school again).
    auto night = +[](int64_t) {
        S.numPeriods = 2;
        schSetPeriod(0, "Prep", 1320, 1380);
        schSetPeriod(1, "Lights out", 1380, 1440);
    };
    v.push_back({"schedule_midnight", FACE_SCHEDULE, "2026-09-15 23:59:30", 60, 250, night, nullptr});
}
