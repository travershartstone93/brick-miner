#include "bundle.hpp"
#include "migration_reports.hpp"
#include "pilot.hpp"
#include <QByteArray>
#include <QScopeGuard>
#include <algorithm>

namespace v5 {
namespace {
const std::string source_id = "retained-tab-column";

J tabular_goal(const J &trial, bool reader_trial) {
  const auto outcome = reader_trial ? trial["outcome"] : J("ordered_column");
  const auto parameters = reader_trial ? trial["parameters"]
                                        : object({{"column", trial["index"]}});
  return spec_for("text/tab-separated-values", trial["type"].asString(),
                  object({{"outcome", outcome},
                          {"parameters", parameters},
                          {"uncovered", array()}}));
}

void prepare_input(const Path &path, const J &fixture) {
  if (fixture.isMember("input")) {
    save(path, fixture["input"].asString());
  } else if (fixture.isMember("input_hex")) {
    const auto hex = QByteArray::fromStdString(fixture["input_hex"].asString());
    const auto bytes = QByteArray::fromHex(hex);
    check(bytes.toHex() == hex, "invalid TSV hex fixture");
    save(path, bytes.toStdString());
  } else if (fixture.get("directory", false).asBool()) {
    fs::create_directory(path);
  }
}

J file_cases(const Path &dir, const Path &exe, const J &trial,
             const Path &runtime_bundle) {
  auto observations = array();
  for (const auto &fixture : trial["cases"])
    for (bool existing : {false, true}) {
      const auto root = dir / fixture["name"].asString() /
                        (existing ? "present" : "absent");
      fs::create_directories(root);
      prepare_input(root / "input", fixture);
      const auto out = root / "out";
      const std::string sentinel = "existing destination\n";
      if (existing)
        save(out, sentinel);
      auto cmd = runtime_bundle.empty() ? sandbox(exe, root)
                                        : duckdb_sandbox(exe, root, runtime_bundle);
      if (!runtime_bundle.empty())
        cmd.push_back("/probe");
      append(cmd, {"input", "out"});
      const auto result = execute(cmd, {.seconds = 8, .probe_limits = true});
      const bool failed = fixture.isMember("error");
      check(result.status == (failed ? 1 : 0) &&
                fs::exists(out) == (existing || !failed),
            "tabular exit or destination differs: " + root.string());
      J observed;
      if (fs::exists(out)) {
        const auto raw = bytes(out);
        const auto expected = failed ? J(sentinel) : fixture["output"];
        const auto actual = !failed && trial["type"] == "application/json"
                                ? parse(raw) : J(raw);
        check(actual == expected,
              "tabular output differs: " + root.string());
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
           const J &sibling, const J &trials, bool reader_trial) {
  const auto base = verified_base();
  const auto previous = qgetenv("PKG_CONFIG_PATH");
  const auto restore = qScopeGuard([&] {
    if (previous.isNull())
      qunsetenv("PKG_CONFIG_PATH");
    else
      qputenv("PKG_CONFIG_PATH", previous);
  });
  if (reader_trial)
    check(qputenv("PKG_CONFIG_PATH", (bundle / "runtime/duckdb/lib/pkgconfig").c_str()),
          "cannot pin the positioned report runtime");
  const auto cut = work / "control-bundle";
  const auto reduced = remove_binding(read_tree(bundle), row);
  const auto providers = parse(reduced.at("providers/contracts.json"))["providers"];
  check(std::count(providers.begin(), providers.end(), sibling) == 1,
        "exact tabular removal lost the sibling");
  write_tree(cut, reduced);
  J results = array();
  for (const auto &trial : trials) {
    const auto spec = tabular_goal(trial, reader_trial);
    const auto name = trial["name"].asString();
    const auto writer = trial["type"] == "application/json" ? "write_texts" : "write_lines";
    const auto chain = reader_trial ? trial["chain"] :
                                      array({"tab_records", "column", writer});
    auto evidence = object({{"name", name}, {"spec", spec}, {"chain", chain}});
    for (const std::string arm : {"baseline", "staged"}) {
      const auto dir = work / arm / name;
      const auto exe = work / ".test-programs" / (arm + "-" + name);
      const auto &catalog = arm == "baseline" ? base : bundle;
      const auto program = complete_file_plan(catalog, dir, spec, chain);
      check(program["source"].asString().find(row["call"].asString() + "(") != std::string::npos,
            "ordinary emission used a different callable");
      build_emission(program, dir, exe, "g++-15");
      evidence[arm] = object({{"emitted_sha256", digest(dir / "component.hpp")},
                              {"program_sha256", digest(exe)},
                              {"observations", file_cases(dir, exe, trial,
                                                         reader_trial ? catalog : Path{})}});
    }
    check(evidence["baseline"] == evidence["staged"],
          "staged ordinary tabular behavior or executable drifted");
    const auto removed = plan(cut, spec, work / "removed" / name);
    check(removed["interpretation_errors"].empty() && removed["clarifications"].empty(),
          "tabular removal changed goal interpretation");
    if (reader_trial) {
      check(removed["providers"].size() == 1, "expected one partial route after reader removal");
      const auto &partial = removed["providers"][0];
      const auto &holes = partial["typed_gaps"];
      check(partial["status"] == "partial" && partial["uncovered"].empty() &&
                holes.size() == 1 && holes[0]["from"] == row["from"] &&
                holes[0]["to"] == row["to"],
            "reader removal did not isolate the typed input port");
      auto remaining = array();
      for (const auto &step : partial["plan"]["steps"])
        if (step.isMember("provider"))
          remaining.append(step["provider"]);
      check(chain.size() == 3 && remaining == array({chain[1], chain[2]}),
            "reader removal changed downstream bindings");
      evidence["removed_typed_gaps"] = holes;
      evidence["retained_chain"] = remaining;
    } else {
      check(removed["providers"].isArray() && removed["providers"].empty() &&
                removed["gaps"]["r"].asString().find("ordered_column") != std::string::npos,
            "exact column removal did not isolate the selected outcome");
      evidence["removed_gaps"] = removed["gaps"];
    }
    results.append(evidence);
  }
  return object({{"trials", results},
                 {"removed_bundle_sha256", digest(cut / "SHA256SUMS")},
                 {reader_trial ? "unchanged_projection" : "unchanged_reader", sibling}});
}

Path qualify_tabular(const Path &package, const std::string &pin,
                     const Path &registry, const std::string &registry_pin,
                     bool reader_trial) {
  verify(package, pin);
  const auto src = sources()[source_id];
  const std::string target = reader_trial ? "tab_records" : "column";
  const std::string fixture = reader_trial ? "tab-records" : "column";
  const auto job = retained_binding_job(v5_root() / "sources.json", target);
  check(src["approval"] == "local-pilot" && src["method"] == "reviewed-local-git-archive" &&
            digest(src["archive"].asString()) == src["sha256"].asString() &&
            job["source"] == source_id && job["stream"] == "foundational" &&
            job["route"] == "direct" && job["tests"] == "v5/fixtures/" + fixture + ".json",
        "unapproved retained tabular archive or job");
  const auto priority = rank_supplied_usage(
      project_file(runs, job["coverage_audit"].asString()), job["coverage_sha256"].asString(),
      project_file(runs, job["migration_snapshot"].asString() + "/ledger.sqlite").parent_path(),
      job["ledger_sha256"].asString());
  const auto selection = select_ranked_scope(package, pin, priority, job);
  const auto bundle = package / "bundle";
  const auto defs = load(bundle / "providers/contracts.json");
  const auto rows = defs["providers"];
  const auto column = select_binding(rows, "ordered_column", "t80", "rows", "strings");
  const auto found = std::find_if(rows.begin(), rows.end(), [](const auto &candidate) {
    return candidate["id"] == "tab_records" && candidate["brick"] == "t80" &&
           candidate["from"] == "in:text/tab-separated-values" && candidate["to"] == "rows";
  });
  check(found != rows.end(), "missing retained tab_records sibling");
  const auto &reader = *found;
  const auto &row = reader_trial ? reader : column;
  const auto &other = reader_trial ? column : reader;
  const auto record = file_binding_identity(row, bundle);
  const auto sibling = file_binding_identity(other, bundle);
  const auto &selected = selection["selected"];
  check(column["id"] == "column" && column["args"] == array({"column"}) &&
            row["call"] == job["callable"] && reader["id"] == "tab_records" &&
            !reader.isMember("effect") && !reader.isMember("args") &&
            column["header"] == reader["header"] &&
            selected["binding_id"] == row["id"] && selected["component_id"] == row["brick"] &&
            selected["binding_sha256"] == record["binding_sha256"] &&
            selected["header_sha256"] == record["header_sha256"],
        "column, reader or ranked binding differs");
  const auto work = fresh_run("v5-" + fixture), stage = work / ".stage";
  const auto scratch = work / ".test-programs";
  fs::create_directory(scratch);
  save_file_registry(stage, src, registry, registry_pin);
  check(retained_binding_job(stage / "sources.json", target) == job,
        "changed retained tabular job");
  auto source = retained_source(stage, source_id, "project-LICENSE");
  for (const auto &file : src["members"].getMemberNames())
    check(digest(bundle / file) == src["members"][file][1].asString(),
          "staged tabular closure differs: " + file);
  const auto archived = [&](const std::string &field) {
    const auto data = archive_member(src, src[field].asString());
    check(sha(data) == src[field + "_sha256"].asString(), "changed tabular " + field);
    return data;
  };
  const auto documentation = parse(archived("documentation"));
  const auto historical = documentation["providers"];
  check(std::count(historical.begin(), historical.end(), row) == 1 &&
            documentation["outcomes"]["ordered_column"] == defs["outcomes"]["ordered_column"],
        "tabular binding differs from archived project declaration");
  save(stage / "original/project-test-components.cpp", archived("examples"));
  source["binding"] = row;
  source["sibling"] = other;
  source["declaration"] = reader_trial ? reader : documentation["outcomes"]["ordered_column"];
  write(stage / "source.json", source);
  write(stage / "priority.json", priority);
  write(stage / "selection.json", selection);
  fs::copy_file(project_file(root, job["tests"].asString()), stage / "spec.json");
  fs::copy_file(v5_root() / "fixtures" / (fixture + "-client.cpp"), stage / "client.cpp");
  fs::copy_file(v5_root() / "fixtures" / (fixture + "-annotation.json"), stage / "annotation.json");
  const auto suite = load(stage / "spec.json");
  check(suite["binding"] == record["key"], "column fixtures name a different binding");

  const auto exe = scratch / "client";
  Strings cmd{"g++-15", "-std=c++17", "-O1", "-I", stage / "original/providers",
              "-I", stage / "original/headers", stage / "client.cpp", "-o", exe};
  const auto tools = file_qualification_tools();
  const auto compiler_flags = flags({"jsoncpp"});
  append(cmd, compiler_flags);
  run(cmd);
  Path cases;
  if (reader_trial) {
    cases = work / "direct";
    fs::create_directory(cases);
    for (const auto *kind : {"direct", "errors"})
      for (const auto &test : suite[kind])
        prepare_input(cases / test["name"].asString(), test);
  }
  RunOptions opts{.input = dump(suite), .seconds = 8, .probe_limits = true};
  const auto probe = sandbox(exe, cases);
  const auto observed = parse(run(probe, opts));
  check(observed["direct"].size() == suite["direct"].size() &&
            observed["errors"].size() == suite["errors"].size() &&
            observed == parse(run(probe, opts)),
        "tabular standalone observations do not reproduce");
  write(stage / "standalone.json",
        object({{"fresh_processes", 2}, {"calls_per_case", 2}, {"observations", observed}}));
  write(stage / "integration.json", ordinary(work, bundle, row, other, suite["ordinary"], reader_trial));
  check(tools == file_qualification_tools(), "tabular qualification tools changed");
  auto env = path_client_environment(exe, tools);
  env["client_flags"] = string_array(compiler_flags);
  if (reader_trial)
    env["ordinary_runtime"] =
        runtime_files(bundle, verified_base(), {"libbrick_shared_relational.so",
                      "libduckdb.so", "pkgconfig/brick_shared_relational.pc"});
  write(stage / "environment.json", env);
  auto contract = file_contract(stage, package, record, src, stage / "sources.json");
  contract["qualification"]["scope"] =
      reader_trial ? "Detached plain byte-oriented TSV reader on the pinned Linux C++17 environment."
                   : "Detached synchronous row projection and TSV file trials on the pinned C++17 environment.";
  contract["fingerprint"]["scope"] =
      reader_trial ? "Tab and LF separation, terminal CR handling, raw bytes, one-based positions and read failures."
                   : "Field selection, byte and visit-order preservation, and short-row and producer errors.";
  validate(contract, load(package / "contract.schema.json"));
  return seal_file_qualification(work, package, pin, array({record, sibling}), 0, contract);
}
} // namespace

Path qualify_column(const Path &package, const std::string &pin,
                    const Path &registry, const std::string &registry_pin) {
  return qualify_tabular(package, pin, registry, registry_pin, false);
}

Path qualify_tab_records(const Path &package, const std::string &pin,
                         const Path &registry, const std::string &registry_pin) {
  return qualify_tabular(package, pin, registry, registry_pin, true);
}
} // namespace v5
