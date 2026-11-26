#include "bundle.hpp"
#include "migration_reports.hpp"
#include "pilot.hpp"
#include <QByteArray>

namespace v5 {
namespace {
const std::string id = "parent_paths", source_id = "retained-recursive-files";

J registry_job(const Path &registry) {
  auto job = retained_binding_job(registry, id);
  check(job["source"] == source_id && job["route"] == "direct" &&
            job["stream"] == "foundational" &&
            job["callable"] == "bricks::parent" &&
            job["tests"] == "v5/fixtures/parent-paths.json",
        "missing or ineligible parent-path job");
  return job;
}

J ordinary(const Path &work, const Path &bundle, const J &row, const J &cases,
           const J &upstream_basename) {
  auto spec = [](const std::string &effect) {
    return spec_for("text/plain", "application/json",
                    object({{"outcome", effect},
                            {"parameters", object()},
                            {"uncovered", array()}}));
  };
  const auto goal = spec(id),
             probes = sequence_requirements(cases, "expected")["cases"];
  J result = object({{"spec", goal}, {"cases", probes}});
  auto measure = [&](const Path &catalog, const std::string &arm,
                     const std::string &operation, const J &tests) {
    auto dir = work / arm, exe = work / ".test-programs" / arm;
    auto chain = array({"read_lines", operation, "write_texts"});
    auto p = complete_file_plan(catalog, dir, spec(operation), chain);
    build_emission(p, dir, exe, "g++-15");
    return object({{"chain", chain},
                   {"program_sha256", digest(exe)},
                   {"emission_sha256", digest(dir / "component.hpp")},
                   {"outcomes", ordinary_file_probes(exe, dir, tests)}});
  };
  result["baseline"] = measure(verified_base(), "baseline", id, probes);
  result["staged"] = measure(bundle, "staged", id, probes);
  auto cut = work / "control-bundle";
  write_tree(cut, remove_binding(read_tree(bundle), row));
  auto absent = plan(cut, goal, work / "removed");
  check(absent["interpretation_errors"].empty() &&
            absent["clarifications"].empty() && absent["providers"].isArray() &&
            absent["providers"].empty() &&
            absent["gaps"]["r"].asString().find(id) != std::string::npos,
        "removal did not isolate the missing parent operation: " +
            dump(absent));
  result["removed"] = object({{"gaps", absent["gaps"]},
                              {"bundle_sha256", digest(cut / "SHA256SUMS")}});
  auto witness = probes[0u];
  check(witness["json"] != upstream_basename,
        "sibling witness is indistinguishable");
  witness["json"] = upstream_basename;
  result["basename_control"] =
      measure(cut, "basename-control", "basenames", array({witness}));
  result["basename_control"]["case"] = witness;
  return result;
}
} // namespace

Path qualify_parent_paths(const Path &package, const std::string &pin,
                          const Path &registry,
                          const std::string &registry_pin) {
  verify(package, pin);
  auto src = sources()[source_id];
  check(src["approval"] == "local-pilot" &&
            digest(src["archive"].asString()) == src["sha256"].asString(),
        "unapproved or changed path operations archive");
  const auto job = registry_job(v5_root() / "sources.json");
  auto work = fresh_run("v5-parent-paths"), stage = work / ".stage",
       scratch = work / ".test-programs", bundle = package / "bundle";
  fs::create_directory(scratch);
  save_file_registry(stage, src, registry, registry_pin);
  check(job == registry_job(stage / "sources.json"), "changed parent-path job");
  auto source = retained_source(stage, source_id, "project-LICENSE");
  auto defs = load(bundle / "providers/contracts.json");
  const auto raw = select_binding(defs["providers"], id, "x29", "strings",
                                  "strings"),
             record = file_binding_identity(raw, bundle);
  check(raw["id"] == id && raw["args"] == array() &&
            raw["call"] == job["callable"] &&
            record["header_sha256"] ==
                digest(stage / "original/headers/path_ops.hpp"),
        "parent binding, callable or header changed");
  fs::copy_file(project_file(root, job["tests"].asString()),
                stage / "spec.json");
  for (const auto &[from, to] :
       std::vector<std::pair<std::string, std::string>>{
           {"parent-paths-client.cpp", "client.cpp"},
           {"parent-paths-annotation.json", "annotation.json"}})
    fs::copy_file(v5_root() / "fixtures" / from, stage / to);
  const auto spec = load(stage / "spec.json"), cases = spec["cases"];
  check(record["key"] == spec["binding"], "unreviewed parent ports");
  auto archived = [&](const std::string &field) {
    auto data = archive_member(src, src[field].asString());
    check(sha(data) == src[field + "_sha256"].asString(), "changed " + field);
    return data;
  };
  auto declaration = id + "|strings|strings|" + raw["call"].asString() + "||" +
                     defs["outcomes"][id]["meaning"].asString();
  check(archived("documentation").find("\n" + declaration + "\n") !=
            std::string::npos,
        "unmatched project operation declaration");
  auto examples = parse(archived("examples"));
  const auto &example = examples["cases"][id],
             &leaf = examples["cases"]["basenames"];
  auto sample = QByteArray::fromStdString(
      join(strings(cases[0]["records"]), "\n") + "\n");
  check(example["expected"] == cases[0]["expected"] &&
            example["source"] == leaf["source"] &&
            examples["files_hex"][example["source"].asString()] ==
                sample.toHex().toStdString(),
        "frozen case differs from archived project example");
  source["example"] = example;
  source["basename_example"] = leaf;
  source["declaration"] = declaration;
  source["binding"] = raw;
  source["job"] = job;
  write(stage / "source.json", source);
  auto tools = file_qualification_tools();
  auto exe = scratch / "direct";
  Strings cmd = {"g++-15",
                 "-std=c++17",
                 "-O1",
                 "-I",
                 stage / "original/headers",
                 stage / "client.cpp",
                 "-o",
                 exe};
  append(cmd, flags({"jsoncpp"}));
  run(cmd);
  RunOptions opts{.input = dump(spec), .seconds = 8, .probe_limits = true};
  auto direct = parse(run(sandbox(exe), opts));
  check(direct == parse(run(sandbox(exe), opts)) &&
            direct["constructed_triples"] == 35 &&
            direct["fixed"].size() == cases.size() &&
            direct["fixed"]["archived-example"]["basenames"] ==
                leaf["expected"],
        "incomplete direct parent or basename evidence");
  for (const auto &c : cases)
    check(direct["fixed"][c["name"].asString()]["parents"] == c["expected"],
          "parent observation disagrees with frozen fixture");
  direct["processes"] = 2;
  write(stage / "standalone.json", direct);
  write(stage / "integration.json",
        ordinary(work, bundle, raw, cases, leaf["expected"]));
  check(tools == file_qualification_tools(),
        "changed parent qualification environment");
  write(stage / "environment.json", path_client_environment(exe, tools));
  auto contract =
      file_contract(stage, package, record, src, stage / "sources.json");
  return seal_file_qualification(work, package, pin, array({record}), 0,
                                 contract);
}
} // namespace v5
