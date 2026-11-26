#pragma once
#include "common.hpp"

namespace miner {
J link_records(const J &records);
J scan(const Path &source, const Path &database, const Strings &units);
} // namespace miner
