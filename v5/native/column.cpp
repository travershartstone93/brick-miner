#include "bundle.hpp"
#include "migration_reports.hpp"
#include "pilot.hpp"
#include <algorithm>

namespace v5 {
namespace {
const std::string source_id = "retained-tab-column";

J column_goal(const J &trial) {
  return spec_for("text/tab-separated-values", trial["type"].asString(),
                  object({{"outcome", "ordered_column"},
                          {"parameters", object({{"column", trial["index"]}})},
                          {"uncovered", array()}}));
}

J file_cases(const Path &dir, const Path &exe, const J &trial) {
  auto observations = array();
  for (const auto &fixture : trial["cases"])
    for (bool existing : {false, true}) {
      const auto root = dir / fixture["name"].asString() /
                        (existing ? "present" : "absent");
      fs::create_directories(root);
      if (fixture.isMember("input"))
        save(root / "input", fixture["input"].asString());
      const auto out = root / "out";
      const std::string sentinel = "existing destination\n";
      if (existing)
        save(out, sentinel);
      auto cmd = sandbox(exe, root);
      append(cmd, {"input", "out"});
      const auto result = execute(cmd, {.seconds = 8, .probe_limits = true});
      const bool failed = fixture.isMember("error");
      check(result.status == (failed ? 1 : 0) &&
                fs::exists(out) == (existing || !failed),
            "column exit or destination differs: " + root.string());
      J observed;
      if (fs::exists(out)) {
        const auto raw = bytes(out);
        const auto expected = failed ? J(sentinel) : fixture["output"];
        const auto actual = !failed && trial["type"] == "application/json"
                                ? parse(raw) : J(raw);
        check(actual == expected,
              "column output differs: " + root.string());
        observed = sha(raw);
      }
      observations.append(object({{"case", fixture["name"]},
                                  {"destination_existed", existing},
                                  {"exit", result.status},
                                  {"output_sha256", observed}}));
    }
  return observations;
}

J ordinary(const Path &work, const Path &bundle, const J &row,
           const J &reader, const J &trials) {
  const auto base = verified_base();
  const auto cut = work / "control-bundle";
  const auto reduced = remove_binding(read_tree(bundle), row);
  const auto providers = parse(reduced.at("providers/contracts.json"))["providers"];
  check(std::count(providers.begin(), providers.end(), reader) == 1,
        "exact projection removal lost the sibling reader");
  write_tree(cut, reduced);
  J results = array();
  for (const auto &trial : trials) {
    const auto spec = column_goal(trial);
    const auto name = trial["name"].asString();
    const auto writer = trial["type"] == "application/json" ? "write_texts" : "write_lines";
    const auto chain = array({"tab_records", "column", writer});
    auto evidence = object({{"name", name}, {"spec", spec}, {"chain", chain}});
    for (const std::string arm : {"baseline", "staged"}) {
      const auto dir = work / arm / name;
      const auto exe = work / ".test-programs" / (arm + "-" + name);
      const auto program = complete_file_plan(arm == "baseline" ? base : bundle,
                                              dir, spec, chain);
      check(program["source"].asString().find("brick_data::column(") != std::string::npos,
            "ordinary emission used a different callable");
      build_emission(program, dir, exe, "g++-15");
      evidence[arm] = object({{"emitted_sha256", digest(dir / "component.hpp")},
                              {"program_sha256", digest(exe)},
                              {"observations", file_cases(dir, exe, trial)}});
    }
    check(evidence["baseline"] == evidence["staged"],
          "staged ordinary column behavior or executable drifted");
    const auto removed = plan(cut, spec, work / "removed" / name);
    check(removed["providers"].isArray() && removed["providers"].empty() &&
              removed["interpretation_errors"].empty() && removed["clarifications"].empty() &&
              removed["gaps"]["r"].asString().find("ordered_column") != std::string::npos,
          "exact column removal failed to isolate the outcome: " + dump(removed));
    evidence["removed_gaps"] = removed["gaps"];
    results.append(evidence);
  }
  return object({{"trials", results},
                 {"removed_bundle_sha256", digest(cut / "SHA256SUMS")},
                 {"unchanged_reader", reader}});
}
} // namespace

Path qualify_column(const Path &package, const std::string &pin,
                    const Path &registry, const std::string &registry_pin) {
  verify(package, pin);
  const auto src = sources()[source_id];
  const auto job = retained_binding_job(v5_root() / "sources.json", "column");
  check(src["approval"] == "local-pilot" && src["method"] == "reviewed-local-git-archive" &&
            digest(src["archive"].asString()) == src["sha256"].asString() &&
            job["source"] == source_id && job["stream"] == "foundational" &&
            job["route"] == "direct" && job["tests"] == src["tests"],
        "unapproved retained column archive or job");
  const auto priority = rank_supplied_usage(
      project_file(runs, job["coverage_audit"].asString()), job["coverage_sha256"].asString(),
      project_file(runs, job["migration_snapshot"].asString() + "/ledger.sqlite").parent_path(),
      job["ledger_sha256"].asString());
  const auto selection = select_ranked_scope(package, pin, priority, job);
  const auto bundle = package / "bundle";
  const auto defs = load(bundle / "providers/contracts.json");
  const auto rows = defs["providers"];
  const auto row = select_binding(rows, "ordered_column", "t80", "rows", "strings");
  const auto found = std::find_if(rows.begin(), rows.end(), [](const auto &candidate) {
    return candidate["id"] == "tab_records" && candidate["brick"] == "t80" &&
           candidate["from"] == "in:text/tab-separated-values" && candidate["to"] == "rows";
  });
  check(found != rows.end(), "missing retained tab_records sibling");
  const auto &reader = *found;
  const auto record = file_binding_identity(row, bundle);
  const auto sibling = file_binding_identity(reader, bundle);
  const auto &selected = selection["selected"];
  check(row["id"] == "column" && row["args"] == array({"column"}) &&
            row["call"] == job["callable"] && reader["id"] == "tab_records" &&
            !reader.isMember("effect") && row["header"] == reader["header"] &&
            selected["binding_id"] == row["id"] && selected["component_id"] == row["brick"] &&
            selected["binding_sha256"] == record["binding_sha256"] &&
            selected["header_sha256"] == record["header_sha256"],
        "column, reader or ranked binding differs");
  const auto work = fresh_run("v5-column"), stage = work / ".stage";
  const auto scratch = work / ".test-programs";
  fs::create_directory(scratch);
  save_file_registry(stage, src, registry, registry_pin);
  check(retained_binding_job(stage / "sources.json", "column") == job,
        "changed retained column job");
  auto source = retained_source(stage, source_id, "project-LICENSE");
  for (const auto &file : src["members"].getMemberNames())
    check(digest(bundle / file) == src["members"][file][1].asString(),
          "staged column closure differs: " + file);
  const auto archived = [&](const std::string &field) {
    const auto data = archive_member(src, src[field].asString());
    check(sha(data) == src[field + "_sha256"].asString(), "changed column " + field);
    return data;
  };
  const auto documentation = parse(archived("documentation"));
  const auto historical = documentation["providers"];
  check(std::count(historical.begin(), historical.end(), row) == 1 &&
            documentation["outcomes"]["ordered_column"] == defs["outcomes"]["ordered_column"],
        "column contract differs from archived project declaration");
  save(stage / "original/project-test-components.cpp", archived("examples"));
  source["binding"] = row;
  source["sibling"] = reader;
  source["declaration"] = documentation["outcomes"]["ordered_column"];
  write(stage / "source.json", source);
  write(stage / "priority.json", priority);
  write(stage / "selection.json", selection);
  fs::copy_file(project_file(root, job["tests"].asString()), stage / "spec.json");
  fs::copy_file(v5_root() / "fixtures/column-client.cpp", stage / "client.cpp");
  fs::copy_file(v5_root() / "fixtures/column-annotation.json", stage / "annotation.json");
  const auto suite = load(stage / "spec.json");
  check(suite["binding"] == record["key"], "column fixtures name a different binding");

  const auto exe = scratch / "client";
  Strings cmd{"g++-15", "-std=c++17", "-O1", "-I", stage / "original/providers",
              "-I", stage / "original/headers", stage / "client.cpp", "-o", exe};
  const auto tools = file_qualification_tools();
  const auto compiler_flags = flags({"jsoncpp"});
  append(cmd, compiler_flags);
  run(cmd);
  RunOptions opts{.input = dump(suite), .seconds = 8, .probe_limits = true};
  const auto observed = parse(run(sandbox(exe), opts));
  check(observed["direct"].size() == suite["direct"].size() &&
            observed["errors"].size() == suite["errors"].size() &&
            observed == parse(run(sandbox(exe), opts)),
        "column standalone observations do not reproduce");
  write(stage / "standalone.json",
        object({{"fresh_processes", 2}, {"calls_per_case", 2}, {"observations", observed}}));
  write(stage / "integration.json", ordinary(work, bundle, row, reader, suite["ordinary"]));
  check(tools == file_qualification_tools(), "column qualification tools changed");
  auto env = path_client_environment(exe, tools);
  env["client_flags"] = string_array(compiler_flags);
  write(stage / "environment.json", env);
  auto contract = file_contract(stage, package, record, src, stage / "sources.json");
  contract["qualification"]["scope"] = "Detached synchronous row projection and TSV file trials on the pinned C++17 environment.";
  contract["fingerprint"]["scope"] = "Field selection, byte and visit-order preservation, and short-row and producer errors.";
  validate(contract, load(package / "contract.schema.json"));
  return seal_file_qualification(work, package, pin, array({record, sibling}), 0, contract);
}
} // namespace v5
