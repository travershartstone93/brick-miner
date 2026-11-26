#include "bundle.hpp"
#include "migration_reports.hpp"
#include "pilot.hpp"
#include <QScopeGuard>
#include <QStandardPaths>
#include <sstream>

namespace v5 {
namespace {
const std::string effect = "filter_records";
const std::string source_id = "retained-relational-filter";
const std::string kernel = "libbrick_shared_relational.so";
const Path runtime = "runtime/duckdb/lib";
const Path history = "qualifications/shared/relational";

J verify_materials(const Path &stage, const Path &bundle, const Path &base,
                   const J &src, const J &row) {
  const Path archive = src["archive"].asString();
  const auto recipe = archive.parent_path() / "PKGBUILD";
  const auto ring = arch_keyring(stage);
  const auto build = signed_build_info(src, recipe, ring);
  auto debug = src;
  debug["archive"] =
      (archive.parent_path() / src["debug_archive"].asString()).string();
  debug["sha256"] = src["debug_sha256"];
  debug["signature_sha256"] = src["debug_signature_sha256"];
  const auto debug_build = signed_build_info(debug, recipe, ring);
  const auto info = package_info(src, "duckdb");
  check(info.find("\nlicense = MIT\n") != std::string::npos,
        "signed package license changed");
  save(stage / "arch-pkginfo.txt", info);
  save(stage / "arch-pkgbuild.txt", bytes(recipe));
  save(stage / "notices/duckdb-LICENSE",
       archive_member(src, "usr/share/licenses/duckdb/LICENSE"));

  const auto provenance = load(bundle / history / "source.json");
  const auto symbol = src["symbols"][0].asString();
  const auto unit = provenance["source_units"][symbol].asString();
  const auto definition = provenance["definitions"][symbol];
  const auto kernel_pin = digest(bundle / runtime / kernel);
  check(provenance["source_id"] == src["upstream_source_id"] &&
            unit == "src/execution/operator/filter/physical_filter.cpp" &&
            provenance["kernel_sha256"].asString() == kernel_pin &&
            definition["name"].isString() &&
            run({"nm", "-D", "--defined-only", bundle / runtime / kernel})
                    .find(" " + definition["name"].asString() + "\n") !=
                std::string::npos,
        "retained DuckDB symbol, source unit or kernel differs");
  fs::create_directories(stage / "original");
  const auto source = stage / "original/physical_filter.cpp";
  run({"bsdtar", "-xOf", debug["archive"].asString(),
       "usr/src/debug/duckdb/duckdb/" + unit},
      {.output = source});
  const auto source_pin = digest(source);
  check(source_pin == src["source_unit_sha256"].asString(),
        "signed filter source member differs");
  const auto packed = stage.parent_path() / "retained-duckdb.so";
  run({"bsdtar", "-xOf", archive, "usr/lib/libduckdb.so"}, {.output = packed});
  check(digest(packed) == digest(bundle / runtime / "libduckdb.so"),
        "retained DuckDB shared object differs from signed package");
  fs::remove(packed);

  retain_adapter(stage, bundle, history, src, row, provenance,
                 "shared/relational/adapter.cpp");
  const auto table = bytes(bundle / history / "table.hpp");
  check(table ==
            run({"git", "-C", root, "show",
                 src["adapter_revision"].asString() + ":shared/table.hpp"}),
        "retained table implementation differs from original");
  save(stage / "original/table.hpp", table);
  const auto links = runtime_files(
      bundle, base,
      {kernel, "libduckdb.so", "pkgconfig/brick_shared_relational.pc"});
  const auto result = object({{"binding", row},
                              {"source_id", source_id},
                              {"kernel_sha256", kernel_pin},
                              {"upstream_symbol", definition},
                              {"source_unit", unit},
                              {"source_unit_sha256", source_pin},
                              {"arch_keyring_sha256", digest(ring)},
                              {"arch_signer", src["signer"]},
                              {"arch_buildinfo_sha256", sha(build)},
                              {"arch_debug_buildinfo_sha256", sha(debug_build)},
                              {"runtime_files", links}});
  write(stage / "source.json", result);
  return result;
}

Strings runtime_sandbox(const Path &exe, const Path &directory,
                        const Path &bundle) {
  auto cmd = sandbox(exe, directory);
  cmd.pop_back();
  append(cmd, {"--ro-bind", bundle / runtime, "/runtime", "--setenv",
               "LD_LIBRARY_PATH", "/runtime"});
  return cmd;
}

J goal(const J &parameters) {
  const auto steps =
      array({object({{"outcome", effect}, {"parameters", parameters}}),
             object({{"outcome", "ordered_column"},
                     {"parameters", object({{"column", 0}})}})});
  return spec_for("text/tab-separated-values", "text/plain",
                  object({{"operations", steps}, {"uncovered", array()}}));
}

J ordinary(const Path &work, const Path &bundle, const Path &base, const J &row,
           const J &spec) {
  const auto chain = array(
      {"tab_records", "op_0_filter_records", "op_1_column", "write_lines"});
  auto report = object({{"chain", chain}, {"trials", array()}});
  for (const auto &trial : spec["ordinary"]) {
    const auto name = trial["name"].asString();
    const auto spec = goal(trial["parameters"]);
    auto result = object({{"name", name}, {"goal", spec}});
    for (const auto &[arm, catalog] :
         std::map<std::string, Path>{{"baseline", base}, {"staged", bundle}}) {
      auto dir = work / arm / name;
      auto exe = work / ".test-programs" / (arm + "-" + name);
      const auto compiled = complete_file_plan(catalog, dir, spec, chain);
      build_emission(compiled, dir, exe, "g++-15");
      auto observations = array();
      for (const auto &fixture : trial["cases"]) {
        const auto case_name = fixture["name"].asString();
        for (const bool existing : {false, true}) {
          auto input =
              dir / "cases" / case_name / (existing ? "present" : "absent");
          save(input / "input", fixture["input"].asString());
          auto dest = input / "output";
          const std::string sentinel = "unchanged output\n";
          if (existing)
            save(dest, sentinel);
          auto cmd = runtime_sandbox(exe, input, catalog);
          append(cmd, {"/probe", "input", "output"});
          auto proc = execute(cmd, {.seconds = 10, .probe_limits = true});
          const bool failure = fixture.get("error", false).asBool();
          check(proc.status == (failure ? 1 : 0) &&
                    fs::exists(dest) == (!failure || existing),
                "unexpected emitted filter exit or output presence " +
                    case_name);
          J output;
          if (fs::exists(dest)) {
            check(bytes(dest) ==
                      (failure ? sentinel : fixture["output"].asString()),
                  "emitted filter changed output bytes " + case_name);
            output = bytes(dest);
          }
          observations.append(object({{"case", case_name},
                                      {"destination_existed", existing},
                                      {"exit", proc.status},
                                      {"output", output}}));
        }
      }
      result[arm] = object({{"source_sha256", digest(dir / "component.hpp")},
                            {"program_sha256", digest(exe)},
                            {"observations", observations}});
    }
    check(result["baseline"] == result["staged"],
          "retained filter differs between baseline and staged programs");
    report["trials"].append(result);
  }
  auto cut = work / "control-bundle";
  write_tree(cut, remove_binding(read_tree(bundle), row));
  auto removed = array();
  for (const auto &trial : spec["ordinary"]) {
    const auto result = plan(cut, goal(trial["parameters"]),
                             work / "removed" / trial["name"].asString());
    check(result["providers"].isArray() && result["providers"].empty() &&
              result["interpretation_errors"].empty() &&
              result["gaps"]["r"].asString().find(effect) != std::string::npos,
          "exact row filter removal left a provider chain");
    removed.append(result["gaps"]);
  }
  report["removed"] = object(
      {{"bundle_sha256", digest(cut / "SHA256SUMS")}, {"gaps", removed}});
  return report;
}

J observe(const Path &work, const Path &stage, const Path &bundle,
          const J &source) {
  const auto compiler_options = flags({"jsoncpp", "brick_shared_relational"});
  auto exe = work / ".test-programs/client";
  Strings command{"g++-15",           "-std=c++17",         "-O1", "-I",
                  stage / "original", stage / "client.cpp", "-o",  exe};
  append(command, compiler_options);
  run(command);
  auto env = path_client_environment(exe, file_qualification_tools());
  for (const auto *tool : {"gpgv", "bsdtar", "nm", "ldd"})
    env["tool_sha256"][tool] =
        digest(QStandardPaths::findExecutable(tool).toStdString());
  std::istringstream linked(
      run({"ldd", exe},
          {.environment = {{"LD_LIBRARY_PATH", (bundle / runtime).string()}}}));
  auto libraries = object();
  for (std::string line; std::getline(linked, line);) {
    std::istringstream fields(line);
    std::string name, arrow, path;
    if (fields >> name >> arrow >> path && arrow == "=>" &&
        (name == kernel || name == "libduckdb.so")) {
      check(fs::canonical(path) == fs::canonical(bundle / runtime / name),
            "standalone filter resolved the wrong retained runtime");
      libraries[name] = digest(path);
    }
  }
  check(libraries.size() == 2, "standalone filter lacks retained runtime");
  for (const auto &file : source["runtime_files"])
    if (libraries.isMember(file["path"].asString()))
      check(libraries[file["path"].asString()] == file["sha256"],
            "standalone library digest mismatch");
  env["retained_libraries"] = libraries;
  env["compiler_flags"] = string_array(compiler_options);
  write(stage / "environment.json", env);

  auto cmd = runtime_sandbox(exe, {}, bundle);
  append(cmd, {"--ro-bind", stage / "spec.json", "/spec", "/probe", "/spec"});
  const auto first = parse(run(cmd, {.seconds = 30, .probe_limits = true}));
  check(first["comparisons"].size() == 24 && first["errors"].size() == 8 &&
            parse(run(cmd, {.seconds = 30, .probe_limits = true})) == first,
        "standalone filter suite failed to reproduce");
  return object({{"fresh_processes", 2},
                 {"visits_per_comparison", 2},
                 {"observations", first}});
}
} // namespace

Path qualify_filter_records(const Path &package, const std::string &pin,
                            const Path &registry,
                            const std::string &registry_pin) {
  verify(package, pin);
  const auto src = sources()[source_id];
  const auto job = retained_binding_job(v5_root() / "sources.json", effect);
  check(src["approval"] == "local-pilot" &&
            src["method"] == "signed-local-package" &&
            job["source"] == source_id && job["route"] == "api" &&
            job["stream"] == "foundational" &&
            job["tests"] == "v5/fixtures/filter-records.json",
        "unapproved retained row filter job");
  auto priority = rank_supplied_usage(
      project_file(runs, job["coverage_audit"].asString()),
      job["coverage_sha256"].asString(),
      project_file(runs,
                   job["migration_snapshot"].asString() + "/ledger.sqlite")
          .parent_path(),
      job["ledger_sha256"].asString());
  const auto work = fresh_run("v5-filter-records"), stage = work / ".stage";
  const auto bundle = package / "bundle", base = verified_base();
  fs::create_directory(work / ".test-programs");
  save_file_registry(stage, src, registry, registry_pin);
  check(retained_binding_job(stage / "sources.json", effect) == job,
        "changed retained row filter job");
  const auto defs = load(bundle / "providers/contracts.json");
  const auto row = select_binding(defs["providers"], effect,
                                  "miner_filter_records", "rows", "rows");
  const auto record = file_binding_identity(row, bundle);
  const auto &selected = priority["first_pending"];
  const auto legacy = load(bundle / history / "contracts.json")[effect];
  check(selected["binding_id"] == effect &&
            selected["binding_sha256"] == record["binding_sha256"] &&
            selected["header_sha256"] == record["header_sha256"] &&
            row["call"] == job["callable"] &&
            row["args"] == array({"column", "type", "comparison", "value"}) &&
            defs["outcomes"][effect]["parameters"] == legacy["args"],
        "qualification differs from ranked ledger or comparison ABI");
  write(stage / "priority.json", priority);
  const auto source = verify_materials(stage, bundle, base, src, row);
  const auto tools = file_qualification_tools();
  for (const auto &[from, to] : std::map<std::string, std::string>{
           {"filter-records.json", "spec.json"},
           {"filter-records-client.cpp", "client.cpp"},
           {"filter-records-annotation.json", "annotation.json"}})
    fs::copy_file(v5_root() / "fixtures" / from, stage / to);
  const auto spec = load(stage / "spec.json");
  check(spec["binding"] == record["key"], "fixture names a different binding");
  const auto previous = qgetenv("PKG_CONFIG_PATH");
  const auto restore = qScopeGuard([&] {
    if (previous.isNull())
      qunsetenv("PKG_CONFIG_PATH");
    else
      qputenv("PKG_CONFIG_PATH", previous);
  });
  check(qputenv("PKG_CONFIG_PATH", (bundle / runtime / "pkgconfig").c_str()),
        "cannot pin retained filter runtime for compilation");
  write(stage / "standalone.json", observe(work, stage, bundle, source));
  write(stage / "integration.json", ordinary(work, bundle, base, row, spec));
  check(file_qualification_tools() == tools, "qualification tools changed");
  auto contract =
      file_contract(stage, package, record, src, stage / "sources.json");
  contract["source"]["implementation_sha256"] = source["kernel_sha256"];
  contract["qualification"]["scope"] =
      "Detached finite comparison fixtures and two ordinary TSV constructions "
      "with the recorded DuckDB runtime and provider ABI.";
  contract["fingerprint"]["scope"] = "Four typed operand modes crossed with "
                                     "six comparisons on ordered fixture rows.";
  validate(contract, load(package / "contract.schema.json"));
  write(stage / "contract.json", contract);
  write(stage / "domain.json",
        object({{"schema", "unllm.scoped-row-evidence-domain/1"},
                {"binding_key", record["key"]},
                {"raw_binding_sha256", record["binding_sha256"]},
                {"logical_id", record["logical_id"]},
                {"header_sha256", record["header_sha256"]},
                {"implementation_sha256", source["kernel_sha256"]},
                {"contract_sha256", digest(stage / "contract.json")},
                {"environment_sha256", digest(stage / "environment.json")},
                {"priority_sha256", digest(stage / "priority.json")},
                {"comparison_fixture_sha256", digest(stage / "spec.json")},
                {"modes", spec["modes"]},
                {"comparisons", spec["comparisons"]},
                {"ordinary_cases", spec["ordinary"]},
                {"scope",
                 "Detached observations for the named fixtures and "
                 "recorded implementation. No unrestricted row admission."}}));
  return finish_file_stage(work, package, pin, record["logical_id"].asString(),
                           "unllm.scoped-row-evidence/1");
}
} // namespace v5
