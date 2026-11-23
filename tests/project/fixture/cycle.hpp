#pragma once

int phase_offset(int sample);
bool inside_window(int sample);
int tag_level(int sample);
int next_count(int sample);
int shifted_count(int sample);
int external_read(int sample);

int phase_plus_tag(int sample);
int through_class(int sample);
int cross_bad(int sample);

struct CycleView {
    static int joined(int sample);
    int sign(int sample) const;
private:
    static int combined(int sample);
};

class Tally {
    int value;
public:
    int add(int sample) const;
};

struct Calibration {
    int offset;
    int adjusted(int sample) const;
};

void add_tick(int& value);
