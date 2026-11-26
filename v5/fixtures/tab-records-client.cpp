#include "tab_provider.hpp"
#include <algorithm>
#include <iostream>
#include <json/json.h>
#include <stdexcept>
#include <type_traits>

namespace {
using J = Json::Value;
using brick_data::Fields;
using brick_data::Rows;
static_assert(std::is_same_v<decltype(&brick_data::tab_records), Rows (*)(const char*)>);

void require(bool result, const std::string &detail) {
  if (!result)
    throw std::runtime_error(detail);
}

std::string hex(const std::string &bytes) {
  constexpr char digits[] = "0123456789abcdef";
  std::string result;
  for (const unsigned char c : bytes) {
    result.push_back(digits[c >> 4]);
    result.push_back(digits[c & 15]);
  }
  return result;
}

Rows producer(const J &fixture) {
  auto name = fixture["name"].asString();
  auto rows = brick_data::tab_records(name.c_str());
  name.assign(name.size(), '!');
  return rows;
}

J row(std::int64_t position, const Fields &fields) {
  J result(Json::arrayValue), values(Json::arrayValue);
  result.append(Json::Int64(position));
  for (const auto &field : fields)
    values.append(hex(field));
  result.append(values);
  return result;
}

J expected(const J &test) {
  if (test.isMember("rows_hex")) {
    auto result = test["rows_hex"];
    for (auto &entry : result)
      entry[0] = Json::Int64(entry[0].asInt64());
    return result;
  }
  J result(Json::arrayValue);
  for (const auto &entry : test["rows"]) {
    Fields values;
    for (const auto &field : entry[1])
      values.push_back(field.asString());
    result.append(row(entry[0].asInt64(), values));
  }
  return result;
}

J observe(const Rows &rows) {
  J result(Json::arrayValue);
  rows([&](std::int64_t pos, const Fields &fields) { result.append(row(pos, fields)); });
  return result;
}

std::string visit_error(const Rows &rows, const brick_data::Visitor &visit) {
  try {
    rows(visit);
  } catch (const std::runtime_error &e) {
    return e.what();
  }
  return "";
}
} // namespace

int main() try {
  Json::CharReaderBuilder parser;
  J spec, report;
  std::string error;
  require(Json::parseFromStream(parser, std::cin, &spec, &error), error);
  for (const auto &test : spec["direct"]) {
    const auto rows = producer(test);
    const auto actual = observe(rows);
    const auto name = test["name"].asString();
    require(actual == expected(test) && observe(rows) == actual,
            "wrong TSV observations or restart for " + name);
    report["direct"][name] = actual;
  }
  for (const auto &test : spec["errors"]) {
    const auto rows = producer(test);
    unsigned visits = 0;
    const auto visitor = [&](std::int64_t, const Fields &) { ++visits; };
    const auto actual = visit_error(rows, visitor);
    const auto name = test["name"].asString();
    require(actual == test["exception"].asString() &&
                visit_error(rows, visitor) == actual && visits == 0,
            "wrong lazy TSV failure for " + name);
    report["errors"][name] = actual;
  }
  const auto &control = spec["visitor_failure"];
  const auto &direct = spec["direct"];
  const auto target = std::find_if(direct.begin(), direct.end(), [&](const J &test) {
    return test["name"] == control["case"];
  });
  require(target != direct.end(), "missing visitor witness");
  const auto rows = producer(*target);
  J stopped;
  const auto exception = visit_error(rows, [&](std::int64_t pos, const Fields &fields) {
    stopped = row(pos, fields);
    throw std::runtime_error(control["exception"].asString());
  });
  const auto all = expected(*target);
  require(exception == control["exception"].asString() && stopped == all[0] &&
              observe(rows) == all,
          "visitor exception was swallowed or reader did not restart");
  report["visitor_failure"]["last_row"] = stopped;
  report["visitor_failure"]["exception"] = exception;
  report["visitor_failure"]["restart"] = all;
  Json::StreamWriterBuilder writer;
  std::cout << Json::writeString(writer, report) << '\n';
  return 0;
} catch (const std::exception &e) {
  std::cerr << e.what() << '\n';
  return 1;
}
