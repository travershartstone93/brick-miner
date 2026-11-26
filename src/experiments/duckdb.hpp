#pragma once
#include "common.hpp"

namespace duckdb_miner {
using namespace miner;
void acquire(const Path &directory, const Path &cache);
void build_kernels(const Path &directory, const Path &cache);
Path fetch(const Path &directory);
std::string hash_file(const Path &path);
J ast_nodes(const Strings &command);
Strings dependencies(const Path &file);
void copy(const Path &from, const Path &to);
} // namespace duckdb_miner
