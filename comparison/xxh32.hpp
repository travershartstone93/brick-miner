// Independently written binding for the frozen seed-zero contract.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <xxhash.h>

inline std::vector<std::int64_t> manual_hash(const std::vector<std::string>& input) {
    std::vector<std::int64_t> output;
    for (const auto& line : input)
        output.push_back(XXH32(line.data(), line.length(), 0));
    return output;
}
