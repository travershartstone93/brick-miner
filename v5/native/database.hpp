#pragma once
#include "common.hpp"
#include <memory>
#include <sqlite3.h>

namespace v5 {
using namespace miner;
class Database {
  sqlite3 *db = nullptr;
  using Statement = std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>;
  Statement prepare(const std::string &sql, const Strings &bindings);

public:
  explicit Database(const Path &file, bool readonly = false);
  ~Database();
  Database(const Database &) = delete;
  Database &operator=(const Database &) = delete;
  void script(const std::string &sql);
  void insert(const std::string &sql, const Strings &params);
  std::vector<Strings> query(const std::string &sql,
                             const Strings &params = {});
};
} // namespace v5
