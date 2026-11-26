#include "miner_select_group_row.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <json/json.h>
#include <stdexcept>
#include <type_traits>

namespace {
using J = Json::Value;
using brick_miner::records::Groups;
using brick_miner::records::select_group_row;
using Signature = brick_data::Rows (*)(const Groups &, const std::string &,
                                       const std::string &, const std::string &);
static_assert(std::is_same_v<decltype(&select_group_row), Signature>);

void require(bool ok, const std::string &message) {
  if (!ok)
    throw std::runtime_error(message);
}

brick_data::Rows select_rows(const J &input, const J &trial) {
  Groups groups;
  for (const auto &group : input) {
    auto &rows = groups.emplace_back().rows;
    for (const auto &row : group) {
      auto &fields = rows.emplace_back(
          brick_miner::records::Row{row[0].asInt64(), {}}).fields;
      for (const auto &field : row[1])
        fields.push_back(field.asString());
    }
  }
  return select_group_row(groups, trial["which"].asString(),
                          trial["order_by"].asString(),
                          trial["result_order"].asString());
}

J collect(const brick_data::Rows &rows) {
  J result(Json::arrayValue);
  rows([&](std::int64_t pos, const brick_data::Fields &fields) {
    J row(Json::arrayValue), values(Json::arrayValue);
    row.append(Json::Int64(pos));
    for (const auto &field : fields)
      values.append(field);
    row.append(values);
    result.append(row);
  });
  return result;
}

J expected(const J &input, const J &trial) {
  J rows(Json::arrayValue);
  for (const auto &pair : trial["selected"]) {
    const auto &row = input[pair[0].asUInt()][pair[1].asUInt()];
    require(row.isArray(), "invalid frozen row coordinate");
    J positioned(Json::arrayValue);
    positioned.append(Json::Int64(row[0].asInt64()));
    positioned.append(row[1]);
    rows.append(positioned);
  }
  return rows;
}

std::string observe_error(const J &input, const J &trial) {
  try {
    select_rows(input, trial);
  } catch (const std::out_of_range &) {
    return "out_of_range";
  } catch (const std::invalid_argument &) {
    return "invalid_argument";
  }
  return "returned producer";
}
} // namespace

int main(int argc, char **argv) try {
  require(argc == 2, "fixture file required");
  std::ifstream file(argv[1]);
  J spec;
  file >> spec;
  auto input = [&](const J &trial) -> const J & {
    return trial.isMember("groups") ? trial["groups"] : spec["groups"];
  };
  J output;
  for (const auto &trial : spec["direct"]) {
    const auto wanted = expected(input(trial), trial);
    const auto rows = select_rows(input(trial), trial);
    const auto actual = collect(rows);
    const auto name = trial["name"].asString();
    require(actual == wanted && collect(rows) == wanted &&
                collect(select_rows(input(trial), trial)) == wanted,
            "wrong selected rows or ownership for " + name);
    output["selections"][name] = actual;
  }
  for (const auto &trial : spec["errors"]) {
    const auto actual = observe_error(input(trial), trial);
    const auto name = trial["name"].asString();
    require(actual == trial["error"].asString() &&
                observe_error(input(trial), trial) == actual,
            "wrong exception for " + name);
    output["errors"][name] = actual;
  }
  const auto &cases = spec["direct"];
  for (const auto &control : spec["controls"]) {
    const auto witness = std::find_if(cases.begin(), cases.end(), [&](const J &test) {
      return test["name"] == control["witness"];
    });
    require(witness != cases.end(), "missing control witness");
    auto changed = *witness;
    changed[control["field"].asString()] = control["value"];
    const auto actual = collect(select_rows(input(changed), changed));
    const auto name = control["name"].asString();
    require(actual != expected(input(*witness), *witness),
            "indistinguishable substituted behavior for " + name);
    output["controls"][name] = actual;
  }
  Json::StreamWriterBuilder writer;
  std::cout << Json::writeString(writer, output) << '\n';
  return 0;
} catch (const std::exception &e) {
  std::cerr << e.what() << '\n';
  return 1;
}
