#include "miner_uri_records.hpp"
#include <iostream>
#include <json/json.h>
#include <type_traits>

int main(int argc, char **argv) {
  using Reader = brick_data::Rows (*)(const char *, const std::string &);
  static_assert(
      std::is_same_v<decltype(&brick_miner::shared::uri_records),
                     Reader>);
  if (argc != 3)
    return 2;
  Json::Value result(Json::objectValue), rows(Json::arrayValue);
  std::string phase = "select";
  try {
    auto produce = brick_miner::shared::uri_records(argv[1], argv[2]);
    auto visit = [&](Json::Value &observed) {
      produce([&](int64_t position, const brick_data::Fields &fields) {
        Json::Value row(Json::objectValue);
        row["position"] = Json::Int64(position);
        row["fields"] = Json::Value(Json::arrayValue);
        for (const auto &field : fields)
          row["fields"].append(field);
        observed.append(row);
      });
    };
    phase = "visit";
    visit(rows);
    Json::Value repeated(Json::arrayValue);
    visit(repeated);
    result["same_instance_repeat"] = rows == repeated;
    phase.clear();
  } catch (const std::invalid_argument &) {
    result["exception"] = "invalid_argument";
  } catch (const std::runtime_error &) {
    result["exception"] = "runtime_error";
  }
  result["phase"] = phase;
  result["rows"] = rows;
  std::cout << result;
}
