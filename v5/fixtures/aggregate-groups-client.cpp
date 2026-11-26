#include "miner_aggregate_groups.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <json/json.h>
#include <stdexcept>
#include <type_traits>

namespace {
using J = Json::Value;
using brick_data::Fields;
using brick_miner::records::Groups;
using brick_miner::records::aggregate_groups;
using Signature = brick_data::Rows (*)(const Groups &, const std::string &,
                                       std::size_t, const std::string &);
static_assert(std::is_same_v<decltype(&aggregate_groups), Signature>);

void require(bool ok, const std::string &message) {
  if (!ok)
    throw std::runtime_error(message);
}

Fields fields(const J &input) {
  Fields result;
  for (const auto &field : input)
    result.push_back(field.asString());
  return result;
}

brick_data::Rows producer(const J &input, const J &trial) {
  Groups groups;
  for (const auto &item : input) {
    auto &group = groups.emplace_back();
    group.key = fields(item["key"]);
    for (const auto &row : item["rows"])
      group.rows.push_back({row[0].asInt64(), fields(row[1])});
  }
  return aggregate_groups(groups, trial["measure"].asString(),
                          trial["value_column"].asUInt64(),
                          trial["result_order"].asString());
}

J positioned(std::int64_t position, const Fields &values) {
  J row(Json::arrayValue), data(Json::arrayValue);
  row.append(Json::Int64(position));
  for (const auto &value : values)
    data.append(value);
  row.append(data);
  return row;
}

J visit(const brick_data::Rows &rows) {
  J result(Json::arrayValue);
  rows([&](std::int64_t pos, const Fields &data) {
    result.append(positioned(pos, data));
  });
  return result;
}

J expected(const J &input, const J &trial) {
  J result(Json::arrayValue);
  std::int64_t pos = 0;
  for (const auto &pair : trial["expected"]) {
    const auto &group = input[pair[0].asUInt()];
    require(group.isObject() && group["key"].isArray(),
            "invalid frozen group coordinate");
    auto keys = fields(group["key"]);
    keys.push_back(pair[1].asString());
    result.append(positioned(++pos, keys));
  }
  return result;
}

std::string error(const J &input, const J &trial) {
  try {
    producer(input, trial);
  } catch (const std::overflow_error &) {
    return "overflow_error";
  } catch (const std::out_of_range &) {
    return "out_of_range";
  } catch (const std::invalid_argument &) {
    return "invalid_argument";
  }
  return "returned producer";
}
} // namespace

int main(int argc, char **argv) try {
  require(argc == 2, "fixture path required");
  std::ifstream file(argv[1]);
  J spec, report;
  file >> spec;
  auto input = [&](const J &trial) -> const J & {
    return trial.isMember("groups") ? trial["groups"] : spec["groups"];
  };
  for (const auto &trial : spec["direct"]) {
    const auto want = expected(input(trial), trial);
    const auto rows = producer(input(trial), trial);
    const auto name = trial["name"].asString();
    const auto actual = visit(rows);
    require(actual == want && visit(rows) == want &&
                visit(producer(input(trial), trial)) == want,
            "incorrect aggregation or lifetime for " + name);
    report["aggregates"][name] = actual;
  }
  for (const auto &trial : spec["errors"]) {
    const auto result = error(input(trial), trial);
    const auto name = trial["name"].asString();
    require(result == trial["error"].asString() && error(input(trial), trial) == result,
            "incorrect aggregation failure for " + name);
    report["errors"][name] = result;
  }
  const auto &direct = spec["direct"];
  for (const auto &control : spec["controls"]) {
    const auto witness = std::find_if(direct.begin(), direct.end(), [&](const J &test) {
      return test["name"] == control["witness"];
    });
    require(witness != direct.end(), "missing aggregation control witness");
    auto changed = *witness;
    changed[control["field"].asString()] = control["value"];
    const auto actual = visit(producer(input(changed), changed));
    const auto name = control["name"].asString();
    require(actual != expected(input(*witness), *witness),
            "indistinguishable aggregation substitution for " + name);
    report["controls"][name] = actual;
  }
  Json::StreamWriterBuilder writer;
  std::cout << Json::writeString(writer, report) << '\n';
  return 0;
} catch (const std::exception &e) {
  std::cerr << e.what() << '\n';
  return 1;
}
