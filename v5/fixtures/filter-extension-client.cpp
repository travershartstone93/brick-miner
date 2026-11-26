#include "path_ops.hpp"
#include <iostream>
#include <json/json.h>
#include <type_traits>

int main() {
  static_assert(std::is_same_v<decltype(&bricks::suffix),
                               bricks::Paths (*)(const bricks::Paths &,
                                                 const std::string &)>);
  Json::Value spec, report(Json::objectValue);
  std::cin >> spec;
  for (const auto &test : spec["cases"]) {
    bricks::Paths paths;
    for (const auto &path : test["records"])
      paths.push_back(path.asString());
    const auto before = paths;
    const auto extension = test["extension"].asString();
    const auto result = bricks::suffix(paths, extension);
    auto observed = Json::Value(Json::arrayValue);
    for (const auto &path : result)
      observed.append(path);
    if (observed != test["expected"] || paths != before) {
      std::cerr << "suffix expectation failed for " << test["name"].asString()
                << '\n';
      return 1;
    }
    report[test["name"].asString()] = observed;
  }
  std::cout << report;
}
