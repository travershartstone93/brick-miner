#include "miner_order_records.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <json/json.h>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {
using J = Json::Value;

void require(bool condition, const std::string &message) {
  if (!condition)
    throw std::runtime_error(message);
}

brick_data::Rows prepare(const J &input, const J &trial,
                         const std::shared_ptr<int> &visits) {
  auto rows = [input, visits](const brick_data::Visitor &visit) {
    ++*visits;
    for (const auto &row : input) {
      brick_data::Fields fields;
      for (const auto &field : row[1])
        fields.push_back(field.asString());
      visit(row[0].asInt64(), fields);
    }
  };
  return brick_miner::shared::order_records(rows, trial["order_by"].asString());
}

J observe(const brick_data::Rows &rows) {
  J observed(Json::arrayValue);
  rows([&](std::int64_t pos, const brick_data::Fields &fields) {
    J cells(Json::arrayValue);
    for (const auto &field : fields)
      cells.append(field);
    J row(Json::arrayValue);
    row.append(Json::Int64(pos));
    row.append(cells);
    observed.append(row);
  });
  return observed;
}

J expected(const J &input, const J &order) {
  require(input.size() == order.size(), "invalid frozen row count");
  J output(Json::arrayValue);
  std::vector<bool> seen(input.size(), false);
  for (const auto &index : order) {
    const auto i = index.asUInt();
    require(i < input.size() && !seen[i], "invalid frozen row permutation");
    seen[i] = true;
    output.append(input[i]);
  }
  return output;
}

J failed(const J &input, const J &trial) {
  auto visits = std::make_shared<int>(0);
  brick_data::Rows rows;
  try {
    rows = prepare(input, trial, visits);
  } catch (const std::invalid_argument &) {
    require(trial["error"] == "invalid_argument" && *visits == 0,
            "unexpected argument failure");
    return "invalid_argument";
  }
  const auto message = trial["error"].asString();
  require(message != "invalid_argument" && *visits == 0, "expected eager argument validation");
  int emitted = 0;
  try {
    rows([&](std::int64_t, const brick_data::Fields &) { ++emitted; });
  } catch (const std::runtime_error &e) {
    require(std::string(e.what()).find(message) != std::string::npos &&
                emitted == 0 && *visits == 1,
            "unexpected row-consumption failure: " + std::string(e.what()));
    return e.what();
  }
  throw std::runtime_error("invalid input produced a result");
}
} // namespace

int main(int argc, char **argv) try {
  require(argc == 2, "fixture path required");
  std::ifstream file(argv[1]);
  J spec, report;
  file >> spec;
  auto input = [&](const J &trial) -> const J & {
    return trial.isMember("rows") ? trial["rows"] : spec["rows"];
  };
  for (const auto &trial : spec["direct"]) {
    const auto name = trial["name"].asString();
    auto visits = std::make_shared<int>(0);
    const auto rows = prepare(input(trial), trial, visits);
    require(*visits == 0, "ordering consumed rows during preparation: " + name);
    const auto want = expected(input(trial), trial["order"]);
    const auto actual = observe(rows);
    require(actual == want && observe(rows) == want && *visits == 2,
            "incorrect repeated ordering for " + name);
    report["orderings"][name] = actual;
  }
  for (const auto &trial : spec["errors"]) {
    const auto result = failed(input(trial), trial);
    require(failed(input(trial), trial) == result, "failure did not reproduce");
    report["errors"][trial["name"].asString()] = result;
  }
  const auto &direct = spec["direct"];
  for (const auto &control : spec["controls"]) {
    const auto witness = std::find_if(direct.begin(), direct.end(), [&](const J &test) {
      return test["name"] == control["witness"];
    });
    require(witness != direct.end(), "missing ordering control witness");
    auto changed = *witness;
    changed["order_by"] = control["order_by"];
    const auto actual = observe(prepare(input(changed), changed, std::make_shared<int>(0)));
    const auto name = control["name"].asString();
    require(actual != expected(input(*witness), (*witness)["order"]),
            "indistinguishable ordering substitution for " + name);
    report["controls"][name] = actual;
  }
  Json::StreamWriterBuilder writer;
  std::cout << Json::writeString(writer, report) << '\n';
  return 0;
} catch (const std::exception &e) {
  std::cerr << e.what() << '\n';
  return 1;
}
