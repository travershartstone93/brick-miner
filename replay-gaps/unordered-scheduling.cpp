#include "row-trial.hpp"
#include <algorithm>
#include <iostream>
#include <set>
#include <sstream>

using namespace miner;
using namespace row_replay;
namespace {
using Edge = std::pair<std::string, std::string>;
using Edges = std::set<Edge>;
const std::string tsv = "text/tab-separated-values";
const std::string plain = "text/plain";
const std::string empty_stop = "Expected between one and 32 ordered outcomes";
const Path response = "run/calls/interpret/response.json";
const Strings splits{"ordinary", "boundary", "empty", "transfer", "sparse"};
const Strings requests{"H081", "H082", "H086", "H087", "H089"};
const std::vector<Edge> witness{
    {"root", "m2"},     {"z", "end"},       {"onlySelf", "onlySelf"},
    {"root", "m1"},     {"m1", "target"},   {"m2", "target"},
    {"root", "target"}, {"root", "target"}, {"m1", "loose"},
    {"root", "long"},   {"m1", "hop"},      {"hop", "long"},
    {"selfA", "selfA"}, {"selfA", "plain"}, {"c1", "c2"},
    {"c2", "c3"},       {"c3", "c1"},       {"c3", "child"},
    {"up", "c1"},       {"a", "app"},       {"app", "b"},
    {"b", "a"},         {"tail", "a"},      {"core", "app"},
    {"app", "child"}};

template <class Iterable> std::string tsv_bytes(const Iterable &edges) {
  std::string result;
  for (const auto &[from, to] : edges)
    result += from + "\t" + to + "\n";
  return result;
}

Edges read_edges(const std::string &data) {
  Edges edges;
  std::istringstream rows(data);
  for (std::string line; std::getline(rows, line);) {
    if (line.ends_with('\r'))
      line.pop_back();
    if (line.empty())
      continue;
    const auto tab = line.find('\t');
    check(tab != std::string::npos &&
              line.find('\t', tab + 1) == std::string::npos,
          "expected two dependency fields");
    edges.emplace(line.substr(0, tab), line.substr(tab + 1));
  }
  return edges;
}

std::string lines(const std::set<std::string> &names) {
  return names.empty() ? std::string{}
                       : join(Strings(names.begin(), names.end()), "\n") + "\n";
}

std::set<std::string> reachable(const Edges &edges, const std::string &node,
                                bool backwards = false) {
  std::set<std::string> found;
  Strings todo{node};
  for (size_t i = 0; i < todo.size(); ++i) {
    const auto current = todo[i];
    for (const auto &[from, to] : edges) {
      const auto &origin = backwards ? to : from;
      const auto &next = backwards ? from : to;
      if (origin == current && found.insert(next).second)
        todo.push_back(next);
    }
  }
  return found;
}

std::string oracle(const std::string &id, const Edges &edges,
                   std::string_view arm = "reference") {
  if (id == "H086") {
    auto result = reachable(edges, "app", true);
    result.erase("app");
    return lines(result);
  }
  std::set<std::string> sources, targets, result;
  for (const auto &[from, to] : edges) {
    sources.insert(from);
    targets.insert(to);
  }
  if (id == "H081") {
    for (const auto &name : sources)
      if (!targets.contains(name))
        result.insert(name);
    return lines(result);
  }
  if (id == "H082") {
    for (const auto &name : targets)
      if (!sources.contains(name))
        result.insert(name);
    return lines(result);
  }
  if (id == "H087") {
    for (const auto &name : sources)
      if (arm == "self-only" ? edges.contains({name, name})
                             : reachable(edges, name).contains(name))
        result.insert(name);
    return lines(result);
  }
  check(id == "H089", "unknown scheduling oracle");
  Edges pairs;
  for (const auto &[from, via] : edges) {
    std::set<std::string> ends;
    if (arm == "transitive")
      ends = reachable(edges, via);
    else
      for (const auto &[origin, to] : edges)
        if (origin == via)
          ends.insert(to);
    for (const auto &to : ends) {
      if (arm != "repeated-modules" && (from == via || via == to || from == to))
        continue;
      if (arm == "without-direct" || edges.contains({from, to}))
        pairs.emplace(from, to);
    }
  }
  return tsv_bytes(pairs);
}

J variant(J steps, const std::string &arm) {
  check(steps[0]["outcome"] == "graph_witnesses", "changed graph recipe");
  if (arm == "self-only" || arm == "transitive") {
    steps[0]["parameters"]["length"] = arm == "self-only" ? "1" : "closure";
    return steps;
  }
  const auto direct = steps[1]["parameters"];
  check(steps[1]["outcome"] == "filter_records" && direct["column"] == 3 &&
            direct["comparison"] == "eq" && direct["value"] == "1",
        "changed direct-edge predicate");
  auto result = array({steps[0]});
  if (arm == "without-direct") {
    for (unsigned i = 2; i < steps.size(); ++i)
      result.append(steps[i]);
  } else {
    check(arm == "repeated-modules" &&
              steps[4]["outcome"] == "filter_records" &&
              steps[5]["outcome"] == "order_records",
          "changed distinct-module predicates");
    result.append(steps[1]);
    for (unsigned i = 5; i < steps.size(); ++i)
      result.append(steps[i]);
  }
  return result;
}

J reuse_h086(const Path &older, const J &prior, const Path &bundle,
             const Path &fixtures, const J &steps, const J &record) {
  const auto &proof = prior["requests"]["H086"];
  const auto &arm = proof["arms"]["fixed"];
  const auto path = older / "fixed/H086";
  const auto spec = older / "specs/H086/fixed.json";
  const auto saved = load(spec);
  const auto &role = arm["roles"]["0"];
  check(hash(path / "receipt.json") == arm["receipt_sha256"].asString() &&
            hash(path / "run/components/emission.json") ==
                arm["plan_sha256"].asString() &&
            hash(spec) == arm["spec_sha256"].asString() &&
            hash(bundle / "SHA256SUMS") == arm["catalog_sha256"].asString() &&
            proof["supplied_sha256"] == record["reply_sha256"] &&
            saved["input_type"] == tsv &&
            saved["roles"][0]["output_type"] == plain &&
            saved["roles"][0]["requirement"]["operations"] == steps &&
            arm["verified"] == true && complete(role) &&
            selects(role, "tab_records") && selects(role, "reachable_nodes") &&
            selects(role, "write_lines"),
        "retained H086 evidence does not match the current context");
  for (const auto &split : splits) {
    const auto fixture = fixtures / split;
    const auto input = fixture / "input/build.tsv";
    const auto expected = oracle("H086", read_edges(bytes(input)));
    check(hash(input) == proof["inputs"][split].asString() &&
              arm["scores"][split] == true &&
              arm["outputs"][split][0] == expected &&
              bytes(fixture / "output/result-1.txt") == expected,
          "retained H086 fixture differs for " + split);
  }
  const auto input = path / "cyclic-prerequisites/input";
  const auto expected = oracle("H086", read_edges(bytes(input)));
  const auto &cyclic = proof["cyclic_prerequisites"];
  check(hash(input) == cyclic["input_sha256"].asString() &&
            cyclic["passed"] == true && cyclic["actual"][0] == expected &&
            bytes(path / "cyclic-prerequisites/result-0.txt") == expected &&
            expected == "a\nb\ncore\ntail\n",
        "retained H086 cycle does not match independent traversal");
  return proof;
}
} // namespace

int main() try {
  const auto rank = runs / "v5-unordered-finance-replay-XXvu2pML/report.json";
  const auto prior_pin = hash(rank);
  check(prior_pin ==
            "20ab431f685db586b3fd07fcfb1d1a7e5bc0dba8727a14642aa7340ba5d3b252",
        "changed frequency report");
  const auto ranking = load(rank);
  auto ids = array();
  for (const auto &id : requests)
    ids.append(id);
  check(ranking["family_frequency"][2]["name"] == "build scheduling" &&
            ranking["family_frequency"][2]["requests"] == ids,
        "changed build-scheduling cohort");
  const auto recipes_file = root / "shared/request-recipes.json";
  const auto recipe_pin = hash(recipes_file);
  check(recipe_pin ==
            "768676cbc34ef7db9d869520bd3d26d86123f49a3c9803901eb5653de3a6f49b",
        "changed supplied recipes");
  const auto recipes = load(recipes_file);

  const auto parent = runs / "v5-queue-XXH5XMi9/stage/package";
  const auto bundle = parent / "bundle";
  const auto base_pin = hash(parent / "manifest.json");
  check(base_pin ==
            "d15aee96917ff440f58f76270e3ca48575268dcfcf2bce951805c865aae3af63",
        "changed V5 catalog stage");
  verify_bundle(
      bundle,
      "436db92629463daee559de2cbbc1c81519e12306d5bf01cc6d2883f3140e779c");
  const auto active = fs::canonical(volume / "unllm/boilerplate/active");
  const auto active_pin = hash(active / "SHA256SUMS");
  check(active_pin ==
            "e4564e3079f0b32b3379a411b170ce9af61ec80ab7938b6a974e724abd538328",
        "changed published catalog");
  const auto older = runs / "v5-reachability-replay-XXxyKVfL";
  const auto older_pin = hash(older / "report.json");
  const auto reachability = load(older / "report.json");
  check(older_pin == "45a5750d826899e2edaf056637d096418cde2ff389d8d9690aebd0800"
                     "a511064" &&
            reachability["parent_manifest_sha256"] == base_pin &&
            reachability["active_manifest_sha256"] == active_pin &&
            reachability["recipes_sha256"] == recipe_pin,
        "changed H086 reachability evidence");

  const auto work = fresh_run("v5-unordered-scheduling-replay");
  std::cout << work.string() << std::endl;
  auto report = object({{"active_path", active.string()},
                        {"active_sha256", active_pin},
                        {"frequency_report_sha256", prior_pin},
                        {"recipes_sha256", recipe_pin},
                        {"parent_manifest_sha256", base_pin},
                        {"reachability_report_sha256", older_pin},
                        {"tools", tools()}});
  report["tools"]["unllm_logic_worker"] =
      hash(main_repo / "build/native/unllm_logic_worker");
  report["bindings"] =
      array({exact_row(bundle, "tab_records", "in:" + tsv, "rows"),
             exact_row(bundle, "filter_membership", "rows", "rows"),
             exact_row(bundle, "reachable_nodes", "rows", "strings"),
             exact_row(bundle, "graph_witnesses", "rows", "rows"),
             exact_row(bundle, "write_lines", "strings", "out:" + plain)});

  auto reversed = witness;
  std::reverse(reversed.begin(), reversed.end());
  auto dag = Edges(witness.begin(), witness.end());
  for (const auto &edge : {Edge{"onlySelf", "onlySelf"}, Edge{"selfA", "selfA"},
                           Edge{"c3", "c1"}, Edge{"b", "a"}})
    check(dag.erase(edge) == 1, "missing cycle edge in witness");
  const std::map<std::string, std::string> fresh{
      {"full", tsv_bytes(witness)},
      {"reversed", tsv_bytes(reversed)},
      {"dag", tsv_bytes(dag)}};
  const auto graph = read_edges(fresh.at("full"));
  check(
      graph.size() + 1 == witness.size() &&
          graph == read_edges(fresh.at("reversed")) &&
          oracle("H081", graph) == "core\nroot\ntail\nup\nz\n" &&
          oracle("H082", graph) == "child\nend\nlong\nloose\nplain\ntarget\n" &&
          oracle("H086", graph) == "a\nb\ncore\ntail\n" &&
          oracle("H087", graph) == "a\napp\nb\nc1\nc2\nc3\nonlySelf\nselfA\n" &&
          oracle("H087", dag).empty() &&
          oracle("H089", graph) == "root\ttarget\n",
      "fresh graph lost an intended distinction");

  const auto archive = runs / "english-audit-bcsgf8pi/replay-staged";
  const auto historical = runs / "shared-review-5ui86km8/transfer";
  std::vector<Path> programs;
  for (const auto &id : requests) {
    const auto dir = archive / id;
    const auto &record = ranking["archive"][id];
    const auto saved_pin = hash(dir / response);
    const auto saved = load(dir / response);
    const auto &role = saved["roles"][0];
    const auto steps = operations(recipes[id][0]);
    check(saved_pin == record["reply_sha256"].asString() &&
              hash(record["reply_source"].asString()) == saved_pin &&
              hash(dir / "request.txt") ==
                  record["request_sha256"].asString() &&
              saved["input_type"] == tsv && saved["roles"].size() == 1 &&
              role["output_type"] == plain &&
              role["requirement"]["operations"] == array(),
          "changed saved scheduling context for " + id);
    auto &entry = report["scheduling"][id];
    entry["archive"] = record;
    const auto archived =
        plan_probe(bundle, work / "plans" / id / "archived", saved);
    check(archived["gaps"]["r"] == empty_stop && archived["routes"].empty(),
          "changed archived operation stop for " + id + ": " + dump(archived));
    entry["archived_plan"] = archived;
    const auto fixtures = historical / id / "fixtures";
    if (id == "H086") {
      entry["reused"] =
          reuse_h086(older, reachability, bundle, fixtures, steps, record);
      write(work / "progress.json", report);
      continue;
    }

    auto inputs = object();
    for (const auto &split : splits)
      inputs[split] = hash(fixtures / split / "input/build.tsv");
    entry["fixture_input_sha256"] = inputs;
    Strings arms{"reference"};
    if (id == "H087")
      arms.push_back("self-only");
    if (id == "H089")
      append(arms, {"without-direct", "repeated-modules", "transitive"});
    for (const auto &arm : arms) {
      auto spec = saved;
      auto &requirement = spec["roles"][0]["requirement"];
      requirement["operations"] =
          arm == "reference" ? steps : variant(steps, arm);
      requirement["uncovered"] = array();
      const auto file = work / "specs" / arm / (id + ".json");
      write(file, spec);
      const auto result =
          trial(work, arm, id, bundle, file, "build.tsv", inputs);
      const auto &emitted = result["roles"]["0"];
      const auto exe = work / arm / id / "run/project/converter";
      entry["runs"][arm] = result;
      write(work / "progress.json", report);
      check(complete(emitted) && fs::is_regular_file(exe) &&
                emitted["steps"][0]["provider"] == "tab_records" &&
                emitted["steps"][emitted["steps"].size() - 1]["provider"] ==
                    "write_lines" &&
                selects(emitted, id == "H081" || id == "H082"
                                     ? "filter_membership"
                                     : "graph_witnesses"),
            "unexpected scheduling route for " + id + " " + arm);
      programs.push_back(exe);
      for (const auto &split : splits) {
        const auto fixture = fixtures / split;
        const auto edges = read_edges(bytes(fixture / "input/build.tsv"));
        const auto correct = oracle(id, edges);
        const auto predicted = oracle(id, edges, arm);
        check(correct == bytes(fixture / "output/result-1.txt") &&
                  result["outputs"][split][0] == predicted &&
                  result["scores"][split] == (predicted == correct),
              "archived output differs from oracle for " + id + " " + arm +
                  " " + split);
      }
      if (arm != "reference")
        check(oracle(id, graph, arm) != oracle(id, graph),
              "fresh full graph did not detect " + arm);
      for (const auto &[name, data] : fresh) {
        const auto answer = oracle(id, read_edges(data), arm);
        const auto e = extra_case(work / "witness" / id / arm / name, exe,
                                  bundle, data, array({answer}));
        check(e["passed"] == true, "scheduling witness differs for " + id +
                                       " " + arm + " " + name + ": " + dump(e));
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
