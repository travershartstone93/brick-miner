#pragma once
#include "bundle.hpp"
namespace shared {
using namespace miner;
const inline Strings batches = {"readers",   "relational", "history",
                                "relations", "inputs",     "analytics",
                                "sequences", "decisions"};
Path here();
Path first_batch();
Path stage_owner(Path pair, const std::string &batch);
J operations(const J &steps);
J suite();
J assess(const std::string &id, const Path &exe, const Path &dest);
J run_requests(const Path &bundle, const Path &work, bool baseline,
               const std::string &only = "");
void compare(const Path &pair, const Path &bundle, const Path &work);
void materialize(const Path &file, const J &data);
void consumer(const J &c, const J &contract, const Path &dest);
std::string string_literal(const std::string &s);
} // namespace shared
