#include "bundle.hpp"
#include "migration_reports.hpp"
#include "pilot.hpp"
#include <QByteArray>

namespace v5 {
namespace {
const std::string effect = "filter_extension";
const std::string source_id = "retained-recursive-files";

J extension_goal(const std::string &value) {
  return spec_for("text/plain", "application/json",
                  object({{"outcome", effect},
                          {"parameters", object({{"extension", value}})},
                          {"uncovered", array()}}));
}

J ordinary(const Path &work, const Path &bundle, const J &row, const J &cases) {
  std::map<std::string, J> groups;
  for (const auto &fixture : cases)
    if (!fixture.get("standalone_only", false).asBool())
      groups[fixture["extension"].asString()].append(fixture);
  const auto chain = array({"read_lines", effect, "write_texts"});
  auto report = object({{"chain", chain}, {"trials", array()}});
  const auto base = verified_base();
  unsigned trial = 0;
  for (const auto &[value, group] : groups) {
    const auto spec = extension_goal(value);
    auto tests = sequence_requirements(group, "expected")["cases"];
    if (value != ".csv")
      tests.resize(group.size());
    J entry = object({{"spec", spec}, {"cases", tests}});
    for (const auto &[arm, catalog] : std::vector<std::pair<std::string, Path>>{
             {"baseline", base}, {"staged", bundle}}) {
      auto name = arm + "-" + std::to_string(trial);
      auto dest = work / name, exe = work / ".test-programs" / name;
      auto plan = complete_file_plan(catalog, dest, spec, chain);
      build_emission(plan, dest, exe, "g++-15");
      entry[arm] =
          object({{"source_sha256", digest(dest / "component.hpp")},
                  {"program_sha256", digest(exe)},
                  {"outcomes", ordinary_file_probes(exe, dest, tests)}});
    }
    check(entry["baseline"] == entry["staged"],
          "retained filter differs on active and staged bundles");
    report["trials"].append(entry);
    ++trial;
  }
  const auto cut = work / "control-bundle";
  write_tree(cut, remove_binding(read_tree(bundle), row));
  const auto absent = plan(cut, extension_goal(".csv"), work / "removed");
  check(absent["interpretation_errors"].empty() &&
            absent["clarifications"].empty() && absent["providers"].isArray() &&
            absent["providers"].empty() &&
            absent["gaps"]["r"].asString().find(effect) != std::string::npos,
        "removal did not block the fixed extension goal");
  report["removed"] = object({{"gaps", absent["gaps"]},
                              {"bundle_sha256", digest(cut / "SHA256SUMS")}});
  return report;
}

std::string fold_ascii(std::string value) {
  for (auto &ch : value)
    if (ch >= 'A' && ch <= 'Z')
      ch = char(ch - 'A' + 'a');
  return value;
}

J negative_controls(const J &cases, const J &direct) {
  J conflicts = object({{"fold_case", array()}, {"text_suffix", array()}});
  for (const auto &fixture : cases) {
    const auto name = fixture["name"].asString();
    const auto selector = fixture["extension"].asString();
    auto folded = array(), text_suffix = array();
    for (const auto &item : fixture["records"]) {
      auto path = item.asString();
      if (fold_ascii(fs::path(path).extension().string()) ==
          fold_ascii(selector))
        folded.append(item);
      if (path.ends_with(selector))
        text_suffix.append(item);
    }
    if (folded != direct[name])
      conflicts["fold_case"].append(
          object({{"case", name}, {"alternative", folded}}));
    if (text_suffix != direct[name])
      conflicts["text_suffix"].append(
          object({{"case", name}, {"alternative", text_suffix}}));
  }
  check(!conflicts["fold_case"].empty() && !conflicts["text_suffix"].empty(),
        "fixtures failed to discriminate extension semantics");
  return conflicts;
}
} // namespace

Path qualify_filter_extension(const Path &package, const std::string &pin,
                              const Path &registry,
                              const std::string &registry_pin) {
  verify(package, pin);
  auto src = sources()[source_id];
  check(src["approval"] == "local-pilot" &&
            digest(src["archive"].asString()) == src["sha256"].asString(),
        "unapproved or changed path archive");
  const auto job = retained_binding_job(v5_root() / "sources.json", effect);
  check(job["source"] == source_id && job["route"] == "direct" &&
            job["stream"] == "foundational" &&
            job["callable"] == "bricks::suffix" &&
            job["tests"] == "v5/fixtures/filter-extension.json",
        "missing or ineligible extension filter job");
  const auto work = fresh_run("v5-filter-extension"), stage = work / ".stage",
             scratch = work / ".test-programs", bundle = package / "bundle";
  fs::create_directory(scratch);
  save_file_registry(stage, src, registry, registry_pin);
  check(job == retained_binding_job(stage / "sources.json", effect),
        "changed extension filter job");
  auto source = retained_source(stage, source_id, "project-LICENSE");
  const auto defs = load(bundle / "providers/contracts.json");
  const auto raw =
      select_binding(defs["providers"], effect, "x25", "strings", "strings");
  const auto record = file_binding_identity(raw, bundle);
  check(raw["id"] == effect && raw["args"] == array({"extension"}) &&
            raw["call"] == job["callable"] &&
            defs["outcomes"][effect]["parameters"] ==
                object({{"extension", "text"}}) &&
            record["header_sha256"] ==
                digest(stage / "original/headers/path_ops.hpp"),
        "extension filter binding, parameter or header changed");
  fs::copy_file(project_file(root, job["tests"].asString()),
                stage / "spec.json");
  fs::copy_file(v5_root() / "fixtures/filter-extension-client.cpp",
                stage / "client.cpp");
  fs::copy_file(v5_root() / "fixtures/filter-extension-annotation.json",
                stage / "annotation.json");
  const auto spec = load(stage / "spec.json"), cases = spec["cases"];
  check(record["key"] == spec["binding"], "unreviewed extension filter ports");
  auto archived = [&](const std::string &field) {
    auto data = archive_member(src, src[field].asString());
    check(sha(data) == src[field + "_sha256"].asString(), "changed " + field);
    return data;
  };
  auto declaration = effect + "|strings|strings|" + raw["call"].asString() +
                     "|extension:text|" +
                     defs["outcomes"][effect]["meaning"].asString();
  check(archived("documentation").find("\n" + declaration + "\n") !=
            std::string::npos,
        "unmatched project operation declaration");
  const auto examples = parse(archived("examples"));
  const auto example = examples["cases"][effect];
  const auto sample = QByteArray::fromStdString(
      join(strings(cases[0]["records"]), "\n") + "\n");
  check(example["expected"] == cases[0]["expected"] &&
            example["params"]["extension"] == cases[0]["extension"] &&
            examples["files_hex"][example["source"].asString()] ==
                sample.toHex().toStdString(),
        "fixture differs from the pinned project example");
  source["example"] = example;
  source["declaration"] = declaration;
  source["binding"] = raw;
  source["job"] = job;
  write(stage / "source.json", source);

  auto tools = file_qualification_tools();
  auto exe = scratch / "direct";
  Strings cmd{"g++-15",
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
  const auto results = parse(run(sandbox(exe), opts));
  check(results == parse(run(sandbox(exe), opts)) &&
            results.size() == cases.size(),
        "inconsistent direct extension filter observations");
  for (const auto &fixture : cases)
    check(results[fixture["name"].asString()] == fixture["expected"],
          "direct observations did not match frozen fixtures");
  write(stage / "standalone.json",
        object({{"processes", 2},
                {"cases", results},
                {"negative_controls", negative_controls(cases, results)}}));
  write(stage / "integration.json", ordinary(work, bundle, raw, cases));
  check(tools == file_qualification_tools(),
        "extension qualification tools changed");
  write(stage / "environment.json", path_client_environment(exe, tools));
  const auto contract =
      file_contract(stage, package, record, src, stage / "sources.json");
  return seal_file_qualification(work, package, pin, array({record}), 0,
                                 contract);
}
} // namespace v5
