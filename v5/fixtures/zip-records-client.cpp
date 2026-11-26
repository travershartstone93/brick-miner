#include "miner_zip_file_records.hpp"
#include <archive.h>
#include <iostream>
#include <json/json.h>
#include <type_traits>

int main(int argc, char **argv) {
  using Reader = brick_data::Rows (*)(const char *, const std::string &);
  static_assert(
      std::is_same_v<decltype(&brick_miner::shared::zip_file_records), Reader>);
  if (argc != 3)
    return 2;
  Json::Value result(Json::objectValue), rows(Json::arrayValue);
  result["libarchive"] = archive_version_number();
  std::string phase = "select";
  try {
    auto visit_rows = brick_miner::shared::zip_file_records(argv[1], argv[2]);
    phase = "visit";
    visit_rows([&](int64_t pos, const brick_data::Fields &fields) {
      Json::Value row(Json::objectValue);
      row["position"] = Json::Int64(pos);
      row["fields"] = Json::arrayValue;
      for (const auto &field : fields)
        row["fields"].append(field);
      rows.append(row);
    });
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
