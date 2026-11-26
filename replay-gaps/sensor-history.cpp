#include "row-trial.hpp"
#include <iostream>

using namespace miner;
using namespace row_replay;
namespace {
const std::string request = "H037";
const std::string selector = R"(["sensor", "room", "minute", "temp"])";
const std::string telemetry = R"([
  {"sensor":"zero","room":"Lab","minute":0,"temp":0},
  {"sensor":"mix","room":"Lab","minute":1,"temp":-2},
  {"sensor":"both","room":"Lab","minute":2,"temp":-3},
  {"sensor":"cold","room":"Lab","minute":3,"temp":-5},
  {"sensor":"pos","room":"Lab","minute":4,"temp":9},
  {"sensor":"mix","room":"Outside","minute":1440,"temp":3},
  {"sensor":"both","room":"Outside","minute":1441,"temp":0},
  {"sensor":"cold","room":"Lab","minute":3,"temp":-5}
])";

J verified_reader(const Path &bundle, const Path &stage) {
  const auto pin = hash(stage / "manifest.json");
  check(pin ==
            "c97ffce350f2eb6f582702d1dd6a9b803463ed0d35ae8a4668bd7d01d195b7cb",
        "changed detached telemetry package");
  const auto manifest = load(stage / "manifest.json");
  for (const auto *name : {"source.json", "domain.json", "contract.json"})
    check(hash(stage / name) == manifest["files"][name].asString(),
          "changed telemetry evidence file");
  const auto source = load(stage / "source.json");
  const auto domain = load(stage / "domain.json");
  auto reader =
      exact_row(bundle, "read_json_records", "in:application/json", "rows");
  check(
      source["binding"] == reader && domain["call"] == reader["call"] &&
          domain["argument_index"] == 1 &&
          domain["all_of"] ==
              array({object({{"id", "exact_text"}, {"value", selector}})}) &&
          domain["header_sha256"] ==
              hash(bundle / "headers" / reader["header"].asString()) &&
          domain["implementation_sha256"] ==
              hash(bundle / "runtime/duckdb/lib/libbrick_shared_readers.so") &&
          domain["contract_sha256"] == hash(stage / "contract.json"),
      "retained reader or selector differs from qualified evidence");
  return object({{"manifest_sha256", pin},
                 {"domain_sha256", hash(stage / "domain.json")},
                 {"binding", reader}});
}

J named_lists_control(const Path &work, const Path &bundle,
                      std::vector<Path> &programs) {
  auto dir = work / "named-lists-json";
  auto spec = spec_for("application/json", "application/json",
                       object({{"outcome", "named_lists"},
                               {"parameters", object()},
                               {"uncovered", array()}}));
  auto emitted = plan(bundle, spec, dir);
  const auto &p = emitted["providers"][0];
  auto chain = array();
  for (const auto &s : p["plan"]["steps"])
    chain.append(s["provider"]);
  check(emitted["providers"].size() == 1 && complete(p) &&
            emitted["interpretation_errors"].empty() &&
            emitted["clarifications"].empty() &&
            chain == array({"named_lists", "write_named_lists"}),
        "existing JSON destination failed for the named-list control");
  auto exe = dir / "named-list";
  build_emission(p, dir, exe, "g++-15");
  programs.push_back(exe);
  const std::string named = R"({"cold":["-5"],"mix":["-2","3"],"zero":["0"]})";
  auto legal = extra_case(dir / "object", exe, bundle, named, array({named}));
  auto array_input = extra_case(dir / "telemetry-array", exe, bundle, telemetry,
                                array({J()}), 1);
  check(legal["passed"] == true && array_input["passed"] == true,
        "named-list control did not distinguish input shape");
  return object({{"chain", chain},
                 {"plan_sha256", hash(dir / "components/emission.json")},
                 {"program_sha256", hash(exe)},
                 {"named_object", legal},
                 {"telemetry_array", array_input}});
}
} // namespace

int main() try {
  const auto parent = runs / "v5-queue-XXH5XMi9/stage/package";
  const auto parent_pin = hash(parent / "manifest.json");
  check(parent_pin ==
            "d15aee96917ff440f58f76270e3ca48575268dcfcf2bce951805c865aae3af63",
        "changed parent");
  const auto bundle = parent / "bundle";
  verify_bundle(
      bundle,
      "436db92629463daee559de2cbbc1c81519e12306d5bf01cc6d2883f3140e779c");
  const auto active = fs::canonical(volume / "unllm/boilerplate/active");
  const auto active_pin = hash(active / "SHA256SUMS");
  check(active_pin ==
            "e4564e3079f0b32b3379a411b170ce9af61ec80ab7938b6a974e724abd538328",
        "changed published replay context");
  const auto reader =
      verified_reader(bundle, runs / "v5-json-records-XXptaI1o/stage");
  const auto recipes_file = root / "shared/request-recipes.json";
  const auto recipe_pin = hash(recipes_file);
  check(recipe_pin ==
            "768676cbc34ef7db9d869520bd3d26d86123f49a3c9803901eb5653de3a6f49b",
        "changed fixed recipes");
  const auto recipes = load(recipes_file)[request];
  const auto old = runs / "english-audit-bcsgf8pi/replay-staged" / request;
  const auto original =
      volume / "unllm/experiments/boilerplate-B42-VrtYbHSf/fresh-staged" /
      request;
  const auto response = Path("run/calls/interpret/response.json");
  const auto saved = old / response;
  const auto saved_pin = hash(saved);
  check(saved_pin == "75d7a4b998cc58cbfee82d632477702ac6a9bba962dea4b3be56e103e"
                     "871aef2" &&
            hash(original / response) == saved_pin,
        "changed supplied interpretation lineage");
  const auto work = fresh_run("v5-sensor-history-replay");
  std::cout << work.string() << std::endl;
  const auto inputs = input_hashes(old, "readings.json");
  auto report = object({{"request", request},
                        {"active_manifest_sha256", active_pin},
                        {"parent_manifest_sha256", parent_pin},
                        {"recipes_sha256", recipe_pin},
                        {"qualified_reader", reader},
                        {"tools", tools()},
                        {"supplied_sha256", saved_pin},
                        {"input_sha256", inputs}});
  for (const auto &[label, dir] : std::vector<std::pair<std::string, Path>>{
           {"original", original}, {"replay", old}}) {
    const auto menu = dir / "run/vocabulary.json";
    const auto outcomes = load(menu)["outcomes"];
    auto omitted = array();
    for (const auto *operation :
         {"read_json_records", "filter_records", "annotate_group_measure",
          "ordered_column", "unique_lines", "sort_lines"})
      if (!outcomes.isMember(operation))
        omitted.append(operation);
    report["menus"][label] =
        object({{"sha256", hash(menu)},
                {"fixed_operations_absent", omitted},
                {"named_lists", outcomes.isMember("named_lists")}});
  }
  std::vector<Path> programs;
  auto run_arm = [&](const std::string &arm, const Path &catalog,
                     const Path &spec) {
    auto result =
        trial(work, arm, request, catalog, spec, "readings.json", inputs);
    if (!result["program_sha256"].isNull())
      programs.push_back(work / arm / request / "run/project/converter");
    report["arms"][arm] = result;
    write(work / "progress.json", report);
    return result;
  };
  auto variant = [&](const std::string &arm, const Path &catalog,
                     const J &spec) {
    auto path = work / "specs" / (arm + ".json");
    write(path, spec);
    return run_arm(arm, catalog, path);
  };
  auto fixed = load(saved), reader_only = fixed;
  check(fixed["input_type"] == "application/json" &&
            fixed["roles"].size() == 2 && recipes.size() == 2,
        "sensor report shape changed");
  for (unsigned i = 0; i < fixed["roles"].size(); ++i) {
    auto &r = fixed["roles"][i]["requirement"];
    check(fixed["roles"][i]["output_type"] == "text/plain" &&
              r["operations"].size() == 1 &&
              r["operations"][0]["outcome"] == "named_lists",
          "archived sensor route changed");
    r["operations"] = operations(recipes[i]);
    r["uncovered"] = array();
    const auto &read = r["operations"][0];
    check(read["outcome"] == "read_json_records" &&
              read["parameters"] == object({{"fields", selector}}),
          "sensor recipe changed its telemetry projection");
    reader_only["roles"][i]["requirement"]["operations"] = array({read});
  }
  const auto archived = run_arm("archived", bundle, saved);
  const auto projected = variant("reader-only", bundle, reader_only);
  const auto accepted = variant("fixed", bundle, fixed);
  check(archived["scores"] == scores(load(old / "receipt.json")) &&
            archived["verified"] == false && projected["verified"] == false &&
            accepted["verified"] == true,
        "sensor comparison did not reproduce the expected fixture verdicts");
  for (const auto *slot : {"0", "1"}) {
    check(ports(archived["roles"][slot]) ==
                  array({array({"named_lists", "out:text/plain"})}) &&
              selects(archived["roles"][slot], "named_lists") &&
              ports(projected["roles"][slot]) ==
                  array({array({"rows", "out:text/plain"})}) &&
              selects(projected["roles"][slot], "read_json_records") &&
              complete(accepted["roles"][slot]) &&
              selects(accepted["roles"][slot], "read_json_records") &&
              selects(accepted["roles"][slot], "write_lines"),
          "sensor interventions did not isolate the input/output boundaries");
  }
  check(selects(accepted["roles"]["1"], "annotate_group_measure"),
        "never-positive route skipped the group aggregate");

  auto removed = [&](const std::string &arm, const J &binding) {
    auto cut = work / (arm + "-bundle");
    write_tree(cut, remove_binding(read_tree(bundle), binding));
    report["removed_bindings"][arm] = binding;
    auto result = variant(arm, cut, fixed);
    fs::remove_all(cut);
    return result;
  };
  const auto no_reader = removed("reader-removed", reader["binding"]);
  check(no_reader["verified"] == false && !complete(no_reader["roles"]["0"]) &&
            !complete(no_reader["roles"]["1"]) &&
            dump(no_reader["planner_diagnostics"]).find("read_json_records") !=
                std::string::npos,
        "reader removal did not block both reports");
  const auto no_aggregate =
      removed("aggregate-removed",
              exact_row(bundle, "annotate_group_measure", "rows", "rows"));
  check(no_aggregate["verified"] == false &&
            complete(no_aggregate["roles"]["0"]) &&
            !complete(no_aggregate["roles"]["1"]),
        "aggregate removal did not isolate the never-positive report");
  for (const auto &split : accepted["outputs"].getMemberNames())
    check(no_aggregate["outputs"][split][0] == accepted["outputs"][split][0] &&
              no_aggregate["outputs"][split][1].isNull(),
          "aggregate removal changed a surviving first output");

  auto witness = [&](const std::string &arm, const J &expected) {
    const auto dir = work / arm / request;
    return extra_case(dir / "sensor-witness", dir / "run/project/converter",
                      bundle, telemetry, expected);
  };
  const std::string ever_below = "both\ncold\nmix\n";
  const std::string never_above = "both\ncold\nzero\n";
  auto correct = witness("fixed", array({ever_below, never_above}));
  check(correct["passed"] == true,
        "fixed sensor predicates missed the fresh witness");
  report["sensor_witness"]["fixed"] = correct;
  auto test_mutation = [&](const std::string &arm, const J &spec,
                           const J &expected, bool archived_pass) {
    auto mutant = variant(arm, bundle, spec);
    check(complete(mutant["roles"]["0"]) && complete(mutant["roles"]["1"]) &&
              mutant["verified"] == archived_pass,
          "mutant did not construct or changed the frozen fixture result");
    const auto stop = witness(arm, expected);
    check(stop["passed"] == true && stop["actual"] != correct["actual"],
          "mutation did not produce the expected wrong sensor list");
    report["sensor_witness"][arm] = stop;
  };
  auto inclusive_below = fixed;
  inclusive_below["roles"][0]["requirement"]["operations"][1]["parameters"]
                 ["comparison"] = "le";
  test_mutation("inclusive-below", inclusive_below,
                array({"both\ncold\nmix\nzero\n", never_above}), true);
  auto strict_above = fixed;
  strict_above["roles"][1]["requirement"]["operations"][2]["parameters"]
              ["comparison"] = "lt";
  test_mutation("strict-maximum", strict_above, array({ever_below, "cold\n"}),
                false);
  auto per_reading = fixed;
  auto &ops = per_reading["roles"][1]["requirement"]["operations"];
  J discard;
  ops.removeIndex(1, &discard);
  check(discard["outcome"] == "annotate_group_measure",
        "unexpected group measure in the never-positive recipe");
  ops[1]["parameters"]["column"] = 3;
  test_mutation("per-reading", per_reading,
                array({ever_below, "both\ncold\nmix\nzero\n"}), true);
  auto duplicated = fixed;
  duplicated["roles"][1]["requirement"]["operations"] =
      fixed["roles"][0]["requirement"]["operations"];
  test_mutation("same-predicate", duplicated, array({ever_below, ever_below}),
                true);

  report["named_lists_json_control"] =
      named_lists_control(work, bundle, programs);
  check(tools() == report["tools"] && hash(active / "SHA256SUMS") == active_pin,
        "sensor replay inputs changed");
  write(work / ".report.json", report);
  run({"sync", "-f", work / ".report.json"});
  for (const auto &exe : programs)
    fs::remove(exe);
  fs::rename(work / ".report.json", work / "report.json");
  std::cout << (work / "report.json").string() << std::endl;
  return 0;
} catch (const std::exception &e) {
  std::cerr << e.what() << '\n';
  return 1;
}
