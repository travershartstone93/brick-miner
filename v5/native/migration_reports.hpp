#pragma once
#include "common.hpp"

namespace v5 {
miner::J sequence_writer_identities(const miner::Path &package);
void project_binding_pair(const miner::Path &stage);
miner::Path qualify_writer(const miner::Path &package, const std::string &pin);
miner::J replay_binding_usage(const miner::Path &snapshot,
                              const std::string &pin);
miner::Path binding_identity_fixture(const miner::Path &package,
                                     const std::string &pin);
} // namespace v5
