#include "migration_reports.hpp"
#include "pilot.hpp"
namespace v5 {
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
