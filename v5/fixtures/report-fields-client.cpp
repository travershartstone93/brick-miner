#include "miner_report_fields.hpp"
#include <algorithm>
#include <charconv>
#include <fstream>
#include <iostream>
#include <json/json.h>
#include <set>
#include <sstream>
#include <stdexcept>

namespace {
using Rows = std::vector<std::pair<std::int64_t, brick_data::Fields>>;

void require(bool ok, const std::string &message) {
  if (!ok)
    throw std::runtime_error(message);
}

Rows load_rows(const Json::Value &value) {
  Rows input;
  for (const auto &row : value) {
    brick_data::Fields fields;
    for (const auto &field : row[1])
      fields.push_back(field.asString());
    input.emplace_back(row[0].asInt64(), std::move(fields));
  }
  return input;
}

brick_data::Rows producer(const Rows &rows, unsigned &reads,
                          bool fail_at_end = false) {
  return [&rows, &reads, fail_at_end](const brick_data::Visitor &emit) {
    ++reads;
    for (const auto &[pos, fields] : rows)
      emit(pos, fields);
    if (fail_at_end)
      throw std::runtime_error("upstream visitor failed");
  };
}

enum class Alternative {
  none,
  omit_empty,
  unique_selectors,
  sort_positions,
  renumber
};

std::vector<std::string> oracle(Rows rows, const std::string &selector,
                                Alternative mode = Alternative::none) {
  if (mode == Alternative::sort_positions)
    std::stable_sort(
        rows.begin(), rows.end(),
        [](const auto &a, const auto &b) { return a.first < b.first; });
  std::vector<std::string> selected;
  std::set<std::string> seen;
  std::istringstream names(selector);
  for (std::string name; std::getline(names, name, ',');)
    if (mode != Alternative::unique_selectors || seen.insert(name).second)
      selected.push_back(name);
  std::vector<std::string> output;
  unsigned counter = 0;
  for (const auto &[pos, row] : rows) {
    ++counter;
    std::string line;
    bool first = true;
    for (const auto &name : selected) {
      std::string field;
      if (name == "pos") {
        field = mode == Alternative::renumber ? std::to_string(counter)
                                              : std::to_string(pos);
      } else {
        std::size_t column = 0;
        const auto end = name.data() + name.size();
        const auto parsed = std::from_chars(name.data(), end, column);
        require(parsed.ec == std::errc{} && parsed.ptr == end,
                "invalid selector in positive fixture");
        field = row.at(column);
      }
      if (mode == Alternative::omit_empty && field.empty())
        continue;
      if (!first)
        line += '\t';
      line += field;
      first = false;
    }
    output.push_back(std::move(line));
  }
  return output;
}
} // namespace

int main(int argc, char **argv) try {
  require(argc == 2, "fixture file required");
  std::ifstream file(argv[1]);
  Json::Value spec;
  file >> spec;
  const auto common = load_rows(spec["rows"]);
  Json::Value report(Json::objectValue);
  report["reports"] = Json::arrayValue;
  report["errors"] = Json::arrayValue;
  const std::pair<Alternative, const char *> controls[]{
      {Alternative::omit_empty, "omit_empty_fields"},
      {Alternative::unique_selectors, "deduplicate_selector"},
      {Alternative::sort_positions, "sort_by_position"},
      {Alternative::renumber, "replace_original_positions"}};
  for (const auto &[mode, label] : controls)
    report["alternatives_detected"][label] = 0u;
  for (const auto &test : spec["direct"]) {
    const auto rows = test.isMember("rows") ? load_rows(test["rows"]) : common;
    const auto fields = test["fields"].asString();
    const auto expected = oracle(rows, fields);
    for (const auto &[mode, label] : controls)
      report["alternatives_detected"][label] =
          report["alternatives_detected"][label].asUInt() +
          (oracle(rows, fields, mode) != expected);
    unsigned reads = 0;
    const auto input = producer(rows, reads);
    for (unsigned call = 0; call < 2; ++call)
      require(brick_miner::shared::report_fields(input, fields) == expected &&
                  reads == call + 1,
              "unexpected report or producer reads for " +
                  test["name"].asString());
    Json::Value entry(Json::objectValue);
    entry["name"] = test["name"];
    entry["fields"] = fields;
    entry["reports"] = Json::arrayValue;
    for (const auto &line : expected)
      entry["reports"].append(line);
    report["reports"].append(entry);
  }
  for (const auto &entry : report["alternatives_detected"])
    require(entry.asUInt() > 0, "report fixtures missed an altered behavior");
  for (const auto &test : spec["errors"]) {
    const auto rows = test.isMember("rows") ? load_rows(test["rows"]) : common;
    unsigned reads = 0;
    const auto input =
        producer(rows, reads, test.get("producer_error", false).asBool());
    bool failed = false;
    try {
      (void)brick_miner::shared::report_fields(input,
                                               test["fields"].asString());
    } catch (const std::exception &) {
      failed = true;
    }
    require(failed && reads == test["reads"].asUInt(),
            "unexpected report error or read " + test["name"].asString());
    Json::Value entry(Json::objectValue);
    entry["name"] = test["name"];
    entry["producer_reads"] = reads;
    entry["returned"] = false;
    report["errors"].append(entry);
  }
  Json::StreamWriterBuilder json;
  json["emitUTF8"] = true;
  std::cout << Json::writeString(json, report) << '\n';
} catch (const std::exception &e) {
  std::cerr << e.what() << '\n';
  return 1;
}
