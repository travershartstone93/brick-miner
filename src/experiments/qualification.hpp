#pragma once
#include "bundle.hpp"

namespace qualification {
using namespace miner;
double seconds();
J argument_order(const Path &contracts, const std::string &name);
J parameters(const J &keys, const J &values);
void plan_inputs(const Path &dir, const Files &files);
void reject(const J &emission);
void publish(Files &stage, const Path &directory);
} // namespace qualification
