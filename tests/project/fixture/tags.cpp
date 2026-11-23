#include "cycle.hpp"
#include <cstdio>

static int bucket(int sample) {
    return sample % 5;
}

static int scale(int value) {
    return value * 3;
}

static int scale(bool flag) {
    return flag ? 1 : 0;
}

int tag_level(int sample) {
    return scale(bucket(sample)) + scale(sample == 0);
}

int external_read(int sample) {
    return sample + std::getchar();
}

int previous_read(int sample) {
    static int previous = 0;
    const int result = previous;
    previous = sample;
    return result;
}

int through_class(int sample) {
    return CycleView::joined(sample) + bucket(sample);
}

void add_tick(int& value) {
    ++value;
}
