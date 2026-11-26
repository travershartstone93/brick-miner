#pragma once
#include "common.hpp"

namespace v5 {
using namespace miner;
inline Path v5_root() { return root / "v5"; }
inline Path sample_application() {
  return runs / "project-mining-780_n3ro/run-1/application";
}
const inline std::string api = "reverse_record_bytes",
                         direct = "number_option_prefix";
const inline std::string base_hash =
    "e4564e3079f0b32b3379a411b170ce9af61ec80ab7938b6a974e724abd538328";
std::string digest(const Path &file);
J tree_hashes(const Path &base);
Path verified_base();
J sources();
Path tantivy_reference();
J select_binding(const J &rows, const std::string &capability,
                 const std::string &brick, const std::string &input,
                 const std::string &output);
void inventory(const Path &dest);
void extract(const Path &dest, const std::string &route);
void extract_latch(const Path &dest);
J retained_source(const Path &dest, const std::string &id,
                  const std::string &notice);
void extract_fifo(const Path &dest);
void qualify_fifo(const Path &extracted, const Path &dest);
void emit_fifo(const Path &base, const Path &bundle, const Path &dest);
J class_contract(const Path &package);
J class_schema(J common, const J &interface);
void qualify_latch(const Path &extracted, const Path &dest);
void standalone(const Path &extract, const Path &dest,
                const std::string &route);
void integrate(const Path &extract, const Path &dest);
void assemble(const Path &work, const Path &dest);
void validate(const J &c, const J &schema);
J verify(const Path &package, const std::string &hash);
} // namespace v5
