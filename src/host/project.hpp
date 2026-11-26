#pragma once
#include "common.hpp"

namespace miner {
J compilation_units(const Path &base, const Path &db, Strings &skipped);
J source_pins(const Path &base, const Path &file);
struct MiningResult {
  J inventory, checks;
  double scan_seconds, client_seconds;
};
MiningResult mine_native(const Path &base, const Path &db, const Path &dest,
                         const J &units, const J &provenance,
                         const J &contracts);
} // namespace miner
