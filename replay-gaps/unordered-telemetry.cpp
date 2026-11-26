#include "row-trial.hpp"
#include <algorithm>
#include <iostream>
#include <map>

using namespace miner;
using namespace row_replay;
namespace {
const std::string json = "application/json";
const std::string tsv = "text/tab-separated-values";
const std::string plain = "text/plain";
const std::string selector = R"(["sensor", "room", "minute", "temp"])";
const std::string empty_stop = "Expected between one and 32 ordered outcomes";
const Path response = "run/calls/interpret/response.json";
const Strings splits{"ordinary", "boundary", "empty", "transfer", "sparse"};
const Strings requests{"H031", "H034", "H036", "H040"};
const std::map<std::string, std::string> variants{
    {"H031", "strict-reset"},
    {"H034", "filter-before-reset"},
    {"H036", "global-previous"},
    {"H040", "greatest-minute"}};

struct Reading {
  std::string sensor, room;
  Json::Int64 minute, temp;
};

const std::vector<Reading> witness{
    {"A", "North", 90, 8},  {"B", "East", 80, 6},  {"A", "North", 91, 9},
    {"C", "West", 100, 12}, {"A", "North", 20, 6}, {"B", "East", 81, 7},
    {"A", "South", 21, 5},  {"B", "", 10, 5},      {"A", "South", 22, 8},
    {"C", "West", 3, -2},   {"B", "", 11, 8},      {"A", "South", 22, 4},
    {"B", "South", 12, 8},  {"A", "", 7, 8},       {"B", "South", 13, 7},
    {"A", "", 6, 9},        {"A", "", 5, 7},       {"D", "Dock", 12, 3}};

std::vector<Reading> readings(const std::string &data) {
  const auto rows = parse(data);
  check(rows.isArray(), "expected a telemetry array");
  std::vector<Reading> result;
  for (const auto &row : rows) {
    check(row["sensor"].isString() && row["room"].isString() &&
              row["minute"].isInt64() && row["temp"].isInt64(),
          "telemetry fixture differs from the declared domain");
    result.push_back({row["sensor"].asString(), row["room"].asString(),
                      row["minute"].asInt64(), row["temp"].asInt64()});
  }
  return result;
}

std::string encode(const std::vector<Reading> &rows) {
  auto result = array();
  for (const auto &r : rows)
    result.append(object({{"sensor", r.sensor},
                          {"room", r.room},
                          {"minute", r.minute},
                          {"temp", r.temp}}));
  return dump(result);
}

std::string oracle(const std::string &id, const std::vector<Reading> &rows,
                   std::string_view arm = "reference") {
  std::string output;
  if (id == "H031") {
    std::map<std::string, bool> suppressed;
    for (const auto &r : rows) {
      auto &hold = suppressed[r.sensor];
      if (r.temp >= 8 && !hold) {
        output += r.sensor + "\n";
        hold = true;
      } else if (r.temp < 4 || (r.temp == 4 && arm != "strict-reset")) {
        hold = false;
      }
    }
    return output;
  }
  if (id == "H034") {
    std::map<std::string, size_t> count;
    for (const auto &r : rows) {
      auto &length = count[r.sensor];
      if (r.temp > 5) {
        if (++length == 3)
          output += r.sensor + "\t" + std::to_string(r.minute) + "\n";
      } else if (arm != "filter-before-reset") {
        length = 0;
      }
    }
    return output;
  }
  if (id == "H036") {
    std::map<std::string, std::string> previous;
    for (const auto &r : rows) {
      const auto key = arm == "global-previous" ? std::string{} : r.sensor;
      const auto it = previous.find(key);
      if (it != previous.end() && it->second != r.room)
        output += r.sensor + "\t" + it->second + "\t" + r.room + "\n";
      previous[key] = r.room;
    }
    return output;
  }

  check(id == "H040", "unknown telemetry reference operation");
  std::map<std::string, const Reading *> latest;
  for (const auto &r : rows) {
    auto &last = latest[r.sensor];
    if (!last || arm != "greatest-minute" || r.minute >= last->minute)
      last = &r;
  }
  std::vector<const Reading *> ranked;
  for (const auto &[id, last] : latest)
    ranked.push_back(last);
  std::sort(ranked.begin(), ranked.end(),
            [](const Reading *a, const Reading *b) {
              if (a->temp != b->temp)
                return a->temp > b->temp;
              return a->sensor < b->sensor;
            });
  auto names = array();
  for (const auto *last : ranked)
    names.append(last->sensor);
  Json::FastWriter writer;
  writer.omitEndingLineFeed();
  return writer.write(names);
}

J variant(const std::string &id, J steps) {
  if (id == "H031") {
    check(steps[1]["outcome"] == "threshold_entries" &&
              steps[1]["parameters"]["reset"] == "le",
          "changed latch reset recipe");
    steps[1]["parameters"]["reset"] = "lt";
    return steps;
  }
  if (id == "H034") {
    check(steps[1]["outcome"] == "conditional_field" &&
              steps[2]["outcome"] == "running_total" &&
              steps[3]["outcome"] == "filter_records",
          "changed streak recipe");
    auto altered = array({steps[0], steps[3], steps[1], steps[2]});
    for (unsigned i = 4; i < steps.size(); ++i)
      altered.append(steps[i]);
    return altered;
  }
  if (id == "H036") {
    check(steps[1]["outcome"] == "previous_field" &&
              steps[1]["parameters"]["keys"] == "0",
          "changed room history recipe");
    steps[1]["parameters"]["keys"] = "all";
    return steps;
  }
  check(id == "H040" && steps[2]["outcome"] == "select_group_row" &&
            steps[2]["parameters"]["order_by"] == "",
        "changed last-reading recipe");
  steps[2]["parameters"]["order_by"] = "2:num:asc";
  return steps;
}
} // namespace

int main() try {
  const auto prior = runs / "v5-unordered-finance-replay-XXvu2pML/report.json";
  const auto prior_pin = hash(prior);
  check(prior_pin ==
            "20ab431f685db586b3fd07fcfb1d1a7e5bc0dba8727a14642aa7340ba5d3b252",
        "changed frequency report");
  const auto ranking = load(prior);
  auto ids = array();
  for (const auto &id : requests)
    ids.append(id);
  check(ranking["family_frequency"][4]["name"] == "telemetry" &&
            ranking["family_frequency"][4]["requests"] == ids,
        "changed telemetry cohort");
  const auto recipe = root / "shared/request-recipes.json";
  const auto recipe_pin = hash(recipe);
  check(recipe_pin ==
            "768676cbc34ef7db9d869520bd3d26d86123f49a3c9803901eb5653de3a6f49b",
        "changed supplied recipes");
  const auto recipes = load(recipe);

  const auto parent = runs / "v5-queue-XXH5XMi9/stage/package";
  const auto writer_stage = runs / "v5-tsv-writer-XXAf0x0y/stage";
  const auto base = parent / "bundle", with_tsv = writer_stage / "bundle";
  const auto base_pin = hash(parent / "manifest.json");
  const auto writer_pin = hash(writer_stage / "manifest.json");
  check(base_pin == "d15aee96917ff440f58f76270e3ca48575268dcfcf2bce951805c865aa"
                    "e3af63" &&
            writer_pin == "0047b47009df08f7ee228729eb46b4438ec9d59eadd7c44647ec"
                          "6761eeabb994",
        "changed catalog stages");
  verify_bundle(
      base, "436db92629463daee559de2cbbc1c81519e12306d5bf01cc6d2883f3140e779c");
  verify_bundle(
      with_tsv,
      "b2f8a40d44b1589779fc82fcfa23995df5cb2b2432167e9383f6f22e9c828d54");
  const auto writer =
      exact_row(with_tsv, "write_tsv_lines", "strings", "out:" + tsv);
  check(remove_binding(read_tree(with_tsv), writer).at("SHA256SUMS") ==
            bytes(base / "SHA256SUMS"),
        "catalog comparison differs outside the TSV writer");
  const auto reader = verify_json_records_scope(
      base, runs / "v5-json-records-XXptaI1o/stage",
      "c97ffce350f2eb6f582702d1dd6a9b803463ed0d35ae8a4668bd7d01d195b7cb",
      selector);
  const auto active = fs::canonical(volume / "unllm/boilerplate/active");
  const auto active_pin = hash(active / "SHA256SUMS");
  check(active_pin ==
            "e4564e3079f0b32b3379a411b170ce9af61ec80ab7938b6a974e724abd538328",
        "changed published catalog");

  const auto work = fresh_run("v5-unordered-telemetry-replay");
  std::cout << work.string() << std::endl;
  auto report =
      object({{"active_path", active.string()},
              {"active_sha256", active_pin},
              {"frequency_report_sha256", prior_pin},
              {"recipes_sha256", recipe_pin},
              {"parent_manifest_sha256", base_pin},
              {"writer_manifest_sha256", writer_pin},
              {"reader_scope", reader},
              {"writer", writer},
              {"latch", exact_row(base, "threshold_keys", "rows", "strings")},
              {"tools", tools()}});
  report["tools"]["unllm_logic_worker"] =
      hash(main_repo / "build/native/unllm_logic_worker");

  auto regrouped = witness;
  std::stable_sort(
      regrouped.begin(), regrouped.end(),
      [](const auto &a, const auto &b) { return a.sensor > b.sensor; });
  const std::map<std::string, std::vector<Reading>> fresh{
      {"interleaved", witness}, {"regrouped", regrouped}};
  check(oracle("H031", witness) == "A\nC\nB\nA\n" &&
            oracle("H031", witness, "strict-reset") == "A\nC\nB\n" &&
            oracle("H034", witness) == "A\t20\nB\t13\nA\t5\n" &&
            oracle("H034", witness, "filter-before-reset") ==
                "A\t20\nB\t11\n" &&
            oracle("H036", witness) ==
                "A\tNorth\tSouth\nB\tEast\t\nB\t\tSouth\nA\tSouth\t\n" &&
            oracle("H040", witness) == R"(["A","B","D","C"])" &&
            oracle("H040", witness, "greatest-minute") ==
                R"(["C","A","B","D"])" &&
            oracle("H040", witness) == oracle("H040", regrouped),
        "lost a frozen distinction in the telemetry witness");

  const auto archive = runs / "english-audit-bcsgf8pi/replay-staged";
  const auto historical = runs / "shared-review-5ui86km8/transfer";
  const std::map<std::string, std::string> operation_ids{
      {"H031", "threshold_keys"},
      {"H034", "running_total"},
      {"H036", "previous_field"},
      {"H040", "select_group_row"}};
  std::vector<Path> programs;
  for (const auto &id : requests) {
    const auto dir = archive / id;
    const auto &record = ranking["archive"][id];
    const auto saved_pin = hash(dir / response);
    const auto saved = load(dir / response);
    const auto &role = saved["roles"][0];
    const auto output = role["output_type"].asString();
    const auto mode = id == "H031" ? plain : id == "H040" ? json : tsv;
    const auto steps = operations(recipes[id][0]);
    check(saved_pin == record["reply_sha256"].asString() &&
              hash(record["reply_source"].asString()) == saved_pin &&
              hash(dir / "request.txt") ==
                  record["request_sha256"].asString() &&
              saved["input_type"] == json && saved["roles"].size() == 1 &&
              output == mode && role["requirement"]["operations"] == array() &&
              steps[0]["outcome"] == "read_json_records" &&
              steps[0]["parameters"]["fields"] == selector,
          "changed saved or supplied telemetry context for " + id);
    auto &entry = report["telemetry"][id];
    entry["archive"] = record;
    const auto archived =
        plan_probe(base, work / "plans" / id / "archived", saved);
    check(archived["gaps"]["r"] == empty_stop && archived["routes"].empty(),
          "changed archived operation stop for " + id + ": " + dump(archived));
    entry["plans"]["archived"] = archived;

    auto supplied = saved;
    auto &requirement = supplied["roles"][0]["requirement"];
    requirement["uncovered"] = array();
    requirement["operations"] = steps;
    if (output == tsv) {
      const auto gap =
          plan_probe(base, work / "plans" / id / "without-writer", supplied);
      check(gap["routes"].size() == 1 && gap["validation"].empty() &&
                gap["routes"][0]["ports"] ==
                    array({array({"strings", "out:" + tsv})}),
            "changed telemetry sink gap for " + id + ": " + dump(gap));
      entry["plans"]["without-writer"] = gap;
    }

    const auto fixtures = historical / id / "fixtures";
    auto inputs = object();
    for (const auto &split : splits)
      inputs[split] = hash(fixtures / split / "input/readings.json");
    entry["fixture_input_sha256"] = inputs;
    const auto &bundle = output == tsv ? with_tsv : base;
    const auto sink = output == tsv    ? "write_tsv_lines"
                      : output == json ? "write_texts"
                                       : "write_lines";
    const std::string suffix = output == json ? ".json" : ".txt";
    for (const auto &arm : {std::string("reference"), variants.at(id)}) {
      auto spec = supplied;
      if (arm != "reference")
        spec["roles"][0]["requirement"]["operations"] = variant(id, steps);
      const auto file = work / "specs" / arm / (id + ".json");
      write(file, spec);
      const auto result =
          trial(work, arm, id, bundle, file, "readings.json", inputs);
      const auto &emitted = result["roles"]["0"];
      const auto exe = work / arm / id / "run/project/converter";
      entry["runs"][arm] = result;
      write(work / "progress.json", report);
      check(complete(emitted) && fs::is_regular_file(exe) &&
                emitted["steps"][0]["provider"] == "op_0_read_json_records" &&
                emitted["steps"][emitted["steps"].size() - 1]["provider"] ==
                    sink &&
                selects(emitted, operation_ids.at(id)),
            "unexpected telemetry route for " + id + " " + arm);
      programs.push_back(exe);
      for (const auto &split : splits) {
        const auto fixture = fixtures / split;
        const auto rows = readings(bytes(fixture / "input/readings.json"));
        const auto correct = oracle(id, rows);
        const auto predicted = oracle(id, rows, arm);
        check(correct == bytes(fixture / "output" / ("result-1" + suffix)) &&
                  result["outputs"][split][0] == predicted &&
                  result["scores"][split] == (predicted == correct),
              "archived output differs from oracle for " + id + " " + arm +
                  " " + split + ": " + dump(result["outputs"][split]));
      }
      if (arm != "reference")
        check(oracle(id, witness, arm) != oracle(id, witness),
              "interleaved witness did not detect " + arm);
      for (const auto &[name, rows] : fresh) {
        const auto e =
            extra_case(work / "witness" / id / arm / name, exe, bundle,
                       encode(rows), array({oracle(id, rows, arm)}));
        check(e["passed"] == true, "telemetry witness differs for " + id + " " +
                                       arm + " " + name + ": " + dump(e));
        entry["witness"][arm][name] = e;
      }
      write(work / "progress.json", report);
    }
  }
  check(hash(active / "SHA256SUMS") == active_pin, "published catalog changed");
  write(work / ".report.json", report);
  run({"sync", "-f", work});
  for (const auto &exe : programs)
    check(fs::remove(exe), "could not retire " + exe.string());
  fs::rename(work / ".report.json", work / "report.json");
  std::cout << hash(work / "report.json") << std::endl;
  return 0;
} catch (const std::exception &e) {
  std::cerr << e.what() << '\n';
  return 1;
}
