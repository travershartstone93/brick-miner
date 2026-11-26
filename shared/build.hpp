#pragma once
#include "qualification.hpp"
namespace shared {
struct Build {
  std::string name;
  std::map<std::string, Path> symbols, originals;
  J metadata = object();
};
Build build_duckdb(const std::string &batch, const J &contracts);
Build build_inputs(const J &contracts);
J definition(const std::string &symbol, const Path &source,
             const Strings &flags);
} // namespace shared
