#pragma once
#include "bundle.hpp"

namespace row_replay {
std::string hash(const miner::Path &file);
miner::J tools();
miner::J input_hashes(const miner::Path &trial, const std::string &name);
miner::J scores(const miner::J &receipt);
miner::J operations(const miner::J &recipe);
miner::J plan_probe(const miner::Path &bundle, const miner::Path &dest,
                    const miner::J &spec);
miner::J trial(const miner::Path &work, const std::string &arm,
               const std::string &request, const miner::Path &bundle,
               const miner::Path &spec_file);
miner::J trial(const miner::Path &work, const std::string &arm,
               const std::string &request, const miner::Path &bundle,
               const miner::Path &spec_file, const std::string &input,
               const miner::J &expected_inputs);
miner::J ports(const miner::J &role);
bool selects(const miner::J &role, const std::string &id);
bool complete(const miner::J &role);
miner::J exact_row(const miner::Path &bundle, const std::string &id,
                   const std::string &from, const std::string &to);
miner::J extra_case(const miner::Path &dir, const miner::Path &exe,
                    const miner::Path &bundle, const std::string &input,
                    const miner::J &expected, int expected_exit = 0);
std::string path_output(const miner::Path &dest, const miner::Path &input,
                        const miner::Path &exe, const miner::Path &bundle);
void verify_bundle(const miner::Path &bundle, const std::string &expected);
miner::J verify_json_records_scope(const miner::Path &bundle,
                                   const miner::Path &stage,
                                   const std::string &expected_manifest,
                                   const std::string &selector);
} // namespace row_replay
