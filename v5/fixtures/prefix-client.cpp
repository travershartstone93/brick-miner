#include "text_ops.hpp"
#include <iostream>
#include <json/json.h>
#include <type_traits>

using Records = std::vector<std::string>;
static_assert(
    std::is_same_v<decltype(&bricks::prefix),
                   Records (*)(const Records &, const std::string &)>);

Json::Value json(const Records &records) {
  Json::Value value(Json::arrayValue);
  for (const auto &record : records)
    value.append(record);
  return value;
}

void check_sequences(Records &inputs, unsigned &trials,
                     unsigned &substring_differences) {
  for (const std::string prefix : {"", "a", "aa", "b", "A", "x"}) {
    Records expected;
    for (const auto &s : inputs)
      if (s.size() >= prefix.size() &&
          std::equal(prefix.begin(), prefix.end(), s.begin()))
        expected.push_back(s);
    auto result = bricks::prefix(inputs, prefix);
    if (result != expected || bricks::prefix(result, prefix) != result)
      throw std::runtime_error("bounded prefix oracle or idempotence failed");
    substring_differences += bricks::contains_text(inputs, prefix) != expected;
    ++trials;
  }
  if (inputs.size() == 3)
    return;
  for (auto value : {"", "a", "aa", "ba"}) {
    inputs.emplace_back(value);
    check_sequences(inputs, trials, substring_differences);
    inputs.pop_back();
  }
}

int main() try {
  Json::Value fixtures, report(Json::objectValue);
  if (!(std::cin >> fixtures) || !fixtures.isArray())
    return 2;
  for (const auto &c : fixtures) {
    Records records;
    for (const auto &s : c["records"])
      records.push_back(s.asString());
    const auto prefix = c["prefix"].asString();
    auto result = bricks::prefix(records, prefix);
    if (json(result) != c["expected"] || json(records) != c["records"])
      return 1;
    auto &fixed = report["fixed"][c["name"].asString()];
    fixed["prefix"] = prefix;
    fixed["result"] = json(result);
    fixed["substring"] = json(bricks::contains_text(records, prefix));
  }
  Records records;
  unsigned trials = 0, differences = 0;
  check_sequences(records, trials, differences);
  report["bounded_trials"] = trials;
  report["substring_differences"] = differences;
  std::cout << report;
} catch (const std::exception &e) {
  std::cerr << e.what();
  return 1;
}
