#pragma once
#include "common.hpp"

namespace pilots {
using namespace miner;
inline J discover_xxh32(const Path &work) {
  auto probe = work / "public-header.cpp", db = work / "library-cdb";
  save(probe, "#include <xxhash.h>\n");
  write(db / "compile_commands.json",
        array({object({{"directory", work.string()},
                       {"file", probe.string()},
                       {"arguments",
                        array({"clang++", "-std=c++17", "-fparse-all-comments",
                               "-fretain-comments-from-system-headers", "-c",
                               probe.string()})}})}));
  return parse(run({root / ".build/brick-mine", "library", db, probe}));
}

inline std::string render_original_adapter(const std::string &k, const J &info,
                                           const std::string &raw) {
  bool api = k == "line_xxh32";
  auto n = raw.find("*/");
  check(n != raw.npos, "missing source notice");
  std::string value = api ? "std::int64_t" : "std::string";
  std::string change =
      api ? ""
          : "// Adapted from GNU GDB on 2026-11-23: reachable definitions "
            "are inline and namespaced.\n";
  return raw.substr(0, n + 2) + "\n" + change +
         "#pragma once\n#include <cstdint>\n#include <string>\n#include "
         "<vector>\n" +
         (api ? "#include <xxhash.h>"
              : "#include <climits>\n#include <stdexcept>") +
         "\n\nnamespace brick_miner {\n" +
         info.get("definitions", "").asString() + "inline std::vector<" +
         value + "> " + k +
         "(const std::vector<std::string>& input) {\n    std::vector<" + value +
         "> out;\n    out.reserve(input.size());\n    for (const auto& s : "
         "input) {\n        " +
         info["body"].asString() +
         "\n    }\n    return out;\n}\n} // namespace brick_miner\n";
}
} // namespace pilots
