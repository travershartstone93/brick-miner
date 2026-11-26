#include "bundle.hpp"
#include "migration_reports.hpp"
#include "pilot.hpp"
#include <QStandardPaths>
#include <sstream>
namespace v5 {
J retained_binding_job(const Path &registry, const std::string &target) {
  J job;
  for (const auto &row : load(registry).get("jobs", array()))
    if (row["target"] == target) {
      check(job.isNull(), "duplicate retained job: " + target);
      job = row;
    }
  return job;
}
std::string archive_member(const J &s, const std::string &file) {
  return run({"bsdtar", "-xOf", s["archive"].asString(), file});
}
std::string verified_runtime_package(const Path &work, const J &s) {
  Path arc = s["archive"].asString(), sig = arc.string() + ".sig",
       ring = work / "runtime-keyring.gpg";
  check(digest(arc) == s["sha256"].asString() &&
            digest(sig) == s["signature_sha256"].asString(),
        "changed installed runtime archive or signature");
  run({"gpg", "--batch", "--dearmor", "--output", ring,
       "/usr/share/pacman/keyrings/cachyos.gpg"});
  auto status = run({"gpgv", "--keyring", ring, "--status-fd", "1", sig, arc});
  check(status.find("[GNUPG:] VALIDSIG " + s["signer"].asString() + " ") !=
            std::string::npos,
        "unexpected installed runtime signer");
  for (const auto &f : s["files"].getMemberNames())
    check(sha(archive_member(s, f)) == s["files"][f].asString() &&
              digest(Path("/") / f) == s["files"][f].asString(),
          "installed runtime differs from signed package: " + f);
  return archive_member(s, ".PKGINFO");
}
J complete_file_plan(const Path &bundle, const Path &out, const J &spec,
                     const J &expected) {
  auto emitted = plan(bundle, spec, out);
  check(emitted["providers"].size() == 1 &&
            emitted["interpretation_errors"].empty() &&
            emitted["clarifications"].empty(),
        "file interpretation did not bind");
  auto p = emitted["providers"][0];
  J ids = array();
  for (const auto &step : p["plan"]["steps"])
    ids.append(step["provider"]);
  check(p["status"] == "complete" && p["uncovered"].empty() &&
            p["typed_gaps"].empty() && ids == expected,
        "unexpected emitted bindings " + dump(ids));
  return p;
}
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
J path_client_environment(const Path &exe, J env) {
  env["compiler_version"] = run({"g++-15", "-dumpfullversion"});
  env["jsoncpp_version"] = run({"pkg-config", "--modversion", "jsoncpp"});
  std::istringstream linked(run({"ldd", exe}));
  for (std::string line; std::getline(linked, line);) {
    std::istringstream fields(line);
    std::string soname, arrow, path;
    if (fields >> soname >> arrow >> path && arrow == "=>" &&
        (soname == "libstdc++.so.6" || soname.starts_with("libjsoncpp.so.")))
      env["direct_libraries"][soname] = object(
          {{"path", fs::canonical(path).string()}, {"sha256", digest(path)}});
  }
  check(env["direct_libraries"].size() == 2 &&
            env["direct_libraries"].isMember("libstdc++.so.6"),
        "missing direct client libraries");
  return env;
}
J file_contract(const Path &stage, const Path &package, const J &record,
                const J &src, const Path &registry) {
  auto c = load(stage / "annotation.json"), claims = c["claims"];
  c["schema"] = "unllm.brick-contract/1";
  c["vocabulary"] = "unllm.brick-vocabulary/1";
  const auto &key = record["key"];
  c["capability_id"] =
      record["effect"].isNull() ? key["binding_id"] : record["effect"];
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
  auto stage = work / ".stage";
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
  return finish_file_stage(work, package, pin, record["logical_id"].asString(),
                           "unllm.binding-qualification-stage/1");
}
Path finish_file_stage(const Path &work, const Path &package,
                       const std::string &pin, const std::string &id,
                       const std::string &schema) {
  auto stage = work / ".stage", scratch = work / ".test-programs";
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
  write(stage / "manifest.json", object({{"schema", schema},
                                         {"catalog_sha256", pin},
                                         {"active_manifest_sha256", base_hash},
                                         {"binding_id", id},
                                         {"files", tree_hashes(stage)}}));
  run({"sync", "-f", stage / "manifest.json"});
  fs::rename(stage, work / "stage");
  return work / "stage/manifest.json";
}
} // namespace v5
