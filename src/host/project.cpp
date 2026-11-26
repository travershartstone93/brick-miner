#include "project.hpp"
#include "link.hpp"
#include "probes.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <regex>

namespace miner {
J compilation_units(const Path &base, const Path &db, Strings &skipped) {
  const std::set<std::string> suffixes = {".C",   ".cc",  ".cpp",
                                          ".cxx", ".c++", ".CPP"};
  J units = object();
  for (const auto &cmd : load(db / "compile_commands.json")) {
    auto p = fs::canonical(Path(cmd["directory"].asString()) /
                           cmd["file"].asString());
    if (!suffixes.contains(p.extension().string())) {
      skipped.push_back(p.filename());
      continue;
    }
    auto relative = p.lexically_relative(base);
    check(project_file(base, relative) == p &&
              !units.isMember(relative.string()),
          "ambiguous compilation unit: " + relative.string());
    units[relative.string()] = cmd;
  }
  std::sort(skipped.begin(), skipped.end());
  check(!units.empty(), "no project C++ compilation units");
  return units;
}
J source_pins(const Path &base, const Path &file) {
  if (file.empty())
    return {};
  auto p = load(file);
  check(!p["package"].empty() && !p["archive_sha256"].asString().empty() &&
            !p["license_files"].empty(),
        "missing package provenance");
  for (const auto &name : p["file_evidence"].getMemberNames())
    check(sha(bytes(project_file(base, name))) ==
              p["file_evidence"][name]["sha256"].asString(),
          "changed source: " + name);
  for (const auto &f : p["license_files"]) {
    auto name = f.asString();
    check(sha(bytes(project_file(base, name))) ==
              p["license_sha256"][name].asString(),
          "changed license: " + name);
  }
  return p;
}
MiningResult mine_native(const Path &base, const Path &db, const Path &dest,
                         const J &units, const J &provenance,
                         const J &contracts) {
  auto records = scan(base, db, units.getMemberNames());
  J checks = object();
  for (auto &r : records) {
    if (r["status"] == "rejected")
      continue;
    const auto id = source_id(r);
    r["id"] = id;
    auto work = dest / "candidates" / id;
    save(work / "candidate.hpp", verified_header(base, r, provenance));
    for (const auto &f : r["files"])
      save(work / "original" / f.asString(),
           bytes(project_file(base, f.asString())));
    checks[id] = exercise(r, base, units, work, contracts);
  }
  write(dest / "inventory.json", records);
  return {std::move(records), std::move(checks)};
}
} // namespace miner
