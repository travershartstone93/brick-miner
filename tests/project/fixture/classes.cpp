#include "cycle.hpp"

int CycleView::joined(int sample) {
    return combined(sample) * 2;
}

int CycleView::sign(int sample) const {
    return phase_plus_tag(sample) > 0;
}

int Tally::add(int sample) const {
    return sample + value;
}

int cross_bad(int sample) {
    return next_count(sample);
}

int Calibration::adjusted(int sample) const {
    return offset + tag_level(sample);
}
