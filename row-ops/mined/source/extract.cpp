#include "duckdb.hpp"
#include <iostream>

int main(int argc, char **argv) try {
  using namespace miner;
  check(argc == 5, "extract --cache SIGNED_INPUTS --output NEW_DIRECTORY");
  Path cache, output;
  for (int i = 1; i < argc; i += 2) {
    std::string key = argv[i];
    if (key == "--cache")
      cache = fs::canonical(argv[i + 1]);
    else if (key == "--output")
      output = fs::absolute(argv[i + 1]);
    else
      throw std::runtime_error("unknown argument " + key);
  }
  check(!cache.empty() && !output.empty(),
        "expected cache and output arguments");
  check_corpus(output);
  duckdb_miner::build_kernels(output, cache);
  return 0;
} catch (const std::exception &e) {
  std::cerr << e.what() << '\n';
  return 1;
}
