#include "miner_filter_records.hpp"
#include <algorithm>
#include <charconv>
#include <fstream>
#include <iostream>
#include <json/json.h>
#include <stdexcept>
#include <string_view>

namespace {
using Rows = std::vector<std::pair<std::int64_t, brick_data::Fields>>;

void require(bool condition, const std::string &message) {
  if (!condition)
    throw std::runtime_error(message);
}

Rows rows(const Json::Value &list) {
  Rows result;
  for (const auto &row : list) {
    brick_data::Fields fields;
    for (const auto &field : row[1])
      fields.push_back(field.asString());
    result.emplace_back(row[0].asInt64(), std::move(fields));
  }
  return result;
}

std::int64_t number(std::string_view text) {
  if (!text.empty() && text.front() == '+')
    text.remove_prefix(1);
  std::int64_t value = 0;
  const auto end = text.data() + text.size();
  const auto parsed = std::from_chars(text.data(), end, value);
  require(parsed.ec == std::errc{} && parsed.ptr == end,
          "invalid oracle integer");
  return value;
}

bool accepts(int order, const std::string &comparison) {
  if (comparison == "eq")
    return order == 0;
  if (comparison == "ne")
    return order != 0;
  if (comparison == "lt")
    return order < 0;
  if (comparison == "le")
    return order <= 0;
  if (comparison == "gt")
    return order > 0;
  require(comparison == "ge", "undefined oracle comparison");
  return order >= 0;
}

Rows oracle(const Rows &input, const Json::Value &test,
            bool lexical_numbers = false, bool swapped = false) {
  const auto type = test["type"].asString();
  const bool numeric = type == "int" || type == "int_field";
  const bool field = type == "int_field" || type == "text_field";
  const auto value = test["value"].asString();
  Rows result;
  for (const auto &row : input) {
    const auto &lhs = row.second.at(test["column"].asUInt());
    const auto &rhs = field ? row.second.at(number(value)) : value;
    int order = lhs.compare(rhs);
    if (numeric && !lexical_numbers) {
      const auto a = number(lhs), b = number(rhs);
      order = (a > b) - (a < b);
    }
    if (accepts(swapped ? (order < 0) - (order > 0) : order,
                test["comparison"].asString()))
      result.push_back(row);
  }
  return result;
}

brick_data::Rows filter(const Rows &input, unsigned &reads,
                        const Json::Value &test) {
  brick_data::Rows producer = [&](const brick_data::Visitor &visit) {
    ++reads;
    for (const auto &[pos, fields] : input)
      visit(pos, fields);
  };
  return brick_miner::shared::filter_records(
      producer, test["column"].asUInt(), test["type"].asString(),
      test["comparison"].asString(), test["value"].asString());
}

Json::Value observation(const Json::Value &test, const Rows &result) {
  Json::Value item = test;
  item["result"] = Json::arrayValue;
  for (const auto &[pos, fields] : result) {
    Json::Value row(Json::arrayValue), values(Json::arrayValue);
    row.append(Json::Int64(pos));
    for (const auto &field : fields)
      values.append(field);
    row.append(values);
    item["result"].append(row);
  }
  return item;
}
} // namespace

int main(int argc, char **argv) try {
  require(argc == 2, "fixture file required");
  std::ifstream file(argv[1]);
  Json::Value suite;
  file >> suite;
  const auto input = rows(suite["rows"]);
  Json::Value report(Json::objectValue);
  report["comparisons"] = Json::arrayValue;
  unsigned lexical = 0, swapped = 0, permuted = 0;
  for (auto mode : suite["modes"]) {
    for (const auto &comparison : suite["comparisons"]) {
      mode["comparison"] = comparison;
      auto expected = oracle(input, mode);
      lexical += expected != oracle(input, mode, true);
      swapped += expected != oracle(input, mode, false, true);
      auto pos_sorted = expected;
      std::stable_sort(
          pos_sorted.begin(), pos_sorted.end(),
          [](const auto &a, const auto &b) { return a.first < b.first; });
      permuted += expected != pos_sorted;
      unsigned reads = 0;
      auto result = filter(input, reads, mode);
      require(reads == 0, "producer read during filter construction");
      for (unsigned visit = 0; visit < 2; ++visit) {
        Rows actual;
        result([&](auto pos, const auto &fields) {
          actual.emplace_back(pos, fields);
        });
        require(actual == expected && reads == visit + 1,
                "typed comparison, row identity or encounter order differs");
      }
      report["comparisons"].append(observation(mode, expected));
    }
  }
  require(lexical && swapped && permuted, "undetected comparison substitution");
  report["alternatives_detected"]["lexical_numbers"] = lexical;
  report["alternatives_detected"]["swapped_operands"] = swapped;
  report["alternatives_detected"]["sort_position_instead_of_encounter"] =
      permuted;
  report["errors"] = Json::arrayValue;
  for (const auto &test : suite["errors"]) {
    const auto bad_rows = rows(test["rows"]);
    unsigned reads = 0, calls = 0;
    std::string phase = "create";
    bool failed = false;
    try {
      const auto result = filter(bad_rows, reads, test);
      phase = "visit";
      result([&](auto, const auto &) { ++calls; });
    } catch (const std::exception &) {
      failed = true;
    }
    require(failed && phase == test["phase"].asString() && calls == 0 &&
                reads == (phase == "create" ? 0 : 1),
            "unexpected error phase or delivered row " +
                test["name"].asString());
    Json::Value observed;
    observed["name"] = test["name"];
    observed["phase"] = phase;
    observed["callbacks"] = calls;
    observed["producer_reads"] = reads;
    report["errors"].append(observed);
  }
  Json::StreamWriterBuilder json;
  json["emitUTF8"] = true;
  std::cout << Json::writeString(json, report) << '\n';
} catch (const std::exception &e) {
  std::cerr << e.what() << '\n';
  return 1;
}
