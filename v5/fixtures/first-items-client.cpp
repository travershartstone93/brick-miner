#include "text_ops.hpp"
#include <algorithm>
#include <iostream>
#include <json/json.h>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace {
using J = Json::Value;

void require(bool condition, const std::string &message) {
  if (!condition)
    throw std::runtime_error(message);
}

bricks::Strings records(const J &values) {
  bricks::Strings result;
  for (const auto &value : values)
    result.push_back(value.asString());
  return result;
}

J encode(const bricks::Strings &values) {
  J result(Json::arrayValue);
  for (const auto &value : values)
    result.append(value);
  return result;
}

std::size_t count(const J &test) {
  const auto &value = test["count"];
  require(value.isUInt64() &&
              value.asUInt64() <= std::numeric_limits<std::size_t>::max(),
          "fixture count is outside the native argument range");
  return static_cast<std::size_t>(value.asUInt64());
}
} // namespace

int main() try {
  using Prefix = bricks::Strings (*)(bricks::Strings, std::size_t);
  static_assert(std::is_same_v<decltype(&bricks::take_first), Prefix>);

  J fixture, report;
  std::cin >> fixture;
  report["size_t_bits"] = std::numeric_limits<std::size_t>::digits;
  for (const auto &test : fixture["cases"]) {
    const auto name = test["name"].asString();
    const auto input = records(test["records"]);
    const auto n = count(test);
    auto result = bricks::take_first(input, n);
    const auto observed = encode(result);
    require(observed == test["expected"], "unexpected prefix for " + name);
    if (!result.empty())
      result.front().assign("changed returned value");
    require(encode(input) == test["records"] &&
                encode(bricks::take_first(input, n)) == observed,
            "prefix did not repeat with independent values for " + name);
    report["cases"][name] = observed;
  }
  const auto &tests = fixture["cases"];
  for (const auto &control : fixture["controls"]) {
    const auto witness = std::find_if(tests.begin(), tests.end(), [&](const J &test) {
      return test["name"] == control["witness"];
    });
    require(witness != tests.end(), "missing prefix control witness");
    const auto input = records((*witness)["records"]);
    const auto n = count(*witness);
    const auto name = control["name"].asString();
    bricks::Strings alternative;
    if (name == "last-instead-of-first")
      alternative = bricks::take_last(input, n);
    else if (name == "sort-before-selection")
      alternative = bricks::take_first(bricks::sorted(input), n);
    else if (name == "drop-empty-before-selection")
      alternative = bricks::take_first(bricks::nonempty(input), n);
    else
      throw std::runtime_error("unknown prefix control: " + name);
    require(encode(alternative) != (*witness)["expected"],
            "indistinguishable prefix control: " + name);
    report["controls"][name] = encode(alternative);
  }
  std::cout << report << '\n';
  return 0;
} catch (const std::exception &error) {
  std::cerr << error.what() << '\n';
  return 1;
}
