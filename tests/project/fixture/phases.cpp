#include "cycle.hpp"

#ifndef CYCLE_REPORT_BUILD
#error "Use this project's compile command."
#endif

static int bucket(int sample) {
    int phase = sample % 12;
    return phase < 0 ? phase + 12 : phase;
}

int phase_offset(int sample) {
    return bucket(sample) * 2 - 7;
}

bool inside_window(int sample) {
    return bucket(sample) < 2;
}

static int meter = 0;

int next_count(int sample) {
    return sample + ++meter;
}

int shifted_count(int sample) {
    return next_count(sample) - 1;
}

int phase_plus_tag(int sample) {
    int value = bucket(sample) + tag_level(sample);
    add_tick(value);
    return value;
}

int CycleView::combined(int sample) {
    return phase_plus_tag(sample);
}
