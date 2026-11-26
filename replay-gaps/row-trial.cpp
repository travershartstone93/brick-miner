#include "row-trial.hpp"
#include <algorithm>
#include <iostream>

using namespace miner;
namespace row_replay {
const Strings splits{"ordinary", "boundary", "empty", "transfer", "sparse"};

std::string hash(const Path &file) { return sha(bytes(file)); }

J tools() {
  auto result = object();
  for (const auto *name :
       {"brick-row-replay", "brick-provider-plan", "brick-end-user-check"})
    result[name] = hash(main_repo / "build/native" / name);
  return result;
}

static Path split_dir(const Path &trial, const std::string &split) {
  return trial /
         (split == "transfer" || split == "sparse" ? "held"
                                                   : "run/check-initial") /
         split;
}

J input_hashes(const Path &trial, const std::string &name) {
  auto hashes = object();
  for (const auto &split : splits)
    hashes[split] = hash(split_dir(trial, split) / "input" / name);
  return hashes;
}

J scores(const J &receipt) {
  J result = object();
  for (const auto &split : splits) {
    const auto &score =
        receipt[split == "transfer" || split == "sparse" ? "held" : "initial"]
               [split];
    check(score.isBool(), "missing checker score for " + split);
    result[split] = score;
  }
  return result;
}

J operations(const J &recipe) {
  J list = array();
  for (const auto &step : recipe)
    list.append(object({{"outcome", step[0]}, {"parameters", step[1]}}));
  return list;
}

J plan_probe(const Path &bundle, const Path &dest, const J &spec) {
  check(spec["roles"].size() == 1, "plan probe requires one role");
  const auto &role = spec["roles"][0];
  const auto p =
      plan(bundle,
           spec_for(spec["input_type"].asString(),
                    role["output_type"].asString(), role["requirement"]),
           dest);
  auto routes = array();
  for (const auto &r : p["providers"])
    routes.append(object({{"status", r["status"]},
                          {"ports", ports(r)},
                          {"steps", r["plan"]["steps"]},
                          {"uncovered", r["uncovered"]}}));
  return object({{"plan_sha256", hash(dest / "components/emission.json")},
                 {"validation", p["interpretation_errors"]},
                 {"gaps", p["gaps"]},
                 {"routes", routes}});
}

static J requirements(const J &spec, unsigned slot) {
  return spec["roles"][slot]["requirement"]["uncovered"];
}

J trial(const Path &work, const std::string &arm, const std::string &request,
        const Path &bundle, const Path &spec_file) {
  std::cout << request << " " << arm << std::endl;
  const auto dir = work / arm / request;
  run({main_repo / "build/native/brick-row-replay", work, arm, request, bundle,
       spec_file},
      {.seconds = 480,
       .environment = {
           {"B42_RUNTIME", (bundle / "runtime/duckdb/lib").string()},
           {"B42_ABLATION", ""},
           {"CORPUS_ROOT", (volume / "unllm").string()}}});
  const auto receipt = load(dir / "receipt.json");
  const auto plan_file = dir / "run/components/emission.json";
  const auto plan = load(plan_file), spec = load(spec_file);
  check(receipt["manifest"] == hash(bundle / "SHA256SUMS") &&
            receipt["spec_sha256"] == hash(spec_file) &&
            receipt["interpreted"] == true && receipt["editor"] == false &&
            receipt["status"] != "runner_error" &&
            receipt["status"] != "time_budget" &&
            plan["interpretation_errors"].empty() &&
            plan["clarifications"].empty() &&
            load(dir / "run/calls/interpret/response.json") == spec,
        "changed replay context for " + request + " " + arm);
  auto roles = object();
  for (const auto &p : plan["providers"]) {
    auto slot = p["slot"].asUInt();
    auto key = std::to_string(slot);
    check(slot < spec["roles"].size() && !roles.isMember(key) &&
              p["uncovered"] == requirements(spec, slot),
          "unexpected emitted role or changed residual");
    roles[key] = object({{"status", p["status"]},
                         {"steps", p["plan"]["steps"]},
                         {"typed_gaps", p["typed_gaps"]},
                         {"uncovered", p["uncovered"]}});
  }
  auto outputs = object();
  for (const auto &split : splits) {
    auto values = array();
    for (unsigned i = 1; i <= spec["roles"].size(); ++i) {
      auto suffix = spec["roles"][i - 1]["output_type"] == "application/json"
                        ? ".json"
                        : ".txt";
      auto file = split_dir(dir, split) / "output" /
                  ("result-" + std::to_string(i) + suffix);
      values.append(fs::is_regular_file(file) ? J(bytes(file)) : J());
    }
    outputs[split] = values;
  }
  auto exe = dir / "run/project/converter";
  return object(
      {{"receipt_sha256", hash(dir / "receipt.json")},
       {"plan_sha256", hash(plan_file)},
       {"spec_sha256", receipt["spec_sha256"]},
       {"catalog_sha256", receipt["manifest"]},
       {"status", receipt["status"]},
       {"verified", receipt["verified"]},
       {"scores", scores(receipt)},
       {"outputs", outputs},
       {"roles", roles},
       {"planner_diagnostics", plan["gaps"]},
       {"program_sha256", fs::is_regular_file(exe) ? J(hash(exe)) : J()}});
}

J trial(const Path &work, const std::string &arm, const std::string &request,
        const Path &bundle, const Path &spec_file, const std::string &input,
        const J &expected_inputs) {
  auto result = trial(work, arm, request, bundle, spec_file);
  check(input_hashes(work / arm / request, input) == expected_inputs,
        "changed replay inputs for " + request + " " + arm);
  return result;
}

J ports(const J &role) {
  J gaps = array();
  for (const auto &g : role["typed_gaps"])
    gaps.append(array({g["from"], g["to"]}));
  return gaps;
}

bool selects(const J &role, const std::string &id) {
  const auto &steps = role["steps"];
  return std::any_of(steps.begin(), steps.end(), [&](const auto &step) {
    auto p = step["provider"].asString();
    return p == id || (p.starts_with("op_") && p.ends_with("_" + id));
  });
}

bool complete(const J &role) {
  return role["status"] == "complete" && role["typed_gaps"].empty() &&
         role["uncovered"].empty();
}

J exact_row(const Path &bundle, const std::string &id, const std::string &from,
            const std::string &to) {
  auto all = load(bundle / "providers/contracts.json");
  J row;
  for (const auto &r : all["providers"])
    if (r["id"] == id) {
      check(row.isNull(), "ambiguous binding " + id);
      row = r;
    }
  check(row["from"] == from && row["to"] == to, "binding port mismatch " + id);
  return row;
}

J extra_case(const Path &dir, const Path &exe, const Path &bundle,
             const std::string &input, const J &expected, int expected_exit) {
  save(dir / "input", input);
  auto cmd = sandbox(exe, dir);
  cmd.pop_back();
  append(cmd, {"--ro-bind", bundle / "runtime/duckdb/lib", "/runtime",
               "--setenv", "LD_LIBRARY_PATH", "/runtime", "/probe", "input"});
  for (unsigned i = 0; i < expected.size(); ++i)
    cmd.push_back("result-" + std::to_string(i) + ".txt");
  auto result = execute(cmd, {.seconds = 12, .probe_limits = true});
  check(result.status == expected_exit,
        "unexpected witness exit in " + dir.string() + ": " + result.err);
  auto actual = array();
  for (unsigned i = 0; i < expected.size(); ++i) {
    auto dest = dir / ("result-" + std::to_string(i) + ".txt");
    actual.append(fs::is_regular_file(dest) ? J(bytes(dest)) : J());
  }
  auto evidence = object({{"input_sha256", hash(dir / "input")},
                          {"expected", expected},
                          {"actual", actual},
                          {"passed", actual == expected}});
  if (expected_exit)
    evidence["exit"] = result.status;
  return evidence;
}

void verify_bundle(const Path &bundle, const std::string &expected) {
  check(hash(bundle / "SHA256SUMS") == expected, "changed replay bundle");
  run({"sha256sum", "--strict", "--quiet", "-c", "SHA256SUMS"},
      {.cwd = bundle});
}

J verify_json_records_scope(const Path &bundle, const Path &stage,
                            const std::string &expected_manifest,
                            const std::string &selector) {
  const auto pin = hash(stage / "manifest.json");
  check(pin == expected_manifest, "changed detached JSON reader package");
  const auto manifest = load(stage / "manifest.json");
  for (const auto *name : {"source.json", "domain.json", "contract.json"})
    check(hash(stage / name) == manifest["files"][name].asString(),
          "changed JSON reader evidence file");
  const auto source = load(stage / "source.json");
  const auto domain = load(stage / "domain.json");
  const auto reader =
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
} // namespace row_replay
