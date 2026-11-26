#include "bundle.hpp"
#include "migration_reports.hpp"
#include "pilot.hpp"
#include <QScopeGuard>
#include <QStandardPaths>
#include <algorithm>
#include <set>
#include <sstream>

namespace v5 {
namespace {
const Path runtime = "runtime/duckdb/lib";
const Path history = "qualifications/shared/relational";
const Path group_history = "qualifications/row-operations";

struct RowScope {
  std::string operation, source_id, fixture, output, unit;
  J arguments;
  std::string qualification, fingerprint;
  bool report() const { return operation == "report_fields"; }
  bool groups() const { return operation == "group_rows"; }
  bool selects() const { return operation == "select_group_row"; }
  bool aggregates() const { return operation == "aggregate_groups"; }
  bool annotates() const { return operation == "annotate_group_measure"; }
  bool derives() const { return operation == "derive_integer"; }
  bool group_transform() const { return selects() || aggregates(); }
  bool row_kernels() const { return groups() || group_transform(); }
  bool appends_fields() const { return annotates() || derives(); }
  bool renders_rows() const { return row_kernels() || appends_fields(); }
  bool filter() const { return !report() && !renders_rows(); }
  Path origin() const {
    if (row_kernels())
      return group_history;
    return annotates() ? Path("qualifications/shared/analytics") : history;
  }
  std::string kernel() const {
    if (row_kernels())
      return "libbrick_row_kernels.so";
    return "lib" + pkg_config() + ".so";
  }
  std::string pkg_config() const {
    if (row_kernels())
      return "brick-row-kernels";
    return annotates() ? "brick_shared_analytics" : "brick_shared_relational";
  }
};

J row_kernel_materials(const Path &stage, const Path &bundle, const Path &base,
                       const J &src, const J &debug, const J &row,
                       const RowScope &scope, J receipt) {
  const auto original = bundle / group_history / "native";
  const auto prior = load(original / "provenance.json");
  const auto &partition = prior["physical_partition"];
  auto library = bundle / runtime / scope.kernel();
  const auto symbols = run({"nm", "-D", "--defined-only", library});
  auto check_execution = [&](const std::string &unit, const J &symbol) {
    const auto &observed = prior["native_checks"]["executed_source_functions"][unit];
    check(symbols.find(" " + symbol.asString() + "\n") != std::string::npos &&
              std::find(observed.begin(), observed.end(), symbol) != observed.end(),
          "missing linked or historical execution evidence for " + unit);
  };
  const auto register_file = original / "register.cpp";
  check(prior["source"]["source_id"] == src["upstream_source_id"] &&
            prior["source"]["signer"] == src["signer"] &&
            prior["outputs"][scope.kernel()] == receipt["kernel_sha256"] &&
            prior["outputs"]["register.cpp"].asString() ==
                digest(register_file) &&
            partition["method"] == "FindOrCreateGroupsInternal" &&
            symbols.find(" brick_register_row_kernels\n") != std::string::npos,
        "group kernel provenance or linked symbols differ");
  const auto partition_unit = partition["source"].asString();
  check_execution(partition_unit, partition["symbol"]);
  const auto &factories = prior["factories"];
  std::set<std::string> units{partition_unit};
  auto verify_factory = [&](const char *name, const char *cls,
                            const char *method, const char *symbol) {
    const auto &factory = factories[name];
    check(factory["cls"] == cls && factory["method"] == method,
          "retained row factory changed: " + std::string(name));
    const auto unit = factory["source"].asString();
    check_execution(unit, symbol);
    units.insert(unit);
    return factory;
  };
  if (scope.selects()) {
    receipt["selection_factories"] = object(
        {{"brick_first", verify_factory("brick_first", "FirstFun", "GetFunctions",
                                        "_ZN6duckdb8FirstFun12GetFunctionsEv")},
         {"brick_last", verify_factory("brick_last", "LastFun", "GetFunctions",
                                       "_ZN6duckdb7LastFun12GetFunctionsEv")}});
  } else if (scope.aggregates()) {
    receipt["aggregate_factories"] = object(
        {{"brick_count", verify_factory("brick_count", "CountFun", "GetFunctions",
                                        "_ZN6duckdb8CountFun12GetFunctionsEv")},
         {"brick_sum", verify_factory("brick_sum", "SumFun", "GetFunctions",
                                      "_ZN6duckdb6SumFun12GetFunctionsEv")}});
  } else {
    receipt["list_factory"] = verify_factory("brick_list", "ListFun", "GetFunction",
                                              "_ZN6duckdb7ListFun11GetFunctionEv");
  }

  const auto members = src["source_units"].getMemberNames();
  check(members == Strings(units.begin(), units.end()),
        "reviewed grouped source closure differs");
  auto extracted = stage.parent_path() / ".signed-group-units";
  fs::create_directory(extracted);
  Strings command{"bsdtar", "-xf", debug["archive"].asString(), "-C",
                  extracted};
  const Path prefix = "usr/src/debug/duckdb/duckdb";
  for (const auto &member : members)
    command.push_back((prefix / member).string());
  run(command);
  for (const auto &member : members) {
    const auto pin = digest(extracted / prefix / member);
    check(pin == src["source_units"][member].asString() &&
              pin == prior["upstream_files"][member].asString() &&
              pin == digest(original / "original" / member),
          "signed DuckDB row kernel member changed: " + member);
    fs::copy_file(extracted / prefix / member,
                  stage / "original" / Path(member).filename());
  }
  fs::remove_all(extracted);

  Strings headers{row["header"].asString()};
  if (scope.group_transform())
    headers.push_back("miner_group_rows.hpp");
  for (const auto &header : headers) {
    const auto adapter = bytes(bundle / "headers" / header);
    check(adapter == bytes(base / "headers" / header) &&
              adapter == run({"git", "-C", root, "show",
                              src["adapter_revision"].asString() +
                                  ":row-ops/mined/include/" + header}),
          "retained row adapter differs from original: " + header);
    save(stage / "original" / header, adapter);
    if (scope.group_transform())
      receipt["adapter_headers"][header] = sha(adapter);
  }
  retain_provider_abi(stage, bundle, src);
  const auto sdk = archive_member(src, "usr/include/duckdb.h");
  check(sha(sdk) == prior["outputs"]["duckdb.h"].asString() &&
            sdk == bytes(bundle / "runtime/duckdb/include/duckdb.h"),
        "retained DuckDB C SDK header differs from signed binary package");
  save(stage / "original/duckdb.h", sdk);
  fs::copy_file(register_file, stage / "original/register.cpp");
  fs::copy_file(original / "provenance.json", stage / "legacy-source.json");
  fs::copy_file(bundle / group_history / "contracts.json",
                stage / "legacy-contracts.json");
  receipt["source_units"] = src["source_units"];
  receipt["partition"] = partition;
  receipt["sdk_header_sha256"] = sha(sdk);
  receipt["registration_sha256"] = digest(register_file);
  receipt["historical_instrumentation_sha256"] =
      digest(original / "provenance.json");
  return receipt;
}

J verify_materials(const Path &stage, const Path &bundle, const Path &base,
                   const J &src, const J &row, const RowScope &scope) {
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
  const auto packed = stage.parent_path() / "retained-duckdb.so";
  run({"bsdtar", "-xOf", archive, "usr/lib/libduckdb.so"}, {.output = packed});
  check(digest(packed) == digest(bundle / runtime / "libduckdb.so"),
        "retained DuckDB shared object differs from signed package");
  fs::remove(packed);
  const auto kernel = scope.kernel();
  const auto kernel_pin = digest(bundle / runtime / kernel);
  const auto links = runtime_files(
      bundle, base,
      {kernel, "libduckdb.so", "pkgconfig/" + scope.pkg_config() + ".pc"});
  auto result = object({{"binding", row},
                        {"source_id", scope.source_id},
                        {"kernel_sha256", kernel_pin},
                        {"arch_keyring_sha256", digest(ring)},
                        {"arch_signer", src["signer"]},
                        {"arch_buildinfo_sha256", sha(build)},
                        {"arch_debug_buildinfo_sha256", sha(debug_build)},
                        {"runtime_files", links}});
  fs::create_directories(stage / "original");
  if (scope.row_kernels()) {
    result = row_kernel_materials(stage, bundle, base, src, debug, row, scope, result);
    write(stage / "source.json", result);
    return result;
  }

  const auto origin = scope.origin();
  const auto provenance = load(bundle / origin / "source.json");
  const auto symbol = src["symbols"][0].asString();
  const auto unit = provenance["source_units"][symbol].asString();
  const auto definition = provenance["definitions"][symbol];
  Strings symbols{"nm", "--defined-only"};
  if (!scope.report())
    symbols.push_back("-D");
  symbols.push_back((bundle / runtime / kernel).string());
  check(provenance["source_id"] == src["upstream_source_id"] &&
            unit == scope.unit &&
            provenance["kernel_sha256"].asString() == kernel_pin &&
            definition["name"].isString() &&
            run(symbols).find(" " + definition["name"].asString() + "\n") !=
                std::string::npos,
        "retained DuckDB symbol, source unit or kernel differs");
  const auto source = stage / "original" / Path(unit).filename();
  run({"bsdtar", "-xOf", debug["archive"].asString(),
       "usr/src/debug/duckdb/duckdb/" + unit},
      {.output = source});
  const auto source_pin = digest(source);
  check(source_pin == src["source_unit_sha256"].asString() &&
            (!scope.appends_fields() ||
             source_pin == provenance["headers"][unit].asString()),
        "signed relational source member differs");
  retain_adapter(stage, bundle, origin, src, row, provenance,
                 scope.annotates() ? "shared/analytics/adapter.cpp"
                                   : "shared/relational/adapter.cpp");
  const auto table = bytes(bundle / origin / "table.hpp");
  check(table ==
            run({"git", "-C", root, "show",
                 src["adapter_revision"].asString() + ":shared/table.hpp"}),
        "retained table implementation differs from original");
  save(stage / "original/table.hpp", table);
  result["upstream_symbol"] = definition;
  result["source_unit"] = unit;
  result["source_unit_sha256"] = source_pin;
  write(stage / "source.json", result);
  return result;
}

J goal(const RowScope &scope, const J &trial, bool tsv = false) {
  auto steps = array();
  if (scope.group_transform())
    steps.append(object({{"outcome", "group_rows"}, {"parameters", trial["group"]}}));
  steps.append(object({{"outcome", scope.operation}, {"parameters", trial["parameters"]}}));
  if (scope.filter()) {
    steps.append(object({{"outcome", "ordered_column"},
                         {"parameters", object({{"column", 0}})}}));
  } else if (scope.renders_rows()) {
    if (scope.groups() && trial["count"].asBool())
      steps.append(
          object({{"outcome", "aggregate_groups"},
                  {"parameters", object({{"measure", "count"},
                                         {"value_column", 0},
                                         {"result_order", "group"}})}}));
    steps.append(
        object({{"outcome", "report_fields"},
                {"parameters", object({{"fields", trial["fields"]}})}}));
  }
  return spec_for("text/tab-separated-values",
                  tsv ? "text/tab-separated-values" : "text/plain",
                  object({{"operations", steps}, {"uncovered", array()}}));
}

J ordinary(const Path &work, const Path &bundle, const Path &base, const J &row,
           const J &spec, const RowScope &scope, const Path &tsv) {
  const auto chain =
      scope.report() ? array({"tab_records", "report_fields", "write_lines"})
                     : array({"tab_records", "op_0_filter_records",
                              "op_1_column", "write_lines"});
  auto report = object({{"trials", array()}});
  if (!scope.renders_rows())
    report["chain"] = chain;
  std::map<std::string, Path> arms{{"baseline", base}, {"staged", bundle}};
  auto tsv_chain = chain;
  if (!tsv.empty()) {
    arms.emplace("tsv", tsv);
    tsv_chain[tsv_chain.size() - 1] = "write_tsv_lines";
    report["tsv_chain"] = tsv_chain;
  }
  for (const auto &trial : spec["ordinary"]) {
    const auto name = trial["name"].asString();
    const auto spec = goal(scope, trial);
    auto result = object({{"name", name}, {"goal", spec}});
    auto expected = chain;
    if (scope.groups()) {
      expected =
          trial["count"].asBool()
              ? array({"tab_records", "op_0_group_rows",
                       "op_1_aggregate_groups", "op_2_report_fields",
                       "write_lines"})
              : array({"tab_records", "op_0_group_rows", "flatten_row_groups",
                       "op_1_report_fields", "write_lines"});
      result["chain"] = expected;
    } else if (scope.group_transform()) {
      expected = array({"tab_records", "op_0_group_rows", "op_1_" + scope.operation,
                        "op_2_report_fields", "write_lines"});
      result["chain"] = expected;
    } else if (scope.appends_fields()) {
      expected = array({"tab_records", "op_0_" + scope.operation,
                        "op_1_report_fields", "write_lines"});
      result["chain"] = expected;
    }
    for (const auto &[arm, catalog] : arms) {
      auto dir = work / arm / name;
      auto exe = work / ".test-programs" / (arm + "-" + name);
      const auto explicit_tsv = arm == "tsv";
      const auto selected_goal = explicit_tsv ? goal(scope, trial, true) : spec;
      if (explicit_tsv)
        result["tsv_goal"] = selected_goal;
      const auto compiled = complete_file_plan(
          catalog, dir, selected_goal, explicit_tsv ? tsv_chain : expected);
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
          auto cmd = duckdb_sandbox(exe, input, catalog);
          append(cmd, {"/probe", "input", "output"});
          auto proc = execute(cmd, {.seconds = 10, .probe_limits = true});
          const bool failure = fixture.get("error", false).asBool();
          check(proc.status == (failure ? 1 : 0) &&
                    fs::exists(dest) == (!failure || existing),
                "unexpected emitted relational exit or output presence " +
                    case_name);
          J output;
          if (fs::exists(dest)) {
            check(bytes(dest) ==
                      (failure ? sentinel : fixture["output"].asString()),
                  "emitted relational program changed output bytes " +
                      case_name);
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
          "retained program differs between baseline and staged programs");
    if (!tsv.empty())
      check(result["staged"]["observations"] == result["tsv"]["observations"],
            "explicit TSV differs from the retained plain writer");
    report["trials"].append(result);
  }
  auto removed = [&](const Path &catalog, bool explicit_tsv) {
    auto cut = work / "control-bundle";
    if (explicit_tsv)
      cut /= "tsv";
    write_tree(cut, remove_binding(read_tree(catalog), row));
    auto gaps = array();
    for (const auto &trial : spec["ordinary"]) {
      const auto result =
          plan(cut, goal(scope, trial, explicit_tsv),
               work / (explicit_tsv ? "removed-tsv" : "removed") /
                   trial["name"].asString());
      check(result["providers"].isArray() && result["providers"].empty() &&
                result["interpretation_errors"].empty() &&
                result["gaps"]["r"].asString().find(scope.operation) !=
                    std::string::npos,
            "exact relational row removal left a provider chain");
      gaps.append(result["gaps"]);
    }
    auto receipt =
        object({{"bundle_sha256", digest(cut / "SHA256SUMS")}, {"gaps", gaps}});
    if (scope.row_kernels()) {
      const auto providers =
          load(cut / "providers/contracts.json")["providers"];
      const auto keep = scope.group_transform() ? "group_rows" : "flatten_row_groups";
      auto sibling = std::find_if(providers.begin(), providers.end(), [&](const auto &p) {
        return p["id"] == keep && p["brick"] == "miner_group_rows";
      });
      check(sibling != providers.end(),
            "exact removal lost the retained group binding");
      receipt[scope.group_transform() ? "retained_grouping" : "retained_sibling"] = *sibling;
    }
    return receipt;
  };
  report["removed"] = removed(bundle, false);
  if (!tsv.empty())
    report["removed_tsv"] = removed(tsv, true);
  return report;
}

J observe(const Path &work, const Path &stage, const Path &bundle,
          const J &source, const J &spec, const RowScope &scope) {
  const auto compiler_options = flags({"jsoncpp", scope.pkg_config()});
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
  const auto kernel = scope.kernel();
  for (std::string line; std::getline(linked, line);) {
    std::istringstream fields(line);
    std::string name, arrow, path;
    if (fields >> name >> arrow >> path && arrow == "=>" &&
        (name == kernel || name == "libduckdb.so")) {
      check(fs::canonical(path) == fs::canonical(bundle / runtime / name),
            "standalone client resolved the wrong retained runtime");
      libraries[name] = digest(path);
    }
  }
  check(libraries.size() == 2, "standalone client lacks retained runtime");
  for (const auto &file : source["runtime_files"])
    if (libraries.isMember(file["path"].asString()))
      check(libraries[file["path"].asString()] == file["sha256"],
            "standalone library digest mismatch");
  env["retained_libraries"] = libraries;
  env["compiler_flags"] = string_array(compiler_options);
  write(stage / "environment.json", env);

  auto cmd = duckdb_sandbox(exe, {}, bundle);
  append(cmd, {"--ro-bind", stage / "spec.json", "/spec", "/probe", "/spec"});
  const auto first = parse(run(cmd, {.seconds = 30, .probe_limits = true}));
  check(first["errors"].size() == spec["errors"].size() &&
            parse(run(cmd, {.seconds = 30, .probe_limits = true})) == first,
        "standalone relational suite failed to reproduce");
  if (!scope.filter()) {
    const auto cases = scope.derives() ? "derivations" :
                       scope.annotates() ? "annotations" :
                       scope.aggregates() ? "aggregates" :
                       scope.selects() ? "selections" : scope.groups() ? "groupings" : "reports";
    const auto calls = scope.derives() ? "calls_per_derivation" :
                       scope.annotates() ? "calls_per_annotation" :
                       scope.selects() ? "calls_per_selection" :
                       scope.groups() ? "calls_per_grouping" :
                       scope.aggregates() ? "calls_per_aggregate" : "calls_per_report";
    check(first[cases].size() ==
              spec["direct"].size(),
          "incomplete direct relational observations");
    return object(
        {{"fresh_processes", 2},
         {calls, 2},
         {"observations", first}});
  }
  check(first["comparisons"].size() == 24,
        "incomplete direct filter observations");
  return object({{"fresh_processes", 2},
                 {"visits_per_comparison", 2},
                 {"observations", first}});
}
J detached_manifest(const Path &stage, const std::string &pin,
                    const std::string &parent, const std::string &schema) {
  const auto raw = bytes(stage / "manifest.json");
  const auto manifest = parse(raw);
  check(sha(raw) == pin && manifest["schema"] == schema.c_str() &&
            manifest["catalog_sha256"].asString() == parent &&
            manifest["active_manifest_sha256"] == base_hash.c_str() &&
            manifest["files"].isObject() && !manifest["files"].empty(),
        "detached evidence manifest or catalog changed");
  for (const auto &member : manifest["files"].getMemberNames())
    check(digest(project_file(stage, member)) ==
              manifest["files"][member].asString(),
          "detached evidence file changed: " + member);
  return manifest;
}

void check_test_receipt(const Path &stage, const J &files) {
  check(files.isMember("tested-programs.json"),
        "missing sealed predecessor member: tested-programs.json");
  const auto tested = load(stage / "tested-programs.json");
  for (const auto *name : {"standalone", "integration", "environment", "spec"}) {
    const auto file = std::string(name) + ".json";
    check(files.isMember(file) && tested[std::string(name) + "_sha256"] == files[file],
          "detached test receipt differs from sealed " + file);
  }
  check(tested["verdict"] == "passed", "detached evidence has no passing receipt");
}

J ranked_parent_binding(const J &raw, const Path &bundle, const J &ranked) {
  const auto rows = load(bundle / "providers/contracts.json")["providers"];
  check(std::count(rows.begin(), rows.end(), raw) == 1,
        "detached binding has no unique parent row");
  const auto expected = file_binding_identity(raw, bundle);
  const auto &key = expected["key"];
  check(key["binding_id"] == ranked["binding_id"] &&
            key["component_id"] == ranked["component_id"] &&
            key["from"] == ranked["from"] && key["to"] == ranked["to"] &&
            expected["binding_sha256"] == ranked["binding_sha256"] &&
            expected["header_sha256"] == ranked["header_sha256"],
        "detached binding differs from the next ranked binding");
  return expected;
}

J binding_predecessor(const Path &stage, const Path &package, const J &manifest,
                      const J &ranked) {
  const auto &files = manifest["files"];
  for (const auto *name : {"payload.json", "contract.json", "source.json",
                          "bindings.sqlite"})
    check(files.isMember(name), "missing sealed predecessor member: " + std::string(name));
  check_test_receipt(stage, files);
  const auto payload = load(stage / "payload.json");
  const auto contract = load(stage / "contract.json");
  const auto raw = load(stage / "source.json")["binding"];
  auto expected = ranked_parent_binding(raw, package / "bundle", ranked);
  const auto &key = expected["key"];
  expected["contract_sha256"] = files["contract.json"];
  expected["qualification"] = "ordinary-emission-tested";
  const auto &bindings = payload["bindings"];
  check(payload["schema"] == "unllm.staged-binding-qualifications/1" &&
            payload["catalog_sha256"] == manifest["catalog_sha256"] &&
            payload["contract_file"] == "contract.json" &&
            payload["qualified_logical_id"] == expected["logical_id"] &&
            manifest["binding_id"] == expected["logical_id"] &&
            std::count(bindings.begin(), bindings.end(), expected) == 1,
        "detached binding differs from the next ranked binding");
  const auto capability = raw["effect"].isNull() ? key["binding_id"] : raw["effect"];
  check(contract["provider_id"] == key["component_id"] &&
            contract["capability_id"] == capability &&
            contract["interface"]["callable"] == expected["call"] &&
            contract["interface"]["structural_input"] == key["from"] &&
            contract["interface"]["structural_output"] == key["to"] &&
            contract["source"]["implementation_sha256"] == expected["header_sha256"] &&
            contract["qualification"]["level"] == expected["qualification"],
        "detached qualification does not describe the exact ranked binding");
  verify_binding_records(stage, bindings);
  return expected;
}

J file_predecessor(const Path &stage, const Path &package, const J &manifest,
                   const J &ranked, const J &scope) {
  const auto &files = manifest["files"];
  for (const auto *name : {"domain.json", "contract.json", "source.json"})
    check(files.isMember(name), "missing sealed predecessor member: " + std::string(name));
  check_test_receipt(stage, files);
  const auto source = load(stage / "source.json");
  const auto raw = source["binding"];
  const auto bundle = package / "bundle";
  const auto binding = ranked_parent_binding(raw, bundle, ranked);
  const auto &key = binding["key"];
  const auto contract = load(stage / "contract.json");
  const auto spec = load(stage / "spec.json");
  const auto domain = load(stage / "domain.json");
  unsigned kernels = 0;
  for (const auto &file : source["runtime_files"]) {
    check(digest(project_file(bundle / runtime, file["path"].asString())) ==
              file["sha256"].asString(),
          "scoped reader runtime differs from the parent");
    if (file["sha256"] == source["kernel_sha256"])
      ++kernels;
  }
  check(kernels == 1, "scoped reader lacks a unique pinned parent kernel");
  check(scope["parameter"].isString() && scope["value"].isString() &&
            scope["argument_index"] == 1 && scope["argument_type"] == "const std::string&" &&
            raw["args"] == array({scope["parameter"]}) &&
            spec["binding"] == key && spec["parameters"][scope["parameter"].asString()] == scope["value"],
        "scoped file fixture differs from approved selector");
  check(domain["schema"] == "unllm.scoped-file-evidence-domain/1" &&
            domain["binding_key"] == key && domain["call"] == binding["call"] &&
            domain["logical_id"] == binding["logical_id"] &&
            manifest["binding_id"] == binding["logical_id"] &&
            domain["raw_binding_sha256"] == binding["binding_sha256"] &&
            domain["header_sha256"] == binding["header_sha256"] &&
            domain["contract_sha256"] == files["contract.json"] &&
            domain["environment_sha256"] == files["environment.json"] &&
            domain["implementation_sha256"] == source["kernel_sha256"] &&
            domain["quantifier"] == "argument" &&
            domain["argument_index"] == scope["argument_index"] &&
            domain["argument_type"] == scope["argument_type"] &&
            domain["all_of"] == array({object({{"id", "exact_text"}, {"value", scope["value"]}})}),
        "sealed file scope differs from the exact reviewed binding or selector");
  check(contract["provider_id"] == key["component_id"] &&
            contract["capability_id"] == raw["effect"] &&
            contract["interface"]["callable"] == binding["call"] &&
            contract["interface"]["structural_input"] == key["from"] &&
            contract["interface"]["structural_output"] == key["to"] &&
            contract["source"]["id"] == source["source_id"] &&
            contract["source"]["implementation_sha256"] == domain["implementation_sha256"] &&
            contract["qualification"]["level"] == "ordinary-emission-tested",
        "scoped file contract differs from the reviewed reader");
  return object({{"binding_id", key["binding_id"]},
                 {"logical_id", binding["logical_id"]},
                 {"domain_sha256", files["domain.json"]},
                 {"contract_sha256", files["contract.json"]},
                 {"selector", scope},
                 {"disposition", "scoped evidence reconciled"}});
}

J json_writer_predecessor(const Path &stage, const Path &package,
                          const J &manifest, const J &ranked,
                          const J &scope) {
  const auto &files = manifest["files"];
  for (const auto *name : {"source.json", "domain.json", "contract.json"})
    check(files.isMember(name),
          "missing sealed predecessor member: " + std::string(name));
  const auto source = load(project_file(stage, "source.json"));
  const auto domain = load(project_file(stage, "domain.json"));
  const auto contract = load(project_file(stage, "contract.json"));
  const auto env = load(project_file(stage, "environment.json"));
  const auto fixture = load(project_file(stage, "spec.json"));
  check_test_receipt(stage, files);
  const auto raw = source["binding"];
  const auto binding = ranked_parent_binding(raw, package / "bundle", ranked);
  const auto &key = binding["key"];
  const auto header = "original/headers/" + raw["header"].asString();
  const auto provenance = sources()["retained-json-text-writer"];
  check(key["binding_id"] == "write_texts" && key["from"] == "strings" &&
            key["to"] == "out:application/json" && raw["destination_first"] == true &&
            raw["effect"].isNull() && raw["args"].isNull() &&
            fixture["binding"] == key && files[header] == binding["header_sha256"] &&
            source["source_id"] == provenance["id"] &&
            source["runtime"] == provenance["runtime"] &&
            env["linked_json_c_sha256"] ==
                provenance["runtime"]["files"]["usr/lib/libjson-c.so.5.5.0"],
        "scoped JSON writer implementation or tested runtime differs");
  const auto predicates = array(
      {object({{"id", "valid_utf8"}, {"value", true}}),
       object({{"id", "max_bytes_inclusive"}, {"value", env["int_max"]}})});
  check(env["int_max"].isInt() && env["int_max"].asInt() > 0 &&
            scope == object({{"quantifier", "each_record"}, {"argument_index", 1},
                             {"argument_type", "const std::vector<std::string>&"},
                             {"all_of", predicates}}) &&
            domain["schema"] == "unllm.scoped-file-evidence-domain/1" &&
            domain["binding_key"] == key && domain["logical_id"] == manifest["binding_id"] &&
            domain["logical_id"] == binding["logical_id"] &&
            domain["raw_binding_sha256"] == binding["binding_sha256"] &&
            domain["implementation_sha256"] == binding["header_sha256"] &&
            domain["call"] == binding["call"] &&
            domain["contract_sha256"] == files["contract.json"] &&
            domain["environment_sha256"] == files["environment.json"] &&
            domain["argument_index"] == scope["argument_index"] &&
            domain["argument_type"] == scope["argument_type"] &&
            domain["quantifier"] == scope["quantifier"] &&
            domain["all_of"] == predicates,
        "sealed JSON writer domain differs from the reviewed per-record scope");
  check(contract["provider_id"] == key["component_id"] &&
            contract["capability_id"] == key["binding_id"] &&
            contract["interface"]["callable"] == binding["call"] &&
            contract["interface"]["structural_input"] == key["from"] &&
            contract["interface"]["structural_output"] == key["to"] &&
            contract["source"]["id"] == provenance["id"] &&
            contract["source"]["implementation_sha256"] == binding["header_sha256"] &&
            contract["qualification"]["level"] == "ordinary-emission-tested",
        "scoped file contract differs from the reviewed JSON writer");
  return object({{"binding_id", key["binding_id"]},
                 {"logical_id", binding["logical_id"]},
                 {"domain_sha256", files["domain.json"]},
                 {"contract_sha256", files["contract.json"]},
                 {"record_scope", scope},
                 {"disposition", "scoped evidence reconciled"}});
}
} // namespace

J select_ranked_scope(const Path &package, const std::string &parent,
                      const J &priority, const J &job) {
  auto next = priority["bindings"].begin();
  const auto end = priority["bindings"].end();
  const auto pending = [](const auto &row) {
    return row["disposition"] == "structural inventory pending qualification";
  };
  auto reviewed = array();
  for (const auto &previous : job["previous_scopes"]) {
    next = std::find_if(next, end, pending);
    check(next != end, "previous evidence exceeds ranked pending bindings");
    const auto stage =
        project_file(runs, previous["manifest"].asString()).parent_path();
    const auto pin = previous["sha256"].asString();
    const auto schema = previous.get("schema", "unllm.scoped-row-evidence/1").asString();
    check(schema == "unllm.scoped-row-evidence/1" ||
              schema == "unllm.scoped-file-evidence/1" ||
              schema == "unllm.binding-qualification-stage/1",
          "unsupported ranked predecessor schema");
    const auto sealed = detached_manifest(stage, pin, parent, schema);
    if (schema == "unllm.binding-qualification-stage/1") {
      const auto record = binding_predecessor(stage, package, sealed, *next);
      reviewed.append(object({{"binding_id", (*next)["binding_id"]},
                              {"manifest_sha256", pin},
                              {"logical_id", record["logical_id"]},
                              {"contract_sha256", record["contract_sha256"]},
                              {"disposition", "scoped evidence reconciled"}}));
      ++next;
      continue;
    }
    if (schema == "unllm.scoped-file-evidence/1") {
      auto record = previous.isMember("record_scope")
                        ? json_writer_predecessor(stage, package, sealed, *next,
                                                  previous["record_scope"])
                        : file_predecessor(stage, package, sealed, *next,
                                           previous["selector"]);
      record["manifest_sha256"] = pin;
      reviewed.append(record);
      ++next;
      continue;
    }
    const auto domain = load(stage / "domain.json");
    const auto contract = load(stage / "contract.json");
    check(
        domain["binding_key"]["binding_id"] == (*next)["binding_id"] &&
            domain["binding_key"]["component_id"] == (*next)["component_id"] &&
            domain["raw_binding_sha256"] == (*next)["binding_sha256"] &&
            domain["header_sha256"] == (*next)["header_sha256"] &&
            domain["logical_id"] == sealed["binding_id"] &&
            domain["contract_sha256"] == sealed["files"]["contract.json"] &&
            domain["priority_sha256"] == sealed["files"]["priority.json"] &&
            contract["qualification"]["level"] == "ordinary-emission-tested" &&
            load(stage / "priority.json") == priority &&
            load(stage / "tested-programs.json")["verdict"] == "passed",
        "reviewed predecessor differs from the next ranked binding");
    reviewed.append(
        object({{"binding_id", (*next)["binding_id"]},
                {"manifest_sha256", pin},
                {"domain_sha256", sealed["files"]["domain.json"]}}));
    ++next;
  }
  next = std::find_if(next, end, pending);
  check(next != end, "no further ranked pending binding");
  return object({{"selected", *next}, {"reviewed_predecessors", reviewed}});
}

namespace {
Path qualify_rows(const Path &package, const std::string &pin,
                  const Path &registry, const std::string &registry_pin,
                  const RowScope &scope) {
  verify(package, pin);
  const auto src = sources()[scope.source_id];
  const auto job =
      retained_binding_job(v5_root() / "sources.json", scope.operation);
  check(src["approval"] == "local-pilot" &&
            src["method"] == "signed-local-package" &&
            job["source"] == scope.source_id.c_str() &&
            job["route"] == (scope.renders_rows() ? "direct" : "api") &&
            job["stream"] == "foundational" &&
            job["tests"].asString() == "v5/fixtures/" + scope.fixture + ".json",
        "unapproved retained relational job");
  auto priority = rank_supplied_usage(
      project_file(runs, job["coverage_audit"].asString()),
      job["coverage_sha256"].asString(),
      project_file(runs,
                   job["migration_snapshot"].asString() + "/ledger.sqlite")
          .parent_path(),
      job["ledger_sha256"].asString());
  const auto work = fresh_run("v5-" + scope.fixture), stage = work / ".stage";
  const auto bundle = package / "bundle", base = verified_base();
  fs::create_directory(work / ".test-programs");
  save_file_registry(stage, src, registry, registry_pin);
  check(retained_binding_job(stage / "sources.json", scope.operation) == job,
        "changed retained relational job");
  const auto defs = load(bundle / "providers/contracts.json");
  const auto row =
      select_binding(defs["providers"], scope.operation,
                     "miner_" + scope.operation,
                     scope.group_transform() ? "row_groups" : "rows", scope.output);
  const auto record = file_binding_identity(row, bundle);
  const auto selection = select_ranked_scope(package, pin, priority, job);
  const auto &selected = selection["selected"];
  const auto legacy = load(bundle / scope.origin() / "contracts.json")[scope.operation];
  check(selected["binding_id"] == scope.operation.c_str() &&
            selected["binding_sha256"] == record["binding_sha256"] &&
            selected["header_sha256"] == record["header_sha256"] &&
            row["call"] == job["callable"] && row["args"] == scope.arguments &&
            defs["outcomes"][scope.operation]["parameters"] == legacy["args"],
        "qualification differs from ranked ledger or relational ABI");
  write(stage / "priority.json", priority);
  Path tsv;
  if (scope.report()) {
    const auto tsv_stage =
        project_file(runs, job["tsv_manifest"].asString()).parent_path();
    const auto tsv_manifest =
        detached_manifest(tsv_stage, job["tsv_manifest_sha256"].asString(), pin,
                          "unllm.scoped-file-evidence/1");
    tsv = tsv_stage / "bundle";
    const auto writer = load(tsv_stage / "source.json")["binding"];
    check(writer["id"] == "write_tsv_lines" && writer["effect"].isNull() &&
              writer["from"] == "strings" &&
              writer["to"] == "out:text/tab-separated-values" &&
              remove_binding(read_tree(tsv), writer).at("SHA256SUMS") ==
                  bytes(bundle / "SHA256SUMS"),
          "explicit TSV arm differs outside the retained writer registration");
    auto qualified_selection = selection;
    qualified_selection["tsv_context"] =
        object({{"manifest_sha256", job["tsv_manifest_sha256"]},
                {"bundle_sha256", tsv_manifest["files"]["bundle/SHA256SUMS"]},
                {"writer", writer}});
    write(stage / "selection.json", qualified_selection);
  } else if (scope.renders_rows()) {
    write(stage / "selection.json", selection);
  }
  const auto source = verify_materials(stage, bundle, base, src, row, scope);
  const auto tools = file_qualification_tools();
  for (const auto &[from, to] : std::map<std::string, std::string>{
           {scope.fixture + ".json", "spec.json"},
           {scope.fixture + "-client.cpp", "client.cpp"},
           {scope.fixture + "-annotation.json", "annotation.json"}})
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
        "cannot pin retained relational runtime for compilation");
  write(stage / "standalone.json",
        observe(work, stage, bundle, source, spec, scope));
  write(stage / "integration.json",
        ordinary(work, bundle, base, row, spec, scope, tsv));
  check(file_qualification_tools() == tools, "qualification tools changed");
  auto contract =
      file_contract(stage, package, record, src, stage / "sources.json");
  contract["source"]["implementation_sha256"] = source["kernel_sha256"];
  contract["qualification"]["scope"] = scope.qualification;
  contract["fingerprint"]["scope"] = scope.fingerprint;
  validate(contract, load(package / "contract.schema.json"));
  write(stage / "contract.json", contract);
  auto domain = object(
      {{"schema", "unllm.scoped-row-evidence-domain/1"},
       {"binding_key", record["key"]},
       {"raw_binding_sha256", record["binding_sha256"]},
       {"logical_id", record["logical_id"]},
       {"header_sha256", record["header_sha256"]},
       {"implementation_sha256", source["kernel_sha256"]},
       {"contract_sha256", digest(stage / "contract.json")},
       {"environment_sha256", digest(stage / "environment.json")},
       {"priority_sha256", digest(stage / "priority.json")},
       {"ordinary_cases", spec["ordinary"]},
       {"scope", "Detached observations for the named fixtures and "
                 "recorded implementation. No unrestricted row admission."}});
  if (!scope.filter()) {
    domain["selector_fixture_sha256"] = digest(stage / "spec.json");
    domain["selection_sha256"] = digest(stage / "selection.json");
    if (scope.renders_rows()) {
      const auto kind = scope.derives() ? "checked_integer_derivation" :
                        scope.annotates() ? "group_measure_annotation" :
                        scope.aggregates() ? "group_aggregation" :
                        scope.selects() ? "row_selection" : "grouping";
      domain[kind] = object(
          {{"arguments", scope.arguments}, {"direct_cases", spec["direct"]}});
    } else {
      domain["selector"] = object({{"argument_index", 1},
                                   {"argument_type", "const std::string&"},
                                   {"direct_cases", spec["direct"]}});
    }
  } else {
    domain["comparison_fixture_sha256"] = digest(stage / "spec.json");
    domain["modes"] = spec["modes"];
    domain["comparisons"] = spec["comparisons"];
  }
  write(stage / "domain.json", domain);
  return finish_file_stage(work, package, pin, record["logical_id"].asString(),
                           "unllm.scoped-row-evidence/1");
}
} // namespace

Path qualify_filter_records(const Path &package, const std::string &pin,
                            const Path &registry,
                            const std::string &registry_pin) {
  return qualify_rows(
      package, pin, registry, registry_pin,
      {"filter_records", "retained-relational-filter", "filter-records", "rows",
       "src/execution/operator/filter/physical_filter.cpp",
       array({"column", "type", "comparison", "value"}),
       "Detached finite comparison fixtures and two ordinary TSV constructions "
       "with the recorded DuckDB runtime and provider ABI.",
       "Four typed operand modes crossed with six comparisons on ordered "
       "fixture rows."});
}

Path qualify_report_fields(const Path &package, const std::string &pin,
                           const Path &registry,
                           const std::string &registry_pin) {
  return qualify_rows(
      package, pin, registry, registry_pin,
      {"report_fields", "retained-relational-report", "report-fields",
       "strings", "src/function/scalar/string/concat_ws.cpp", array({"fields"}),
       "Detached field-selector fixtures and ordinary line and TSV emission "
       "with the recorded DuckDB runtime and provider ABI.",
       "Exact field and position rendering on the frozen ordered fixture "
       "rows."});
}

Path qualify_group_rows(const Path &package, const std::string &pin,
                        const Path &registry, const std::string &registry_pin) {
  return qualify_rows(
      package, pin, registry, registry_pin,
      {"group_rows", "retained-group-rows", "group-rows", "row_groups", "",
       array({"keys", "group_order"}),
       "Detached finite grouping fixtures and two ordinary constructions using "
       "the recorded DuckDB row kernels and provider ABI.",
       "Exact selected key tuples, group order and positioned member rows on "
       "the "
       "frozen grouping fixtures."});
}

Path qualify_select_group_row(const Path &package, const std::string &pin,
                             const Path &registry, const std::string &registry_pin) {
  return qualify_rows(
      package, pin, registry, registry_pin,
      {"select_group_row", "retained-select-group-row", "select-group-row", "rows", "",
       array({"which", "order_by", "result_order"}),
       "Detached positional and ordered selection fixtures through the pinned DuckDB row kernels.",
       "Entire selected rows and signed positions across checked encounter, numeric and byte orderings."});
}

Path qualify_aggregate_groups(const Path &package, const std::string &pin,
                              const Path &registry, const std::string &registry_pin) {
  return qualify_rows(
      package, pin, registry, registry_pin,
      {"aggregate_groups", "retained-aggregate-groups", "aggregate-groups", "rows", "",
       array({"measure", "value_column", "result_order"}),
       "Detached count, distinct byte count and checked signed sum fixtures on the pinned DuckDB row kernels.",
       "Exact group keys, decimal results, output positions, value order and failure outcomes."});
}

Path qualify_annotate_group_measure(const Path &package, const std::string &pin,
                                   const Path &registry, const std::string &registry_pin) {
  return qualify_rows(
      package, pin, registry, registry_pin,
      {"annotate_group_measure", "retained-annotate-group-measure",
       "annotate-group-measure", "rows", "src/function/window/window_aggregate_function.cpp",
       array({"keys", "column", "measure", "value"}),
       "Detached count-equal, min, max and lower-median fixtures on the pinned DuckDB window adapter.",
       "Exact original fields and positions, encounter order, appended partition measures and failure outcomes."});
}

Path qualify_derive_integer(const Path &package, const std::string &pin,
                            const Path &registry, const std::string &registry_pin) {
  return qualify_rows(
      package, pin, registry, registry_pin,
      {"derive_integer", "retained-derive-integer", "derive-integer", "rows",
       "src/execution/operator/projection/physical_projection.cpp",
       array({"column", "operation", "operand"}),
       "Detached signed add, subtract, divide and absolute-value fixtures on the pinned DuckDB projection adapter.",
       "Exact original fields and positions, signed decimal results, and failure outcomes."});
}
} // namespace v5
