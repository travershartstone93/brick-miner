#include "row-trial.hpp"
#include <iostream>

using namespace miner;
using namespace row_replay;
namespace {
const std::string tsv = "text/tab-separated-values";

J fixed_spec(J saved, const J &recipe) {
  check(saved["input_type"] == tsv && saved["roles"].size() == 1 &&
            recipe.size() == 1 && recipe[0].size() == 1 &&
            recipe[0][0][0] == "reachable_nodes",
        "unexpected reachability recipe or input format");
  auto &req = saved["roles"][0]["requirement"];
  req["operations"] = operations(recipe[0]);
  req["uncovered"] = array();
  return saved;
}

J schedule_control(const Path &work, const Path &bundle,
                   std::vector<Path> &programs) {
  const auto dir = work / "json-schedule";
  auto spec = spec_for("application/json", "application/json",
                       object({{"outcome", "parallel_layers"},
                               {"parameters", object()},
                               {"uncovered", array()}}));
  auto emission = plan(bundle, spec, dir);
  check(emission["providers"].size() == 1 &&
            emission["interpretation_errors"].empty() &&
            emission["clarifications"].empty() &&
            complete(emission["providers"][0]),
        "declared JSON graph did not reach the schedule provider");
  const auto &provider = emission["providers"][0];
  auto steps = array();
  for (const auto &s : provider["plan"]["steps"])
    steps.append(s["provider"]);
  check(steps == array({"read_named_sets", "schedule", "write_nested_lists"}),
        "unexpected JSON graph control chain");
  auto exe = dir / "schedule";
  build_emission(provider, dir, exe, "g++-15");
  programs.push_back(exe);
  auto graph = object({{"a", array({"core"})},
                       {"b", array({"a", "z"})},
                       {"core", array({"up"})},
                       {"island", array()},
                       {"leaf", array({"island"})},
                       {"up", array()},
                       {"z", array({"core"})}});
  auto dag = extra_case(
      dir / "acyclic", exe, bundle, dump(graph),
      array({R"([["island","up"],["core","leaf"],["a","z"],["b"]])"}));
  graph["core"] = array({"b", "core", "up"});
  auto cyclic =
      extra_case(dir / "cyclic", exe, bundle, dump(graph), array({J()}), 1);
  check(dag["passed"] == true && cyclic["passed"] == true,
        "JSON schedule witnesses diverged");
  return object({{"chain", steps},
                 {"plan_sha256", hash(dir / "components/emission.json")},
                 {"program_sha256", hash(exe)},
                 {"acyclic", dag},
                 {"cyclic", cyclic}});
}
} // namespace

int main() try {
  const auto parent = runs / "v5-queue-XXH5XMi9/stage/package";
  const auto parent_pin = hash(parent / "manifest.json");
  check(parent_pin ==
            "d15aee96917ff440f58f76270e3ca48575268dcfcf2bce951805c865aae3af63",
        "changed V5 parent");
  const auto bundle = parent / "bundle";
  verify_bundle(
      bundle,
      "436db92629463daee559de2cbbc1c81519e12306d5bf01cc6d2883f3140e779c");
  const auto active = fs::canonical(volume / "unllm/boilerplate/active");
  const auto active_pin = hash(active / "SHA256SUMS");
  check(active_pin ==
            "e4564e3079f0b32b3379a411b170ce9af61ec80ab7938b6a974e724abd538328",
        "changed active manifest");
  const auto recipes_file = root / "shared/request-recipes.json";
  const auto recipes_pin = hash(recipes_file);
  check(recipes_pin ==
            "768676cbc34ef7db9d869520bd3d26d86123f49a3c9803901eb5653de3a6f49b",
        "changed typed reference recipes");
  const auto recipes = load(recipes_file);
  const auto archive = runs / "english-audit-bcsgf8pi/replay-staged";
  const auto original =
      volume / "unllm/experiments/boilerplate-B42-VrtYbHSf/fresh-staged";
  const auto work = fresh_run("v5-reachability-replay");
  std::cout << work.string() << std::endl;
  auto report = object({{"parent_manifest_sha256", parent_pin},
                        {"active", active.string()},
                        {"active_manifest_sha256", active_pin},
                        {"recipes_sha256", recipes_pin},
                        {"tools", tools()}});
  std::vector<Path> programs;
  for (const std::string request : {"H085", "H086"}) {
    const bool downstream = request == "H085";
    const auto old = archive / request;
    const auto relative = Path("run/calls/interpret/response.json");
    const auto supplied = old / relative;
    const auto old_pin =
        downstream
            ? "72ae55e1eabec775b5b4d9dea5d7e1562628ca47db966ce13a1dc897cb138305"
            : "3af7c7cd89daf95964f75e500376783c74400a4a3ecd88f4beea2280f0fb7d0"
              "6";
    check(hash(supplied) == old_pin &&
              hash(original / request / relative) == old_pin,
          "changed request lineage");
    const auto saved = load(supplied),
               fixed = fixed_spec(saved, recipes[request]);
    const auto &param =
        fixed["roles"][0]["requirement"]["operations"][0]["parameters"];
    check(param == object({{"from_column", downstream ? 0 : 1},
                           {"to_column", downstream ? 1 : 0},
                           {"seed", downstream ? "core" : "app"}}) &&
              saved["roles"][0]["output_type"] ==
                  (downstream ? "application/json" : "text/plain"),
          "changed graph orientation or destination");
    const auto fixture_hashes = input_hashes(old, "build.tsv");
    auto &entry = report["requests"][request];
    entry["supplied_sha256"] = old_pin;
    entry["archived_scores"] = scores(load(old / "receipt.json"));
    entry["inputs"] = fixture_hashes;
    for (const auto &[label, folder] :
         std::vector<std::pair<std::string, Path>>{
             {"original", original / request / "run"},
             {"replay", old / "run"}}) {
      auto menu = folder / "vocabulary.json";
      const auto outcomes = load(menu)["outcomes"];
      entry["menus"][label] =
          object({{"sha256", hash(menu)},
                  {"reachable_nodes", outcomes.isMember("reachable_nodes")},
                  {"parallel_layers", outcomes.isMember("parallel_layers")}});
    }
    auto run_arm = [&](const std::string &arm, const Path &catalog,
                       const Path &spec) {
      auto result =
          trial(work, arm, request, catalog, spec, "build.tsv", fixture_hashes);
      if (!result["program_sha256"].isNull())
        programs.push_back(work / arm / request / "run/project/converter");
      entry["arms"][arm] = result;
      write(work / "progress.json", report);
      return result;
    };
    auto variant = [&](const std::string &arm, const Path &catalog,
                       const J &spec) {
      auto path = work / "specs" / request / (arm + ".json");
      write(path, spec);
      return run_arm(arm, catalog, path);
    };
    auto removed = [&](const std::string &arm, const J &binding) {
      auto cut = work / (arm + "-bundle");
      write_tree(cut, remove_binding(read_tree(bundle), binding));
      entry["removed_bindings"][arm] = binding;
      auto result = variant(arm, cut, fixed);
      fs::remove_all(cut);
      return result;
    };
    if (downstream) {
      const auto archived = run_arm("archived", bundle, supplied);
      const auto &role = archived["roles"]["0"];
      check(archived["scores"] == entry["archived_scores"] &&
                archived["verified"] == false &&
                ports(role) == array({array({"in:" + tsv, "prerequisites"})}) &&
                selects(role, "schedule") &&
                selects(role, "write_nested_lists"),
            "ranked H085 boundary did not reproduce");
    }
    const auto accepted = variant("fixed", bundle, fixed);
    const auto &role = accepted["roles"]["0"];
    check(accepted["verified"] == true && complete(role) &&
              selects(role, "tab_records") &&
              selects(role, "reachable_nodes") &&
              selects(role, downstream ? "write_texts" : "write_lines"),
          "fixed reachability route did not deliver the fixture set");
    auto witness = [&](const std::string &arm, const std::string &name,
                       const std::string &input, const std::string &expected) {
      auto folder = work / arm / request;
      return extra_case(folder / name, folder / "run/project/converter", bundle,
                        input, array({expected}));
    };
    if (!downstream) {
      auto incoming =
          witness("fixed", "cyclic-prerequisites",
                  "a\tapp\napp\tcore\ncore\ta\nb\tapp\ntail\tb\napp\tchild\n",
                  "a\nb\ncore\ntail\n");
      check(incoming["passed"] == true, "reverse reachability transfer failed");
      entry["cyclic_prerequisites"] = incoming;
      continue;
    }
    auto flipped = fixed;
    auto &change =
        flipped["roles"][0]["requirement"]["operations"][0]["parameters"];
    change["from_column"] = 1;
    change["to_column"] = 0;
    auto reversal = variant("reversed-columns", bundle, flipped);
    check(complete(reversal["roles"]["0"]) && reversal["verified"] == false,
          "wrong direction was not distinguished");
    auto reader_cut = removed("reader-removed", exact_row(bundle, "tab_records",
                                                          "in:" + tsv, "rows"));
    check(reader_cut["verified"] == false &&
              ports(reader_cut["roles"]["0"]) ==
                  array({array({"in:" + tsv, "rows"})}),
          "exact row reader removal did not isolate the input port");
    auto reachability_cut =
        removed("reachability-removed",
                exact_row(bundle, "reachable_nodes", "rows", "strings"));
    check(reachability_cut["verified"] == false &&
              !complete(reachability_cut["roles"]["0"]) &&
              dump(reachability_cut["planner_diagnostics"])
                      .find("reachable_nodes") != std::string::npos,
          "exact reachability removal did not isolate the missing operation");
    const std::string dag =
        "z\tb\ncore\tz\nisland\tleaf\nup\tcore\ncore\ta\na\tb\ncore\ta\n";
    auto normal =
        witness("fixed", "acyclic-descendants", dag, R"(["a","b","z"])");
    auto cyclic = witness("fixed", "cyclic-descendants",
                          dag + "b\tcore\ncore\tcore\n", R"(["a","b","z"])");
    auto wrong =
        witness("reversed-columns", "acyclic-ancestors", dag, R"(["up"])");
    auto absent = witness("fixed", "absent-seed", "island\tleaf\n", "[]");
    check(normal["passed"] == true && cyclic["passed"] == true &&
              wrong["passed"] == true && absent["passed"] == true,
          "fresh reachability witnesses diverged");
    entry["witnesses"] = object({{"downstream_dag", normal},
                                 {"downstream_cycle", cyclic},
                                 {"reversed_dag", wrong},
                                 {"absent_seed", absent}});
  }
  report["existing_json_schedule"] = schedule_control(work, bundle, programs);
  check(tools() == report["tools"] && hash(active / "SHA256SUMS") == active_pin,
        "reachability replay inputs changed");
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
