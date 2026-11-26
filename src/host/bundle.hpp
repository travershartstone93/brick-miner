#pragma once
#include "common.hpp"

namespace miner {
using Files = std::map<std::string, std::string>;
Files read_tree(const Path &path);
void write_tree(const Path &path, const Files &files);
std::string manifest(const Files &files);
std::string seal(Files &bundle);
Files register_tasks(Files base, const J &tasks, const Files &headers,
                     const J &meta, const Path &work, const J &types = object(),
                     const J &conversions = array());
Files ablate(Files bundle, const std::string &key);
Files remove_binding(Files bundle, const J &binding);
J plan(const Path &bundle, const J &spec, const Path &dest);
J spec_for(const std::string &input, const std::string &output,
           const J &requirement);
Strings flags(const Strings &packages);
void build_emission(const J &provider, const Path &dest, const Path &exe,
                    const std::string &compiler = "c++");
J evaluate(const Path &exe, const J &cases, const Path &dest);
std::pair<J, J> publish_project(const Path &base, const Path &work,
                                const J &provenance, const J &records,
                                const J &checks, const J &contracts,
                                const J &units, const Path &generation);
} // namespace miner
