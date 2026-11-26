#pragma once
#include "common.hpp"

namespace v5 {
miner::J replay_binding_usage(const miner::Path &snapshot,
                              const std::string &pin);
miner::Path binding_identity_fixture(const miner::Path &package,
                                     const std::string &pin);
} // namespace v5
