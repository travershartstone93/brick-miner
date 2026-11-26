#include "miner_group_rows.hpp"
#include <algorithm>
#include <charconv>
#include <fstream>
#include <iostream>
#include <json/json.h>
#include <sstream>
#include <stdexcept>

namespace {
using Row = std::pair<std::int64_t, brick_data::Fields>;
using Rows = std::vector<Row>;

void require(bool ok, const std::string &message) {
  if (!ok)
    throw std::runtime_error(message);
}

Rows load_rows(const Json::Value &items) {
  Rows rows;
  for (const auto &item : items) {
    brick_data::Fields fields;
    for (const auto &field : item[1])
      fields.push_back(field.asString());
    rows.emplace_back(item[0].asInt64(), std::move(fields));
  }
  return rows;
}

brick_data::Rows producer(const Rows &rows, unsigned &reads,
                          bool fail_at_end = false) {
  return [&rows, &reads, fail_at_end](const brick_data::Visitor &emit) {
    ++reads;
    for (const auto &[position, fields] : rows)
      emit(position, fields);
    if (fail_at_end)
      throw std::runtime_error("upstream visitor failed");
  };
}

Json::Value strings(const brick_data::Fields &fields) {
  Json::Value result(Json::arrayValue);
  for (const auto &field : fields)
    result.append(field);
  return result;
}

void append_group(Json::Value &output, const brick_data::Fields &key,
                  const Rows &rows) {
  Json::Value group(Json::objectValue);
  group["key"] = strings(key);
  group["rows"] = Json::arrayValue;
  for (const auto &[position, fields] : rows) {
    Json::Value item(Json::arrayValue);
    item.append(Json::Int64(position));
    item.append(strings(fields));
    group["rows"].append(item);
  }
  output.append(group);
}

enum class Alter {
  none,
  concatenate,
  nul_terminated,
  encounter,
  position,
  unique
};
struct Group {
  brick_data::Fields key, bucket;
  Rows rows;
};

Json::Value oracle(const Rows &rows, const Json::Value &test,
                   Alter control = Alter::none) {
  std::vector<std::size_t> columns;
  std::istringstream selectors(test["keys"].asString());
  for (std::string selector; std::getline(selectors, selector, ',');) {
    std::string_view token(selector);
    if (!token.empty() && token.front() == '+')
      token.remove_prefix(1);
    std::size_t index = 0;
    const auto end = token.data() + token.size();
    const auto parsed = std::from_chars(token.data(), end, index);
    require(parsed.ec == std::errc{} && parsed.ptr == end,
            "invalid selector in positive fixture");
    columns.push_back(index);
  }
  std::vector<Group> groups;
  for (const auto &row : rows) {
    brick_data::Fields key;
    for (const auto index : columns)
      key.push_back(row.second.at(index));
    auto bucket = key;
    if (control == Alter::concatenate) {
      std::string joined;
      for (const auto &field : key)
        joined += field;
      bucket = {joined};
    } else if (control == Alter::nul_terminated) {
      for (auto &field : bucket)
        field = field.substr(0, field.find('\0'));
    }
    auto group =
        std::find_if(groups.begin(), groups.end(),
                     [&](const Group &g) { return g.bucket == bucket; });
    if (group == groups.end()) {
      groups.push_back({std::move(key), std::move(bucket), {row}});
    } else if (control != Alter::unique ||
               std::find(group->rows.begin(), group->rows.end(), row) ==
                   group->rows.end()) {
      group->rows.push_back(row);
    }
  }
  auto order = test["group_order"].asString();
  if (order != "encounter" && control != Alter::encounter)
    std::sort(groups.begin(), groups.end(),
              [&](const Group &a, const Group &b) {
                return order == "asc" ? a.key < b.key : b.key < a.key;
              });
  Json::Value result(Json::arrayValue);
  for (auto &group : groups) {
    if (control == Alter::position)
      std::stable_sort(
          group.rows.begin(), group.rows.end(),
          [](const Row &a, const Row &b) { return a.first < b.first; });
    append_group(result, group.key, group.rows);
  }
  return result;
}

Json::Value observe(const brick_data::Rows &input, const Json::Value &test) {
  const auto actual = brick_miner::records::group_rows(
      input, test["keys"].asString(), test["group_order"].asString());
  Json::Value result(Json::arrayValue);
  for (const auto &group : actual) {
    Rows rows;
    for (const auto &row : group.rows)
      rows.emplace_back(row.position, row.fields);
    append_group(result, group.key, rows);
  }
  return result;
}
} // namespace

int main(int argc, char **argv) try {
  require(argc == 2, "fixture file required");
  std::ifstream file(argv[1]);
  Json::Value spec;
  file >> spec;
  const auto common = load_rows(spec["rows"]);
  Json::Value report(Json::objectValue);
  report["groupings"] = Json::arrayValue;
  report["errors"] = Json::arrayValue;
  const std::pair<Alter, const char *> controls[]{
      {Alter::concatenate, "concatenate_composite_keys"},
      {Alter::nul_terminated, "truncate_keys_at_nul"},
      {Alter::encounter, "ignore_requested_order"},
      {Alter::position, "sort_members_by_position"},
      {Alter::unique, "drop_duplicate_members"}};
  for (const auto &[mode, label] : controls)
    report["alternatives_detected"][label] = 0u;
  for (const auto &test : spec["direct"]) {
    const auto rows = test.isMember("rows") ? load_rows(test["rows"]) : common;
    const auto expected = oracle(rows, test);
    for (const auto &[mode, label] : controls)
      report["alternatives_detected"][label] =
          report["alternatives_detected"][label].asUInt() +
          (oracle(rows, test, mode) != expected);
    unsigned reads = 0;
    const auto input = producer(rows, reads);
    for (unsigned call = 0; call < 2; ++call)
      require(observe(input, test) == expected && reads == call + 1,
              "unexpected grouping or producer reads for " +
                  test["name"].asString());
    Json::Value item(Json::objectValue);
    item["name"] = test["name"];
    item["groups"] = expected;
    report["groupings"].append(item);
  }
  for (const auto &count : report["alternatives_detected"])
    require(count.asUInt() > 0, "fixtures missed an altered grouping behavior");

  for (const auto &test : spec["errors"]) {
    const auto rows = test.isMember("rows") ? load_rows(test["rows"]) : common;
    unsigned reads = 0;
    const auto input =
        producer(rows, reads, test.get("producer_error", false).asBool());
    bool failed = false;
    try {
      (void)observe(input, test);
    } catch (const std::exception &) {
      failed = true;
    }
    require(failed && reads == test["reads"].asUInt(),
            "unexpected grouping error or read " + test["name"].asString());
    Json::Value item(Json::objectValue);
    item["name"] = test["name"];
    item["producer_reads"] = reads;
    item["returned"] = false;
    report["errors"].append(item);
  }
  Json::StreamWriterBuilder json;
  json["emitUTF8"] = true;
  std::cout << Json::writeString(json, report) << '\n';
} catch (const std::exception &e) {
  std::cerr << e.what() << '\n';
  return 1;
}
