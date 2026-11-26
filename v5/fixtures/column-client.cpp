#include "tab_provider.hpp"
#include <json/json.h>
#include <algorithm>
#include <iostream>
#include <set>
#include <stdexcept>
#include <type_traits>

using J = Json::Value;
using brick_data::Fields;
using brick_data::Rows;
using brick_data::Visitor;
using Positioned = std::pair<std::int64_t, Fields>;
static_assert(std::is_same_v<decltype(&brick_data::column),
                             Fields (*)(const Rows &, std::size_t)>);

std::vector<Positioned> positioned(const J &rows) {
  std::vector<Positioned> result;
  for (const auto &row : rows) {
    Fields fields;
    for (const auto &field : row[1])
      fields.push_back(field.asString());
    result.emplace_back(row[0].asInt64(), std::move(fields));
  }
  return result;
}

J strings(const Fields &fields) {
  J result(Json::arrayValue);
  for (const auto &v : fields)
    result.append(v);
  return result;
}

J observe(const J &spec) {
  J result(Json::objectValue);
  auto records = positioned(spec["rows"]);
  const auto index = spec["index"].asUInt64();
  unsigned visits = 0;
  const Rows provider = [&](const Visitor &visit) {
    for (const auto &[pos, fields] : records) {
      ++visits;
      visit(pos, fields);
    }
    if (spec.get("producer_error", false).asBool())
      throw std::runtime_error("producer stopped");
  };
  try {
    const auto values = brick_data::column(provider, index);
    for (auto &row : records)
      for (auto &field : row.second)
        field = "changed after return";
    result["observed"] = strings(values);
  } catch (const std::out_of_range &) {
    result["observed"] = "out_of_range";
  } catch (const std::runtime_error &e) {
    result["observed"] = e.what();
  }
  result["visits"] = visits;
  return result;
}

int main() try {
  Json::CharReaderBuilder parser;
  J spec;
  std::string error;
  if (!Json::parseFromStream(parser, std::cin, &spec, &error))
    throw std::runtime_error(error);
  J results;
  for (const auto *kind : {"direct", "errors"}) {
    for (const auto &row : spec[kind]) {
      auto actual = observe(row);
      const auto expected = row.isMember("expected") ? row["expected"] : row["observed"];
      const auto visits = row.get("visits", row["rows"].size()).asUInt();
      if (actual["observed"] != expected || actual["visits"].asUInt() != visits ||
          actual != observe(row))
        throw std::runtime_error("unexpected observation for " + row["name"].asString());
      results[kind][row["name"].asString()] = actual;
    }
  }
  const auto &witness = spec["direct"][0];
  auto sorted = positioned(witness["rows"]);
  std::stable_sort(sorted.begin(), sorted.end(),
                   [](const auto &a, const auto &b) { return a.first < b.first; });
  Fields by_position, first, unique, no_empty;
  std::set<std::string> seen;
  for (const auto &row : sorted)
    by_position.push_back(row.second[1]);
  for (const auto &row : positioned(witness["rows"])) {
    first.push_back(row.second[0]);
    if (seen.insert(row.second[1]).second)
      unique.push_back(row.second[1]);
    if (!row.second[1].empty())
      no_empty.push_back(row.second[1]);
  }
  results["distinguishing"]["position_order"] = strings(by_position);
  results["distinguishing"]["first_column"] = strings(first);
  results["distinguishing"]["drop_duplicates"] = strings(unique);
  results["distinguishing"]["drop_empty"] = strings(no_empty);
  for (const auto &mutant : results["distinguishing"])
    if (mutant == witness["expected"])
      throw std::runtime_error("undistinguished projected output");
  Json::StreamWriterBuilder writer;
  std::cout << Json::writeString(writer, results) << '\n';
  return 0;
} catch (const std::exception &e) {
  std::cerr << e.what() << '\n';
  return 1;
}
