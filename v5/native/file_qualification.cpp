#include "bundle.hpp"
#include "migration_reports.hpp"
#include "pilot.hpp"
#include <QStandardPaths>
namespace v5 {
void save_file_registry(const Path &stage, const J &source,
                        const Path &registry, const std::string &pin) {
  auto data = bytes(registry);
  check(pin.empty() || sha(data) == pin, "source registry pin mismatch");
  auto rows = parse(data)["sources"];
  unsigned matches = 0;
  for (const auto &row : rows)
    if (row["id"] == source["id"]) {
      check(row == source, "recorded source differs from current approval");
      ++matches;
    }
  check(matches == 1, "source registry does not uniquely identify the source");
  save(stage / "sources.json", data);
}
J file_qualification_tools() {
  J pins = object();
  for (const auto *name : {"g++-15", "bwrap", "prlimit", "env", "pkg-config"}) {
    auto path = QStandardPaths::findExecutable(name).toStdString();
    pins["tool_sha256"][name] = digest(path);
  }
  for (const auto *name : {"brick-provider-plan", "brick-index-formats",
                           "unllm_logic_worker", "libunllm_native_logic.so"})
    pins["ordinary_tools"][name] = digest(main_repo / "build/native" / name);
  auto dir = main_repo / "experiments/boilerplate/integrated";
  for (const auto *name :
       {"provider-stage.sh", "effective-contracts.sh", "stage-headers.sh"})
    pins["ordinary_tools"][name] = digest(dir / name);
  pins["qt_version"] = run({"pkg-config", "--modversion", "Qt6Core"});
  pins["qt_flags"] = string_array(flags({"Qt6Core"}));
  return pins;
}
J file_contract(const Path &stage, const Path &package, const J &record,
                const J &src, const Path &registry) {
  auto c = load(stage / "annotation.json"), claims = c["claims"];
  c["schema"] = "unllm.brick-contract/1";
  c["vocabulary"] = "unllm.brick-vocabulary/1";
  const auto &key = record["key"];
  c["capability_id"] = key["binding_id"];
  c["provider_id"] = c["legacy_brick"] = key["component_id"];
  c["interface"]["callable"] = record["call"];
  c["interface"]["structural_input"] = key["from"];
  c["interface"]["structural_output"] = key["to"];
  c["source"] = object({{"id", src["id"]},
                        {"revision", src["revision"]},
                        {"archive_sha256", src["sha256"]},
                        {"implementation_sha256", record["header_sha256"]},
                        {"header", "headers/" + record["header"].asString()},
                        {"source_registry_sha256", digest(registry)}});
  c["facets"]["license"] =
      array({src["rights"]["upstream_files"], src["rights"]["redistribute"]});
  c["facets"]["qualification"] = array({"ordinary-emission-tested"});
  c["qualification"] = object(
      {{"level", "ordinary-emission-tested"},
       {"independent", "standalone.json"},
       {"integration", "integration.json"},
       {"scope", "Local byte and file-effect fixtures on Linux/ext4. Detached "
                 "candidate for review against the pinned parent."}});
  c["fingerprint"] =
      object({{"suite_sha256", digest(stage / "spec.json")},
              {"observations_sha256", digest(stage / "standalone.json")},
              {"scope", "Native direct calls against the frozen byte and "
                        "file-effect cases on the recorded environment."}});
  c["property_signature"] = array();
  for (const auto &name : claims.getMemberNames()) {
    if (claims[name]["status"] == "observed")
      c["property_signature"].append(name);
    for (const auto &e : claims[name]["evidence"])
      project_file(stage, e.asString());
  }
  validate(c, load(package / "contract.schema.json"));
  return c;
}
Path seal_file_qualification(const Path &work, const Path &package,
                             const std::string &pin, J bindings,
                             Json::ArrayIndex selected, const J &contract) {
  auto stage = work / ".stage", scratch = work / ".test-programs";
  auto &record = bindings[selected];
  write(stage / "contract.json", contract);
  record["contract_sha256"] = digest(stage / "contract.json");
  record["qualification"] = contract["qualification"]["level"];
  write(stage / "payload.json",
        object({{"schema", "unllm.staged-binding-qualifications/1"},
                {"catalog_sha256", pin},
                {"bindings", bindings},
                {"contract_file", "contract.json"},
                {"qualified_logical_id", record["logical_id"]}}));
  project_binding_records(stage);
  auto programs = tree_hashes(scratch);
  write(stage / "tested-programs.json",
        object({{"programs_sha256", programs},
                {"standalone_sha256", digest(stage / "standalone.json")},
                {"integration_sha256", digest(stage / "integration.json")},
                {"environment_sha256", digest(stage / "environment.json")},
                {"spec_sha256", digest(stage / "spec.json")},
                {"verdict", "passed"}}));
  run({"sync", "-f", stage / "tested-programs.json"});
  fs::remove_all(scratch);
  // Keep the exact generated programs' build inputs and removal receipts
  // outside the portable stage.
  fs::remove_all(work / "control-bundle");
  check(digest(package / "manifest.json") == pin &&
            digest(verified_base() / "SHA256SUMS") == base_hash,
        "parent changed before sealing");
  write(stage / "manifest.json",
        object({{"schema", "unllm.binding-qualification-stage/1"},
                {"catalog_sha256", pin},
                {"active_manifest_sha256", base_hash},
                {"binding_id", record["logical_id"]},
                {"files", tree_hashes(stage)}}));
  run({"sync", "-f", stage / "manifest.json"});
  fs::rename(stage, work / "stage");
  return work / "stage/manifest.json";
}
} // namespace v5
