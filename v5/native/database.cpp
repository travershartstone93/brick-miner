#include "database.hpp"

namespace v5 {
Database::Database(const Path &file, bool readonly) {
  auto code =
      sqlite3_open_v2(file.c_str(), &db,
                      readonly ? SQLITE_OPEN_READONLY
                               : SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                      nullptr);
  if (code != SQLITE_OK) {
    auto error = std::string(sqlite3_errmsg(db));
    sqlite3_close(db);
    throw std::runtime_error(error);
  }
}
Database::~Database() { sqlite3_close(db); }
void Database::script(const std::string &sql) {
  char *reason = nullptr;
  int code = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &reason);
  if (code != SQLITE_OK) {
    std::string msg = reason ? reason : sqlite3_errmsg(db);
    sqlite3_free(reason);
    throw std::runtime_error(msg);
  }
}
Database::Statement Database::prepare(const std::string &sql,
                                      const Strings &bindings) {
  sqlite3_stmt *raw = nullptr;
  check(sqlite3_prepare_v2(db, sql.c_str(), -1, &raw, nullptr) == SQLITE_OK,
        sqlite3_errmsg(db));
  Statement s(raw, sqlite3_finalize);
  check(size_t(sqlite3_bind_parameter_count(raw)) == bindings.size(),
        "incorrect SQL binding count");
  for (size_t i = 0; i < bindings.size(); ++i)
    check(sqlite3_bind_text(raw, i + 1, bindings[i].data(), bindings[i].size(),
                            SQLITE_TRANSIENT) == SQLITE_OK,
          "SQLite binding failed");
  return s;
}
void Database::insert(const std::string &sql, const Strings &params) {
  auto s = prepare(sql, params);
  int rc = sqlite3_step(s.get());
  check(rc == SQLITE_DONE, sqlite3_errmsg(db));
}
std::vector<Strings> Database::query(const std::string &sql,
                                     const Strings &params) {
  auto s = prepare(sql, params);
  std::vector<Strings> result;
  int rc;
  while ((rc = sqlite3_step(s.get())) == SQLITE_ROW) {
    Strings row;
    for (int i = 0; i < sqlite3_column_count(s.get()); ++i) {
      auto *p = sqlite3_column_text(s.get(), i);
      row.emplace_back(p ? reinterpret_cast<const char *>(p) : "",
                       sqlite3_column_bytes(s.get(), i));
    }
    result.push_back(std::move(row));
  }
  check(rc == SQLITE_DONE, sqlite3_errmsg(db));
  return result;
}
} // namespace v5
