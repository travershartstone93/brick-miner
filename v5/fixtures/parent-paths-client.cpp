#include "path_ops.hpp"
#include <iostream>
#include <json/json.h>
#include <type_traits>

using Paths = std::vector<std::string>;
static_assert(
    std::is_same_v<decltype(&bricks::parent), Paths (*)(const Paths &)>);

Json::Value values(const Paths &paths) {
  Json::Value out(Json::arrayValue);
  for (const auto &p : paths)
    out.append(p);
  return out;
}

int main() {
  Json::Value spec, report(Json::objectValue);
  if (!(std::cin >> spec) || !spec["cases"].isArray())
    return 2;
  for (const auto &c : spec["cases"]) {
    Paths paths;
    for (const auto &p : c["records"])
      paths.push_back(p.asString());
    auto parents = values(bricks::parent(paths));
    if (parents != c["expected"] || values(paths) != c["records"])
      return 1;
    auto &row = report["fixed"][c["name"].asString()];
    row["parents"] = parents;
    row["basenames"] = values(bricks::basename(paths));
  }
  // Construct paths from known parents and leaves. No filesystem routines
  // participate in the oracle.
  unsigned trials = 0;
  for (std::string dir : {"", ".", "..", "x", "x/..", "/x", "/"}) {
    for (std::string name : {"leaf", ".hidden", "..", "a b", "ü"}) {
      auto path = dir + (dir.empty() || dir == "/" ? "" : "/") + name;
      Paths input{path, path + "/", path};
      if (bricks::parent(input) != Paths{dir, path, dir})
        return 1;
      ++trials;
    }
  }
  report["constructed_triples"] = trials;
  std::cout << report;
}
