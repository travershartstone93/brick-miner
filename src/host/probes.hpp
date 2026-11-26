#pragma once
#include "common.hpp"

namespace miner {
bool is_text(const std::string &type);
std::string source_id(const J &record);
std::string verified_header(const Path &base, const J &record,
                            const J &provenance);
std::string row_header(const J &record, const std::string &id,
                       const std::string &code);
void compare_lzip(const Path &package, const Path &dest);
J exercise(const J &record, const Path &base, const J &units, const Path &work,
           const J &contracts);
} // namespace miner
