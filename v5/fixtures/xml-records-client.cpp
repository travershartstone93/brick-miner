#include "miner_xml_records.hpp"
#include <pugixml.hpp>
#include <iostream>
#include <json/json.h>
#include <sstream>
#include <stdexcept>
#include <type_traits>

namespace {
Json::Value observe(const char *path, const char *encoded) {
  std::istringstream input(encoded);
  Json::Value parameters, result(Json::objectValue), rows(Json::arrayValue);
  input >> parameters;
  std::string phase = "select";
  try {
    auto produce = brick_miner::shared::xml_records(
        path, parameters["records"].asString(), parameters["fields"].asString());
    auto visit = [&](Json::Value &observed) {
      produce([&](std::int64_t position, const brick_data::Fields &fields) {
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
  } catch (const pugi::xpath_exception &) {
    result["exception"] = "xpath_exception";
  } catch (const std::invalid_argument &) {
    result["exception"] = "invalid_argument";
  } catch (const std::runtime_error &) {
    result["exception"] = "runtime_error";
  }
  result["phase"] = phase;
  result["rows"] = rows;
  return result;
}
} // namespace

int main(int argc, char **argv) try {
  using Reader = brick_data::Rows (*)(const char *, const std::string &,
                                     const std::string &);
  static_assert(std::is_same_v<decltype(&brick_miner::shared::xml_records),
                               Reader>);
  if (argc != 3)
    return 2;
  std::cout << observe(argv[1], argv[2]);
  return 0;
} catch (const std::exception &error) {
  std::cerr << error.what() << '\n';
  return 1;
}
