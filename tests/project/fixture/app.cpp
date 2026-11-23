#include "cycle.hpp"
#include <iostream>

int main() {
    int sample;
    while (std::cin >> sample) {
        std::cout << phase_offset(sample) << ' ' << inside_window(sample)
                  << ' ' << tag_level(sample) << ' ' << through_class(sample)
                  << ' ' << CycleView::joined(sample) << ' ' << CycleView{}.sign(sample) << ' ' << Calibration{4}.adjusted(sample) << '\n';
    }
    return 0;
}
