#include "text_ops.hpp"
#include <iostream>
#include <json/json.h>
#include <type_traits>

// Verify the actual compiled interface before comparing behavioral witnesses.
static_assert(
    std::is_same_v<decltype(&bricks::count), decltype(&bricks::lengths)>);
static_assert(
    std::is_same_v<decltype(&bricks::lengths),
                   std::vector<std::int64_t> (*)(const bricks::Strings &)>);
int main(int argc, char **argv) {
  if (argc != 2)
    return 2;
  auto binding = std::string(argv[1]);
  if (binding != "byte_lengths" && binding != "item_count")
    return 2;
  Json::Value inputs;
  if (!(std::cin >> inputs) || !inputs.isArray())
    return 2;
  Json::Value observations(Json::arrayValue);
  for (const auto &set : inputs) {
    if (!set.isArray())
      return 2;
    bricks::Strings records;
    for (const auto &v : set) {
      if (!v.isString())
        return 2;
      records.push_back(v.asString());
    }
    auto output = binding == "byte_lengths" ? bricks::lengths(records)
                                            : bricks::count(records);
    Json::Value numbers(Json::arrayValue);
    for (auto v : output)
      numbers.append(Json::Int64(v));
    observations.append(numbers);
  }
  std::cout << observations;
}
