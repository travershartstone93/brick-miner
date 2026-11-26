#include "row-trial.hpp"
#include <algorithm>
#include <iostream>
#include <map>
#include <set>

using namespace miner;
using namespace row_replay;
namespace {
const std::string json = "application/json";
const std::string tsv = "text/tab-separated-values";
const std::string plain = "text/plain";
const std::string empty_stop = "Expected between one and 32 ordered outcomes";
const Strings splits{"ordinary", "boundary", "empty", "transfer", "sparse"};
const Strings requests{"H091", "H093", "H094", "H097", "H098", "H099", "H100"};
const Path response = "run/calls/interpret/response.json";
const std::string witness = R"([
  {"id":"r-start","room":"Birch","start":60,"stop":61,"state":"active"},
  {"id":"b-inner","room":"Amber","start":55,"stop":65,"state":"active"},
  {"id":"ca-first","room":"Amber","start":1,"stop":8,"state":"canceled"},
  {"id":"y-after","room":"Amber","start":70,"stop":75,"state":"active"},
  {"id":"cc-cover","room":"Cedar","start":0,"stop":100,"state":"canceled"},
  {"id":"m-left","room":"Amber","start":50,"stop":60,"state":"active"},
  {"id":"d-start","room":"Birch","start":60,"stop":64,"state":"active"},
  {"id":"a-right","room":"Amber","start":60,"stop":70,"state":"active"},
  {"id":"f-only","room":"Delta","start":-5,"stop":0,"state":"active"},
  {"id":"ca-second","room":"Amber","start":6,"stop":12,"state":"canceled"},
  {"id":"e-stop","room":"Birch","start":52,"stop":60,"state":"active"},
  {"id":"k-inner","room":"Amber","start":55,"stop":65,"state":"active"},
  {"id":"q-gap","room":"Amber","start":81,"stop":84,"state":"active"},
  {"id":"z-long","room":"Amber","start":50,"stop":70,"state":"active"},
  {"id":"cb-single","room":"Birch","start":200,"stop":213,"state":"canceled"},
  {"id":"g-later","room":"Delta","start":2,"stop":3,"state":"active"}
])";

struct Booking {
  std::string id, room;
  int start, stop;
};

template <class Map> std::string report_rows(const Map &values) {
  std::string result;
  for (const auto &[key, value] : values)
    result += key + "\t" + std::to_string(value) + "\n";
  return result;
}

std::string ids_json(const std::set<std::string> &ids) {
  auto values = array();
  for (const auto &id : ids)
    values.append(id);
  Json::FastWriter writer;
  writer.omitEndingLineFeed();
  return writer.write(values);
}

J reference(const J &rows) {
  std::vector<Booking> active;
  std::map<std::string, int> canceled_sum, occupied, peak, inclusive_peak;
  std::map<std::string, std::map<int, int>> active_minutes, inclusive_minutes;
  std::map<std::string, std::set<int>> canceled_minutes;
  std::set<std::string> all, at_sixty, include_stop, containers, overlaps,
      successors;
  for (const auto &r : rows) {
    Booking b{r["id"].asString(), r["room"].asString(), r["start"].asInt(),
              r["stop"].asInt()};
    check(all.insert(b.id).second && b.start >= -300 && b.stop <= 300 &&
              b.start < b.stop,
          "invalid bounded booking witness");
    if (r["state"] == "canceled") {
      canceled_sum[b.room] += b.stop - b.start;
      for (int minute = b.start; minute < b.stop; ++minute)
        canceled_minutes[b.room].insert(minute);
      continue;
    }
    check(r["state"] == "active", "invalid witness state");
    active.push_back(b);
    if (b.start <= 60 && 60 <= b.stop)
      include_stop.insert(b.id);
    for (int minute = b.start; minute < b.stop; ++minute) {
      ++active_minutes[b.room][minute];
      if (minute == 60)
        at_sixty.insert(b.id);
    }
    for (int minute = b.start; minute <= b.stop; ++minute)
      ++inclusive_minutes[b.room][minute];
  }
  for (const auto &a : active)
    for (const auto &b : active) {
      if (a.room != b.room || a.id == b.id)
        continue;
      if (a.id < b.id && a.start < b.stop && b.start < a.stop)
        overlaps.insert(a.id + "\t" + b.id);
      if (a.stop == b.start)
        successors.insert(a.id + "\t" + b.id);
      if (a.start <= b.start && a.stop >= b.stop &&
          (a.start != b.start || a.stop != b.stop))
        containers.insert(a.id);
    }
  for (const auto &[room, matches] : active_minutes) {
    occupied[room] = matches.size();
    for (const auto &[minute, count] : matches)
      peak[room] = std::max(peak[room], count);
  }
  for (const auto &[room, matches] : inclusive_minutes)
    for (const auto &[minute, count] : matches)
      inclusive_peak[room] = std::max(inclusive_peak[room], count);
  std::map<std::string, int> canceled_union;
  for (const auto &[room, minutes] : canceled_minutes)
    canceled_union[room] = minutes.size();

  auto lines = [](const std::set<std::string> &values) {
    return values.empty()
               ? std::string{}
               : join(Strings(values.begin(), values.end()), "\n") + "\n";
  };
  auto ranked = [](const std::map<std::string, int> &values) {
    std::map<std::pair<int, std::string>, int> order;
    for (const auto &[room, count] : values)
      order.emplace(std::pair{-count, room}, count);
    std::string result;
    for (const auto &[key, count] : order)
      result += key.second + "\t" + std::to_string(count) + "\n";
    return result;
  };
  return object({{"H091", lines(overlaps)},
                 {"H093", ids_json(at_sixty)},
                 {"H094", report_rows(occupied)},
                 {"H097", lines(successors)},
                 {"H098", lines(containers)},
                 {"H099", ranked(canceled_sum)},
                 {"H100", report_rows(peak)},
                 {"include-stop", ids_json(include_stop)},
                 {"union-cancellations", ranked(canceled_union)},
                 {"arrivals-first", report_rows(inclusive_peak)}});
}

J mutation(const std::string &id, J steps, const J &recipes) {
  if (id == "H093") {
    check(steps[3]["parameters"]["comparison"] == "gt",
          "changed point-in-time recipe");
    steps[3]["parameters"]["comparison"] = "ge";
    return steps;
  }
  if (id == "H099") {
    auto union_steps = operations(recipes["H094"][0]);
    union_steps[1]["parameters"]["value"] = "canceled";
    union_steps[10]["parameters"]["result_order"] = "value_desc";
    return union_steps;
  }
  check(id == "H100" && steps[2]["outcome"] == "interval_endpoints",
        "changed endpoint recipe");
  auto reordered = array();
  for (unsigned i = 0; i < steps.size(); ++i) {
    reordered.append(steps[i]);
    if (i == 2)
      reordered.append(object(
          {{"outcome", "order_records"},
           {"parameters",
            object({{"order_by", "0:text:asc,1:int:asc,2:int:desc"}})}}));
  }
  return reordered;
}
} // namespace

int main() try {
  const auto previous =
      runs / "v5-unordered-finance-replay-XXvu2pML/report.json";
  const auto previous_pin = hash(previous);
  check(previous_pin ==
            "20ab431f685db586b3fd07fcfb1d1a7e5bc0dba8727a14642aa7340ba5d3b252",
        "changed cohort rank");
  const auto ranking = load(previous);
  auto ranked = array();
  for (const auto &id : requests)
    ranked.append(id);
  check(ranking["family_frequency"][1]["name"] == "room bookings" &&
            ranking["family_frequency"][1]["requests"] == ranked,
        "changed room-booking cohort");
  const auto recipe = root / "shared/request-recipes.json";
  const auto recipe_pin = hash(recipe);
  check(recipe_pin ==
            "768676cbc34ef7db9d869520bd3d26d86123f49a3c9803901eb5653de3a6f49b",
        "changed reference recipes");
  const auto recipes = load(recipe);

  const auto stage = runs / "v5-queue-XXH5XMi9/stage/package";
  const auto writer_stage = runs / "v5-tsv-writer-XXAf0x0y/stage";
  const auto base = stage / "bundle", with_tsv = writer_stage / "bundle";
  const auto base_pin = hash(stage / "manifest.json");
  const auto writer_pin = hash(writer_stage / "manifest.json");
  check(base_pin == "d15aee96917ff440f58f76270e3ca48575268dcfcf2bce951805c865aa"
                    "e3af63" &&
            writer_pin == "0047b47009df08f7ee228729eb46b4438ec9d59eadd7c44647ec"
                          "6761eeabb994",
        "changed fixed catalog stages");
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

  const auto active = fs::canonical(volume / "unllm/boilerplate/active");
  const auto active_pin = hash(active / "SHA256SUMS");
  check(active_pin ==
            "e4564e3079f0b32b3379a411b170ce9af61ec80ab7938b6a974e724abd538328",
        "changed published catalog");
  const auto work = fresh_run("v5-unordered-bookings-replay");
  std::cout << work.string() << std::endl;
  auto report = object({{"active_path", active.string()},
                        {"active_sha256", active_pin},
                        {"frequency_report_sha256", previous_pin},
                        {"recipes_sha256", recipe_pin},
                        {"parent_manifest_sha256", base_pin},
                        {"writer_manifest_sha256", writer_pin},
                        {"writer", writer},
                        {"tools", tools()}});
  report["tools"]["unllm_logic_worker"] =
      hash(main_repo / "build/native/unllm_logic_worker");
  for (const auto *effect : {"match_intervals", "interval_endpoints"})
    report["retained_bindings"][effect] =
        exact_row(base, effect, "rows", "rows");
  report["retained_bindings"]["read_json_records"] =
      exact_row(base, "read_json_records", "in:" + json, "rows");
  const auto contracts = load(base / "providers/contracts.json");
  const auto &bindings = contracts["providers"];

  const auto rows = parse(witness);
  auto reversed = array();
  for (unsigned i = rows.size(); i > 0; --i)
    reversed.append(rows[i - 1]);
  const auto expected = reference(rows);
  check(reference(reversed) == expected &&
            expected["H099"] == "Cedar\t100\nAmber\t13\nBirch\t13\n" &&
            expected["union-cancellations"] ==
                "Cedar\t100\nBirch\t13\nAmber\t11\n" &&
            expected["H100"] == "Amber\t4\nBirch\t2\nDelta\t1\n" &&
            expected["arrivals-first"] == "Amber\t5\nBirch\t3\nDelta\t1\n",
        "booking witness lost a boundary distinction");
  report["witness_sha256"] = sha(witness);
  report["reversed_witness_sha256"] = sha(dump(reversed));
  const std::map<std::string, std::string> alternatives{
      {"H093", "include-stop"},
      {"H099", "union-cancellations"},
      {"H100", "arrivals-first"}};
  const auto historical = runs / "shared-review-5ui86km8/transfer";
  const auto archive = runs / "english-audit-bcsgf8pi/replay-staged";
  std::vector<Path> programs;
  for (const auto &id : requests) {
    const auto dir = archive / id;
    const auto &record = ranking["archive"][id];
    const auto saved_pin = hash(dir / response);
    const auto saved = load(dir / response);
    const auto &role = saved["roles"][0];
    const auto output = role["output_type"].asString();
    const auto mode = id == "H093" ? json : id == "H098" ? plain : tsv;
    const auto steps = operations(recipes[id][0]);
    check(saved_pin == record["reply_sha256"].asString() &&
              hash(record["reply_source"].asString()) == saved_pin &&
              hash(dir / "request.txt") ==
                  record["request_sha256"].asString() &&
              saved["input_type"] == json && saved["roles"].size() == 1 &&
              output == mode && role["requirement"]["operations"] == array() &&
              steps[0]["outcome"] == "read_json_records",
          "changed saved booking context for " + id);
    auto &entry = report["bookings"][id];
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
            "changed booking sink gap for " + id + ": " + dump(gap));
      entry["plans"]["without-writer"] = gap;
    }

    auto inputs = object();
    const std::string input = "bookings.json";
    const auto fixtures = historical / id / "fixtures";
    for (const auto &split : splits)
      inputs[split] = hash(fixtures / split / "input" / input);
    entry["fixture_input_sha256"] = inputs;
    const auto &bundle = output == tsv ? with_tsv : base;
    const auto sink = output == tsv    ? "write_tsv_lines"
                      : output == json ? "write_texts"
                                       : "write_lines";
    auto replay = [&](const std::string &arm, const J &spec, const J &ops) {
      const auto file = work / "specs" / arm / (id + ".json");
      write(file, spec);
      const auto result = trial(work, arm, id, bundle, file, input, inputs);
      const auto exe = work / arm / id / "run/project/converter";
      entry["runs"][arm] = result;
      write(work / "progress.json", report);
      const auto &r = result["roles"]["0"];
      const auto &chain = r["steps"];
      check(fs::is_regular_file(exe) && complete(r) &&
                chain.size() == ops.size() + 1 &&
                chain[ops.size()]["provider"] == sink,
            "incomplete booking construction for " + id + " " + arm);
      for (unsigned i = 0; i < ops.size(); ++i) {
        const auto &step = chain[i];
        const auto wanted = ops[i]["outcome"].asString();
        const auto prefix = "op_" + std::to_string(i) + "_";
        check(std::any_of(bindings.begin(), bindings.end(),
                          [&](const auto &binding) {
                            return binding["effect"] == wanted &&
                                   binding["from"] == step["from"] &&
                                   binding["to"] == step["to"] &&
                                   step["provider"] ==
                                       prefix + binding["id"].asString();
                          }),
              "booking chain omitted " + wanted + " for " + id);
      }
      programs.push_back(exe);
      return exe;
    };

    const auto exe = replay("reference", supplied, steps);
    const auto &result = entry["runs"]["reference"];
    const std::string suffix = output == json ? ".json" : ".txt";
    for (const auto &split : splits)
      check(result["scores"][split] == true &&
                result["outputs"][split][0] ==
                    bytes(fixtures / split / "output" / ("result-1" + suffix)),
            "saved booking fixture mismatch for " + id + " " + split);
    auto test = [&](const std::string &arm, const Path &program,
                    const std::string &data, const J &answer) {
      const auto result = extra_case(work / "witness" / id / arm, program,
                                     bundle, data, array({answer}));
      check(result["passed"] == true, "booking witness mismatch for " + id +
                                          " " + arm + ": " + dump(result));
      entry["witness"][arm] = result;
    };
    test("reference", exe, witness, expected[id]);
    test("reversed", exe, dump(reversed), expected[id]);

    if (const auto it = alternatives.find(id); it != alternatives.end()) {
      const auto &arm = it->second;
      auto altered = supplied;
      const auto mutation_steps = mutation(id, steps, recipes);
      altered["roles"][0]["requirement"]["operations"] = mutation_steps;
      test(arm, replay(arm, altered, mutation_steps), witness, expected[arm]);
      check(expected[arm] != expected[id], "witness did not detect " + arm);
    }
    write(work / "progress.json", report);
  }
  check(hash(active / "SHA256SUMS") == active_pin, "published context changed");
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
