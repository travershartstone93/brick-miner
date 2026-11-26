#include "bundle.hpp"
#include "migration_reports.hpp"
#include "pilot.hpp"
#include <map>

namespace v5 {
namespace {
const std::string operation = "first_items";

J goal(const J &parameters) {
  return spec_for("text/plain", "application/json",
                  object({{"outcome", operation}, {"parameters", parameters},
                          {"uncovered", array()}}));
}

J ordinary(const Path &work, const Path &base, const Path &bundle,
           const J &binding, const J &fixture) {
  std::map<Json::UInt64, J> cases_by_count;
  for (const auto &test : fixture["cases"])
    if (!test.get("standalone_only", false).asBool())
      cases_by_count[test["count"].asUInt64()].append(test);

  const auto chain = array({"read_lines", "first_items", "write_texts"});
  auto trials = array();
  const auto removal_bundle = work / "control-bundle";
  write_tree(removal_bundle, remove_binding(read_tree(bundle), binding));
  for (const auto &[count, cases] : cases_by_count) {
    const auto label = std::to_string(count);
    const auto spec = goal(object({{"count", cases[0]["count"]}}));
    auto tests = sequence_requirements(cases, "expected")["cases"];
    auto trial = object({{"count", cases[0]["count"]}, {"goal", spec}, {"cases", tests}});
    for (const auto &[arm, catalog] : std::map<std::string, Path>{
             {"baseline", base}, {"staged", bundle}}) {
      const auto dest = work / arm / label;
      const auto exe = work / ".test-programs" / (arm + "-" + label);
      auto compiled = complete_file_plan(catalog, dest, spec, chain);
      build_emission(compiled, dest, exe, "g++-15");
      trial[arm] = object({{"source_sha256", digest(dest / "component.hpp")},
                           {"program_sha256", digest(exe)},
                           {"outcomes", ordinary_file_probes(exe, dest, tests)}});
    }
    check(trial["baseline"] == trial["staged"],
          "retained first-items program differs between catalog arms");
    const auto removed = plan(removal_bundle, spec, work / "removed" / label);
    check(removed["providers"].isArray() && removed["providers"].empty() &&
              removed["interpretation_errors"].empty() &&
              removed["clarifications"].empty() &&
              removed["gaps"]["r"].asString().find(operation) != std::string::npos,
          "exact first-items removal left a complete plan");
    trial["removed_gaps"] = removed["gaps"];
    trials.append(trial);
  }

  auto invalid = array();
  for (const auto &test : fixture["invalid_parameters"]) {
    const auto name = test["name"].asString();
    const auto spec = goal(test["parameters"]);
    const auto rejected = plan(bundle, spec, work / "invalid" / name);
    for (const auto &candidate : rejected["providers"])
      check(candidate["status"] != "complete", "unexpected plan for invalid count " + name);
    invalid.append(object({{"name", name}, {"goal", spec},
                            {"interpretation_errors", rejected["interpretation_errors"]},
                            {"clarifications", rejected["clarifications"]},
                            {"gaps", rejected["gaps"]}}));
  }
  return object({{"chain", chain}, {"trials", trials}, {"invalid_parameters", invalid},
                 {"removed_bundle_sha256", digest(removal_bundle / "SHA256SUMS")}});
}
} // namespace

Path qualify_first_items(const Path &package, const std::string &pin,
                         const Path &registry, const std::string &registry_pin) {
  verify(package, pin);
  const auto src = sources()["retained-first-items"];
  const auto job = retained_binding_job(v5_root() / "sources.json", operation);
  check(src["approval"] == "local-pilot" &&
            src["method"] == "reviewed-local-git-archive" &&
            digest(src["archive"].asString()) == src["sha256"].asString() &&
            job["source"] == src["id"] && job["stream"] == "foundational" &&
            job["route"] == "direct" && job["tests"] == "v5/fixtures/first-items.json",
        "unapproved first-items archive or job");
  const auto priority = rank_supplied_usage(
      project_file(runs, job["coverage_audit"].asString()),
      job["coverage_sha256"].asString(),
      project_file(runs, job["migration_snapshot"].asString() + "/ledger.sqlite")
          .parent_path(),
      job["ledger_sha256"].asString());
  const auto work = fresh_run("v5-first-items"), stage = work / ".stage",
             scratch = work / ".test-programs",
             bundle = package / "bundle", base = verified_base();
  fs::create_directory(scratch);
  save_file_registry(stage, src, registry, registry_pin);
  check(retained_binding_job(stage / "sources.json", operation) == job,
        "changed first-items qualification job");
  const auto defs = load(bundle / "providers/contracts.json");
  const auto row = select_binding(defs["providers"], operation, "x17", "strings", "strings");
  const auto record = file_binding_identity(row, bundle);
  const auto selection = select_ranked_scope(package, pin, priority, job);
  const auto &selected = selection["selected"];
  const auto &outcome = defs["outcomes"][operation];
  check(row["id"] == operation && selected["binding_id"] == operation &&
            selected["component_id"] == row["brick"] &&
            selected["binding_sha256"] == record["binding_sha256"] &&
            selected["header_sha256"] == record["header_sha256"] &&
            row["call"] == job["callable"] && row["args"] == array({"count"}) &&
            outcome["parameters"] == object({{"count", "column"}}),
        "first-items qualification differs from ranked binding or parameter type");
  auto source = retained_source(stage, src["id"].asString(), "project-LICENSE");
  for (const auto &member : src["members"].getMemberNames())
    check(digest(bundle / member) == src["members"][member][1].asString(),
          "staged first-items header closure differs: " + member);
  const auto declaration = operation + "|strings|strings|" + row["call"].asString() +
                           "|count:column|" + outcome["meaning"].asString();
  const auto documentation = archive_member(src, src["documentation"].asString());
  check(sha(documentation) == src["documentation_sha256"].asString() &&
            documentation.find("\n" + declaration + "\n") != std::string::npos,
        "first-items operation declaration differs from the reviewed archive");
  source["binding"] = row;
  source["declaration"] = declaration;
  write(stage / "source.json", source);
  write(stage / "priority.json", priority);
  write(stage / "selection.json", selection);
  for (const auto &[from, to] : std::map<std::string, std::string>{
           {"first-items.json", "spec.json"}, {"first-items-client.cpp", "client.cpp"},
           {"first-items-annotation.json", "annotation.json"}})
    fs::copy_file(v5_root() / "fixtures" / from, stage / to);
  const auto fixture = load(stage / "spec.json");
  check(fixture["binding"] == record["key"], "first-items fixture names a different binding");
  const auto tools = file_qualification_tools();
  const auto compiler_flags = flags({"Qt6Core", "jsoncpp"});
  const auto exe = scratch / "client";
  Strings build{"g++-15", "-std=c++17", "-O1", "-fPIC", "-I",
                stage / "original/headers", "-I", stage / "original/providers",
                stage / "client.cpp", "-o", exe};
  append(build, compiler_flags);
  run(build);
  const auto command = sandbox(exe);
  RunOptions options{.input = dump(fixture), .seconds = 8, .probe_limits = true};
  const auto first = parse(run(command, options));
  check(first == parse(run(command, options)) &&
            first["cases"].size() == fixture["cases"].size() &&
            first["controls"].size() == fixture["controls"].size() &&
            first["size_t_bits"] == 64,
        "first-items standalone evidence did not reproduce");
  write(stage / "standalone.json",
        object({{"fresh_processes", 2}, {"calls_per_case", 2}, {"observations", first}}));
  write(stage / "integration.json", ordinary(work, base, bundle, row, fixture));
  check(file_qualification_tools() == tools, "first-items qualification tools changed");
  auto environment = path_client_environment(exe, tools);
  environment["client_flags"] = string_array(compiler_flags);
  write(stage / "environment.json", environment);
  auto contract = file_contract(stage, package, record, src, stage / "sources.json");
  contract["qualification"]["scope"] =
      "Detached prefix selection on the tested 64-bit C++17 ABI and bounded ordinary count fixtures.";
  validate(contract, load(package / "contract.schema.json"));
  return seal_file_qualification(work, package, pin, array({record}), 0, contract);
}
} // namespace v5
