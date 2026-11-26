#include "provider.hpp"
#include <iostream>
#include <stdexcept>

int main() {
    using Strings = std::vector<std::string>;
    const Strings input{"drawer", "", "ab", "ab", std::string("\xff\0A", 3)};
    const Strings expected{"reward", "", "ba", "ba", std::string("A\0\xff", 3)};
    auto actual = brick_miner::reverse_record_bytes(input);
    if (actual != expected || input[0] != "drawer" ||
        brick_miner::reverse_record_bytes(actual) != input ||
        !brick_miner::reverse_record_bytes({}).empty()) return 1;
    std::cout << "mixed-bytes retained-order input-unchanged sampled-involution empty\n";
}
