#include "miner_sqlite_records.hpp"
#include <sqlite3.h>
#include <iostream>
#include <json/json.h>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <type_traits>

namespace {
void make_database(const char *path, const char *script) {
  sqlite3 *raw = nullptr;
  const auto opened = sqlite3_open_v2(
      path, &raw, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
  std::unique_ptr<sqlite3, decltype(&sqlite3_close)> database(raw, sqlite3_close);
  if (opened != SQLITE_OK)
    throw std::runtime_error("cannot create SQLite fixture");
  if (sqlite3_exec(database.get(), script, nullptr, nullptr, nullptr) != SQLITE_OK)
    throw std::runtime_error(sqlite3_errmsg(database.get()));
}

Json::Value observe(const char *path, const char *encoded) {
  std::istringstream input(encoded);
  Json::Value parameters, result(Json::objectValue), rows(Json::arrayValue);
  input >> parameters;
  std::string phase = "select";
  try {
    auto produce = brick_miner::shared::sqlite_records(
        path, parameters["table"].asString(), parameters["fields"].asString(),
        parameters["order_by"].asString());
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
                                     const std::string &, const std::string &);
  static_assert(std::is_same_v<decltype(&brick_miner::shared::sqlite_records),
                               Reader>);
  if (argc == 4 && std::string(argv[1]) == "--make") {
    make_database(argv[2], argv[3]);
    return 0;
  }
  if (argc != 3)
    return 2;
  std::cout << observe(argv[1], argv[2]);
  return 0;
} catch (const std::exception &error) {
  std::cerr << error.what() << '\n';
  return 1;
}
