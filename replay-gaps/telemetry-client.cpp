#include "miner_read_json_records.hpp"
#include "structure.hpp"
#include <iostream>
#include <json/json.h>
#include <type_traits>

int main(int argc, char **argv) try {
  static_assert(
      std::is_same_v<decltype(&brick_miner::shared::read_json_records),
                     brick_data::Rows (*)(const char *, const std::string &)>);
  if (argc != 3)
    return 2;
  auto rows = brick_miner::shared::read_json_records(argv[1], argv[2]);
  Json::Value report(Json::objectValue), values(Json::arrayValue);
  rows([&](std::int64_t position, const brick_data::Fields &fields) {
    Json::Value row(Json::objectValue);
    row["position"] = Json::Int64(position);
    row["fields"] = Json::arrayValue;
    for (const auto &field : fields)
      row["fields"].append(field);
    values.append(row);
  });
  report["records"] = values;
  try {
    report["nested_list_count"] = Json::UInt64(bricks::groups(argv[1]).size());
  } catch (const std::invalid_argument &e) {
    report["nested_list_error"] = e.what();
  }
  std::cout << report;
  return 0;
} catch (const std::exception &e) {
  std::cerr << e.what() << '\n';
  return 1;
}
