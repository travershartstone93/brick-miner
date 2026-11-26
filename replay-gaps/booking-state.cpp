#include "row-trial.hpp"
#include <iostream>

using namespace miner;
using namespace row_replay;
namespace {
const std::string request = "H096";
const std::string bookings = "bookings.json";
const std::string full_fields = R"(["id", "room", "start", "stop", "state"])";
const std::string compact_fields = R"(["room", "state"])";
const std::string witness = R"([
  {"id":"b1","room":"Zeta","start":10,"stop":15,"state":"canceled"},
  {"id":"b2","room":"Beta","start":10,"stop":15,"state":"active"},
  {"id":"b3","room":"Gamma","start":10,"stop":15,"state":"active"},
  {"id":"b4","room":"Zeta","start":20,"stop":25,"state":"active"},
  {"id":"b5","room":"Omega","start":10,"stop":15,"state":"canceled"},
  {"id":"b6","room":"Zeta","start":30,"stop":35,"state":"canceled"},
  {"id":"b7","room":"Beta","start":20,"stop":25,"state":"canceled"},
  {"id":"b8","room":"Alpha","start":10,"stop":15,"state":"canceled"},
  {"id":"b9","room":"Zeta","start":40,"stop":45,"state":"active"},
  {"id":"b10","room":"Omega","start":20,"stop":25,"state":"canceled"}
])";

J graph_shape(const Path &work, const Path &bundle, const J &named_sets,
              std::vector<Path> &programs) {
  const auto dir = work / "graph-shape";
  const auto spec = spec_for("application/json", "application/json",
                             object({{"outcome", "parallel_layers"},
                                     {"parameters", object()},
                                     {"uncovered", array()}}));
  const auto emitted = plan(bundle, spec, dir);
  const auto &p = emitted["providers"][0];
  auto chain = array();
  for (const auto &s : p["plan"]["steps"])
    chain.append(s["provider"]);
  const auto graph_reader = exact_row(bundle, "read_named_sets",
                                      "in:application/json", "prerequisites");
  check(emitted["providers"].size() == 1 && complete(p) &&
            emitted["clarifications"].empty() &&
            emitted["interpretation_errors"].empty() &&
            chain ==
                array({"read_named_sets", "schedule", "write_nested_lists"}) &&
            graph_reader["call"] == named_sets["call"] &&
            graph_reader["header"] == named_sets["header"] &&
            graph_reader["brick"] == named_sets["brick"] &&
            graph_reader["args"] == named_sets["args"],
        "graph control did not use the same reader callable");
  const auto exe = dir / "schedule";
  build_emission(p, dir, exe, "g++-15");
  programs.push_back(exe);
  const auto graph = extra_case(dir / "graph", exe, bundle,
                                R"({"install":["fetch"],"fetch":[]})",
                                array({R"([["fetch"],["install"]])"}));
  const auto records =
      extra_case(dir / "bookings", exe, bundle, witness, array({J()}), 1);
  check(graph["passed"] == true && records["passed"] == true,
        "graph route did not distinguish the booking-array shape");
  return object({{"chain", chain},
                 {"reader", graph_reader},
                 {"plan_sha256", hash(dir / "components/emission.json")},
                 {"program_sha256", hash(exe)},
                 {"graph", graph},
                 {"bookings", records}});
}

void narrow_projection(J &steps) {
  auto &read = steps[0];
  check(read["outcome"] == "read_json_records" &&
            read["parameters"]["fields"] == full_fields,
        "changed booking reader in fixed recipe");
  read["parameters"]["fields"] = compact_fields;
  for (unsigned i = 1; i < steps.size(); ++i) {
    const auto &op = steps[i]["outcome"];
    auto &p = steps[i]["parameters"];
    if (op == "annotate_group_measure") {
      check(p["keys"] == "1" && p["column"] == 4, "changed booking group keys");
      p["keys"] = "0";
      p["column"] = 1;
    } else if (op == "filter_records") {
      const auto n = p["column"].asInt();
      check(n == 5 || n == 6, "changed booking count columns");
      p["column"] = n - 3;
    } else if (op == "ordered_column") {
      check(p["column"] == 1, "changed room column");
      p["column"] = 0;
    } else {
      check(op == "unique_lines" || op == "sort_lines",
            "changed booking route");
    }
  }
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
        "changed published replay context");
  const auto recipes_file = root / "shared/request-recipes.json";
  const auto recipes_pin = hash(recipes_file);
  check(recipes_pin ==
            "768676cbc34ef7db9d869520bd3d26d86123f49a3c9803901eb5653de3a6f49b",
        "changed fixed recipes");
  const auto recipes = load(recipes_file)[request];
  const auto old = runs / "english-audit-bcsgf8pi/replay-staged" / request;
  const auto original =
      volume / "unllm/experiments/boilerplate-B42-VrtYbHSf/fresh-staged" /
      request;
  const auto response = Path("run/calls/interpret/response.json");
  const auto saved = old / response;
  const auto supplied_pin = hash(saved);
  check(supplied_pin == "99be2116f3a185c3c188996bc343affe59e2d5541ddf3037fbe341"
                        "8166c0534b" &&
            hash(original / response) == supplied_pin,
        "changed archived interpretation");
  const auto work = fresh_run("v5-booking-state-replay");
  std::cout << work.string() << std::endl;

  const auto reader =
      exact_row(bundle, "read_json_records", "in:application/json", "rows");
  const auto named_sets =
      exact_row(bundle, "named_sets", "in:application/json", "prerequisites");
  const auto aggregate =
      exact_row(bundle, "annotate_group_measure", "rows", "rows");
  const auto inputs = input_hashes(old, bookings);
  auto report = object({{"request", request},
                        {"active", active.string()},
                        {"active_manifest_sha256", active_pin},
                        {"parent_manifest_sha256", parent_pin},
                        {"recipes_sha256", recipes_pin},
                        {"supplied_sha256", supplied_pin},
                        {"tools", tools()},
                        {"input_sha256", inputs},
                        {"reader", reader},
                        {"aggregate", aggregate},
                        {"named_sets", named_sets}});
  for (const auto &[label, dir] : std::vector<std::pair<std::string, Path>>{
           {"original", original}, {"replay", old}}) {
    const auto menu = dir / "run/vocabulary.json";
    const auto outcomes = load(menu)["outcomes"];
    auto offered = object();
    for (const auto *id :
         {"named_sets", "read_json_records", "annotate_group_measure",
          "aggregate_groups", "filter_records", "ordered_column",
          "unique_lines", "sort_lines"})
      offered[id] = outcomes.isMember(id);
    report["menus"][label] =
        object({{"sha256", hash(menu)}, {"offered", offered}});
  }

  std::vector<Path> programs;
  auto converter = [&](const std::string &arm) {
    return work / arm / request / "run/project/converter";
  };
  auto replay = [&](const std::string &arm, const Path &catalog,
                    const Path &spec) {
    auto result = trial(work, arm, request, catalog, spec, bookings, inputs);
    if (!result["program_sha256"].isNull())
      programs.push_back(converter(arm));
    report["arms"][arm] = result;
    write(work / "progress.json", report);
    return result;
  };
  auto variant = [&](const std::string &arm, const Path &catalog,
                     const J &spec) {
    const auto path = work / "specs" / (arm + ".json");
    write(path, spec);
    return replay(arm, catalog, path);
  };
  auto fixed = load(saved), reader_only = fixed, compact = fixed;
  check(fixed["input_type"] == "application/json" &&
            fixed["roles"].size() == 2 && recipes.size() == 2,
        "changed archived booking task");
  for (unsigned i = 0; i < 2; ++i) {
    auto &role = fixed["roles"][i];
    auto &r = role["requirement"];
    check(role["output_type"] == "text/plain" && r["operations"].size() == 1 &&
              r["operations"][0]["outcome"] == "named_sets",
          "changed booking role");
    r["operations"] = operations(recipes[i]);
    r["uncovered"] = array();
    reader_only["roles"][i]["requirement"]["operations"] =
        array({r["operations"][0]});
    auto &smaller = compact["roles"][i]["requirement"];
    smaller = r;
    narrow_projection(smaller["operations"]);
  }
  const auto archived = replay("archived", bundle, saved);
  const auto projected = variant("reader-only", bundle, reader_only);
  const auto accepted = variant("fixed", bundle, fixed);
  const auto narrow = variant("compact", bundle, compact);
  check(archived["scores"] == scores(load(old / "receipt.json")) &&
            archived["verified"] == false && projected["verified"] == false &&
            accepted["verified"] == true && narrow["verified"] == true &&
            narrow["outputs"] == accepted["outputs"],
        "booking comparison did not reproduce fixture verdicts");
  for (const auto *slot : {"0", "1"}) {
    check(ports(archived["roles"][slot]) ==
                  array({array({"prerequisites", "out:text/plain"})}) &&
              selects(archived["roles"][slot], "named_sets") &&
              ports(projected["roles"][slot]) ==
                  array({array({"rows", "out:text/plain"})}) &&
              selects(projected["roles"][slot], "read_json_records"),
          "booking intervention did not isolate the typed gaps");
    for (const auto &result : {accepted, narrow}) {
      const auto &role = result["roles"][slot];
      check(complete(role) && selects(role, "read_json_records") &&
                selects(role, "annotate_group_measure") &&
                selects(role, "write_lines"),
            "complete booking recipe selected an unexpected binding chain");
    }
  }

  for (const auto &[arm, binding] : std::vector<std::pair<std::string, J>>{
           {"reader-removed", reader}, {"aggregate-removed", aggregate}}) {
    const auto cut = work / (arm + "-bundle");
    write_tree(cut, remove_binding(read_tree(bundle), binding));
    const auto result = variant(arm, cut, fixed);
    fs::remove_all(cut);
    check(result["verified"] == false && !complete(result["roles"]["0"]) &&
              !complete(result["roles"]["1"]),
          "removing an exact booking binding left a complete report");
    for (const auto &outputs : result["outputs"])
      check(outputs == array({J(), J()}),
            "an incomplete booking converter wrote a destination");
  }

  auto exact_one = fixed;
  auto &count_test = exact_one["roles"][1]["requirement"]["operations"][4];
  check(count_test["outcome"] == "filter_records" &&
            count_test["parameters"] == object({{"column", 6},
                                                {"type", "int"},
                                                {"comparison", "gt"},
                                                {"value", "0"}}),
        "changed mixed-report cancellation predicate");
  count_test["parameters"]["comparison"] = "eq";
  count_test["parameters"]["value"] = "1";
  const auto mutation = variant("one-cancellation", bundle, exact_one);
  check(mutation["verified"] == true &&
            mutation["outputs"] == accepted["outputs"],
        "archived fixtures changed their coverage of mixed-room multiplicity");

  const auto correct = array({"Alpha\nOmega\n", "Beta\nZeta\n"});
  for (const auto *arm : {"fixed", "compact", "one-cancellation"}) {
    const auto expected = std::string(arm) == "one-cancellation"
                              ? array({"Alpha\nOmega\n", "Beta\n"})
                              : correct;
    auto result = extra_case(work / arm / request / "booking-witness",
                             converter(arm), bundle, witness, expected);
    check(result["passed"] == true,
          "fresh booking witness differed for " + std::string(arm));
    report["booking_witness"][arm] = result;
  }
  const std::string partial = R"([{"room":"North","state":"canceled"}])";
  auto full_reject = extra_case(work / "fixed/domain", converter("fixed"),
                                bundle, partial, array({J(), J()}), 1);
  auto compact_accept =
      extra_case(work / "compact/domain", converter("compact"), bundle, partial,
                 array({"North\n", ""}));
  check(full_reject["passed"] == true && compact_accept["passed"] == true,
        "booking projection domain probe differed");
  report["projection_domain"] =
      object({{"fixed", full_reject}, {"compact", compact_accept}});
  report["graph_shape"] = graph_shape(work, bundle, named_sets, programs);
  check(tools() == report["tools"] && hash(active / "SHA256SUMS") == active_pin,
        "booking replay inputs changed");
  write(work / ".report.json", report);
  run({"sync", "-f", work / ".report.json"});
  for (const auto &exe : programs)
    fs::remove(exe);
  fs::rename(work / ".report.json", work / "report.json");
  std::cout << (work / "report.json").string() << '\n';
  return 0;
} catch (const std::exception &e) {
  std::cerr << e.what() << '\n';
  return 1;
}
