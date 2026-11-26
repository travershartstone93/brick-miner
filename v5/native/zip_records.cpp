#include "bundle.hpp"
#include "migration_reports.hpp"
#include "pilot.hpp"
#include <QScopeGuard>
#include <QStandardPaths>
#include <sstream>

namespace v5 {
namespace {
const std::string effect = "zip_file_records";
const std::string source_id = "retained-zip-records";
const std::string library = "libbrick_shared_inputs.so";
const Path history = "qualifications/shared/inputs";
const Path runtime = "runtime/duckdb/lib";

void verify_materials(const Path &stage, const Path &bundle, const Path &base,
                      const J &src, const J &binding) {
  auto arc = Path(src["archive"].asString()), sig = Path(arc.string() + ".sig");
  auto recipe = arc.parent_path() / "libarchive.PKGBUILD";
  check(digest(arc) == src["sha256"].asString() &&
            digest(sig) == src["signature_sha256"].asString() &&
            digest(recipe) == src["recipe_sha256"].asString(),
        "changed retained Arch inputs");
  auto ring = stage.parent_path() / "arch.gpg";
  run({"gpg", "--batch", "--dearmor", "--output", ring,
       "/usr/share/pacman/keyrings/archlinux.gpg"});
  const auto signature =
      run({"gpgv", "--keyring", ring, "--status-fd", "1", sig, arc});
  check(signature.find("[GNUPG:] VALIDSIG " + src["signer"].asString() + " ") !=
            std::string::npos,
        "unexpected Arch archive signer");
  auto build = archive_member(src, ".BUILDINFO");
  auto info = archive_member(src, ".PKGINFO");
  check(build.find("\npkgbuild_sha256sum = " + digest(recipe) + "\n") !=
                std::string::npos &&
            info.find("\npkgname = libarchive\n") != std::string::npos &&
            info.find("\npkgver = " + src["release"].asString() + "\n") !=
                std::string::npos &&
            info.find("\nurl = " + src["upstream"].asString() + "\n") !=
                std::string::npos,
        "unexpected Arch build or package identity");
  save(stage / "arch-pkginfo.txt", info);
  save(stage / "arch-pkgbuild.txt", bytes(recipe));

  const auto provenance = load(bundle / history / "source.json");
  auto kernel = digest(bundle / runtime / library);
  auto source = object({{"binding", binding},
                        {"source_id", source_id},
                        {"kernel_sha256", kernel},
                        {"arch_signature_sha256", digest(sig)},
                        {"arch_signer", src["signer"]},
                        {"arch_keyring_sha256", digest(ring)},
                        {"arch_buildinfo_sha256", sha(build)},
                        {"archive_entrypoint", "archive_read_next_header"}});
  auto link_files = array();
  for (const auto &file :
       {Path(library), Path("libarchive.so.13"), Path("libsqlite3.so.0"),
        Path("liburiparser.so.1"), Path("libduckdb.so"),
        Path("pkgconfig/brick_shared_inputs.pc")}) {
    auto hash = digest(bundle / runtime / file);
    check(hash == digest(base / runtime / file), "retained runtime changed");
    link_files.append(object({{"path", file.string()}, {"sha256", hash}}));
  }
  for (const auto *soname :
       {"libarchive.so.13", "libsqlite3.so.0", "liburiparser.so.1"})
    check(digest(bundle / runtime / soname) ==
              provenance["runtime_sha256"][soname].asString(),
          "legacy runtime provenance mismatch");
  check(sha(archive_member(src, "usr/lib/libarchive.so.13.8.9")) ==
                digest(bundle / runtime / "libarchive.so.13") &&
            provenance["kernel_sha256"] == kernel &&
            provenance["archives"][arc.filename().string()]["signer"] ==
                src["signer"],
        "kernel or signed archive provenance mismatch");
  source["runtime_files"] = link_files;
  auto adapter = bytes(bundle / history / "adapter.cpp");
  check(sha(adapter) == provenance["adapter_sha256"].asString() &&
            adapter == run({"git", "-C", root, "show",
                            src["adapter_revision"].asString() +
                                ":shared/inputs/adapter.cpp"}),
        "retained adapter differs from recorded source");
  save(stage / "original/adapter.cpp", adapter);
  const auto sibling_revision = src["provider_abi_revision"].asString();
  auto abi = run({"git", "-C", main_repo, "show",
                  sibling_revision + ":experiments/boilerplate/component-trial/"
                                     "providers/provider_types.hpp"});
  auto notice =
      run({"git", "-C", main_repo, "show", sibling_revision + ":LICENSE"});
  check(abi == bytes(bundle / "providers/provider_types.hpp") &&
            sha(notice) == src["provider_abi_notice_sha256"].asString(),
        "retained provider ABI or notice changed");
  save(stage / "original/provider_types.hpp", abi);
  fs::copy_file(bundle / history / "source.json", stage / "legacy-source.json");
  fs::copy_file(bundle / history / "contracts.json",
                stage / "legacy-contracts.json");
  fs::copy_file(bundle / "headers" / binding["header"].asString(),
                stage / "original/miner_zip_file_records.hpp");
  save(stage / "notices/provider-abi-MIT", notice);
  for (const auto *name : {"BSD-2-Clause.txt", "BSD-4-Clause-UC.txt"}) {
    std::string path = "usr/share/licenses/libarchive/" + std::string(name);
    auto content = archive_member(src, path);
    check(sha(content) == provenance["headers"][path].asString(),
          "archived libarchive notice changed");
    save(stage / "notices" / name, content);
  }
  const std::string pugi = "usr/share/licenses/pugixml/LICENSE.md";
  const auto pugi_notice =
      bytes(bundle / "qualifications/shared/upstream/formats" / pugi);
  check(sha(pugi_notice) == provenance["headers"][pugi].asString(),
        "bundled pugixml notice changed");
  save(stage / "notices/pugixml-LICENSE.md", pugi_notice);
  auto header = archive_member(src, "usr/include/archive.h");
  check(sha(header) ==
            provenance["headers"]["usr/include/archive.h"].asString(),
        "archived libarchive header changed");
  save(stage / "original/archive.h", header);
  write(stage / "source.json", source);
}

void make_inputs(const Path &work, const Path &stage, const J &spec) {
  auto members = work / "members", tar = work / "input.tar";
  save(members / "d/UP.CsV", "data");
  save(members / "TOP.csv", "");
  fs::create_directories(members / "links");
  fs::create_symlink("../TOP.csv", members / "links/alias");
  fs::create_directory(members / "dir.csv");
  for (const auto *name : {"d/archive.tar.GZ", "notes/.env", "p/x.", "README"})
    save(members / name, "data");
  auto c = spec["cases"][0];
  check(c["format"] == "mixed", "expected a fixture member order");
  Strings command{"tar",
                  "--format=ustar",
                  "--mtime=@946684800",
                  "--mode=u+rwX,go=rX",
                  "--owner=0",
                  "--group=0",
                  "--numeric-owner",
                  "--no-recursion",
                  "--hard-dereference",
                  "-cf",
                  tar,
                  "--"};
  append(command, strings(c["members"]));
  run(command, {.cwd = members});
  auto fixtures = stage / "fixtures";
  fs::create_directories(fixtures);
  run({"bsdtar", "--format", "zip", "--options", "zip:compression=store", "-cf",
       fixtures / "mixed", "@" + tar.string()});
  check(run({"unzip", "-Z1", fixtures / "mixed"}) ==
            join(strings(c["members"]), "\n") + "\n",
        "Info-ZIP member list differs from authored fixture");
  run({"unzip", "-tqq", fixtures / "mixed"});
  fs::copy_file(tar, fixtures / "tar");
  save(fixtures / "empty",
       std::string("PK\005\006", 4) + std::string(18, '\0'));
  save(fixtures / "invalid", "unrecognized archive");
  const auto path = volume / "unllm/experiments/v5-parent-transfer-QCn3p5ZQ";
  const auto pins = spec["pinned_fixtures"];
  for (const auto &name : pins.getMemberNames()) {
    auto source = path / (name + ".zip");
    check(digest(source) == pins[name].asString(), "prior ZIP fixture changed");
    fs::copy_file(source, fixtures / name);
  }
}

Strings invoke(const Path &exe, const Path &dir, const Path &bundle) {
  auto cmd = sandbox(exe, dir);
  cmd.pop_back();
  append(cmd, {"--ro-bind", bundle / runtime, "/retained-runtime", "--setenv",
               "LD_LIBRARY_PATH", "/retained-runtime", "/probe"});
  return cmd;
}

J repeated_calls(const Path &exe, const Path &stage, const Path &bundle,
                 const J &spec) {
  const auto fixtures = stage / "fixtures";
  const auto before = tree_hashes(fixtures);
  J observed = array();
  auto probe = [&](const std::string &path, const std::string &fields) {
    auto cmd = invoke(exe, fixtures, bundle);
    // The sandbox binds the runtime and case directory separately. The
    // recorded fixture inventory checks that neither invocation changed input.
    append(cmd, {path, fields});
    auto first = parse(run(cmd, {.seconds = 8, .probe_limits = true}));
    check(first == parse(run(cmd, {.seconds = 8, .probe_limits = true})) &&
              first["libarchive"] == 3008009,
          "different archive version or process result");
    return first;
  };
  for (const auto &c : spec["cases"]) {
    auto seen = probe(c["format"].asString(), "ext,parent");
    J rows = array();
    int pos = 0;
    for (const auto &r : c["rows"])
      rows.append(object({{"position", ++pos}, {"fields", r}}));
    check(seen["rows"] == rows &&
              seen["phase"].asString() == c.get("error", "").asString() &&
              seen["exception"] ==
                  (c.isMember("error") ? J("runtime_error") : J()),
          "ZIP row observation differs for " + c["name"].asString() + ": " +
              dump(seen));
    observed.append(object({{"name", c["name"]}, {"observation", seen}}));
  }
  auto reorder = probe("top-level", "parent,ext");
  check(reorder["phase"] == "" &&
            reorder["rows"] ==
                array({object(
                    {{"position", 1}, {"fields", array({".", "csv"})}})}),
        "reordered projection did not preserve field semantics");
  auto invalid = probe("top-level", "ext,unknown");
  check(invalid["phase"] == "select" &&
            invalid["exception"] == "invalid_argument" &&
            invalid["rows"] == array() && tree_hashes(fixtures) == before,
        "unexpected projection rejection or fixture mutation");
  return object({{"processes_per_case", 2},
                 {"cases", observed},
                 {"input_sha256", before},
                 {"reordered_control", reorder},
                 {"invalid_projection", invalid}});
}

J ordinary(const Path &work, const Path &stage, const Path &bundle,
           const Path &base, const J &row, const J &suite) {
  auto recipes = root / "shared/request-recipes.json";
  check(digest(recipes) ==
            "768676cbc34ef7db9d869520bd3d26d86123f49a3c9803901eb5653de3a6f49b",
        "changed H058 operation recipe");
  const auto tasks = load(recipes);
  J ops = array();
  for (const auto &step : tasks["H058"][0])
    ops.append(object({{"outcome", step[0]}, {"parameters", step[1]}}));
  check(ops[0]["outcome"] == effect &&
            ops[0]["parameters"] == suite["parameters"],
        "H058 does not request the qualified projection");
  const auto spec =
      spec_for("application/zip", "text/plain",
               object({{"operations", ops}, {"uncovered", array()}}));
  const auto chain =
      array({"op_0_zip_file_records", "op_1_filter_records", "op_2_column",
             "op_3_unique_lines", "op_4_sort_lines", "write_lines"});
  auto report = object(
      {{"spec", spec}, {"chain", chain}, {"recipe_sha256", digest(recipes)}});
  for (const std::string arm : {"baseline", "staged"}) {
    auto dest = work / arm, exe = work / ".test-programs" / arm;
    auto p = complete_file_plan(arm == "baseline" ? base : bundle, dest, spec,
                                chain);
    build_emission(p, dest, exe, "g++-15");
    J observations = array();
    for (const auto &c : suite["cases"])
      for (const bool exists : {false, true}) {
        auto dir =
            dest / c["name"].asString() / (exists ? "existing" : "absent");
        fs::create_directories(dir);
        auto fixture = stage / "fixtures" / c["format"].asString();
        if (fs::exists(fixture))
          fs::copy_file(fixture, dir / "in");
        if (exists)
          save(dir / "out", "sentinel");
        auto before = tree_hashes(dir), after = before;
        auto cmd = invoke(exe, dir, bundle);
        append(cmd, {"in", "out"});
        auto result = execute(cmd, {.seconds = 8, .probe_limits = true});
        const bool fails = c.isMember("error");
        if (!fails)
          after["out"] = sha(c["output"].asString());
        check(result.status == (fails ? 1 : 0) && tree_hashes(dir) == after,
              "unexpected CLI ZIP result for " + c["name"].asString() + " " +
                  result.err);
        observations.append(object({{"case", c["name"]},
                                    {"exit", result.status},
                                    {"destination_existed", exists},
                                    {"before", before},
                                    {"after", after}}));
      }
    report[arm] = object({{"execution", observations},
                          {"program_sha256", digest(exe)},
                          {"emitted_sha256", digest(dest / "component.hpp")}});
  }
  check(report["baseline"] == report["staged"],
        "retained ZIP recipe differs between arms");
  auto cut = work / "control-bundle";
  write_tree(cut, remove_binding(read_tree(bundle), row));
  auto absent = plan(cut, spec, work / "removed");
  check(absent["providers"].isArray() && absent["providers"].empty() &&
            absent["interpretation_errors"].empty() &&
            absent["clarifications"].empty() &&
            absent["gaps"]["r"].asString().find(effect) != std::string::npos,
        "removal did not isolate the correlated ZIP binding");
  report["removed"] = object({{"gaps", absent["gaps"]},
                              {"bundle_sha256", digest(cut / "SHA256SUMS")}});
  return report;
}
} // namespace

Path qualify_zip_records(const Path &package, const std::string &pin,
                         const Path &registry,
                         const std::string &registry_pin) {
  verify(package, pin);
  auto src = sources()[source_id];
  const auto job = retained_binding_job(v5_root() / "sources.json", effect);
  check(src["approval"] == "local-pilot" &&
            src["method"] == "signed-local-package" &&
            job["source"] == source_id && job["route"] == "api" &&
            job["stream"] == "foundational" &&
            job["tests"] == "v5/fixtures/zip-records.json" &&
            job["parameter"] == object({{"fields", "ext,parent"}}),
        "no reviewed local ZIP projection job");
  const auto work = fresh_run("v5-zip-records"), stage = work / ".stage",
             scratch = work / ".test-programs", bundle = package / "bundle",
             base = verified_base();
  fs::create_directory(scratch);
  save_file_registry(stage, src, registry, registry_pin);
  check(retained_binding_job(stage / "sources.json", effect) == job,
        "changed ZIP projection job");
  auto defs = load(bundle / "providers/contracts.json");
  auto raw = select_binding(defs["providers"], effect, "miner_zip_file_records",
                            "in:application/zip", "rows");
  auto record = file_binding_identity(raw, bundle);
  auto legacy = load(bundle / history / "contracts.json")[effect];
  check(raw["id"] == effect &&
            raw["call"] == "brick_miner::shared::zip_file_records" &&
            raw["args"] == array({"fields"}) && raw["from"] == legacy["from"] &&
            raw["to"] == legacy["to"] &&
            legacy["args"] == object({{"fields", "text"}}) &&
            defs["outcomes"][effect]["parameters"] == legacy["args"],
        "ZIP reader binding differs from retained declaration");
  verify_materials(stage, bundle, base, src, raw);
  const auto source = load(stage / "source.json");
  for (const auto &[from, to] :
       std::vector<std::pair<std::string, std::string>>{
           {"zip-records.json", "spec.json"},
           {"zip-records-client.cpp", "client.cpp"},
           {"zip-records-annotation.json", "annotation.json"}})
    fs::copy_file(v5_root() / "fixtures" / from, stage / to);
  const auto spec = load(stage / "spec.json");
  check(spec["binding"] == record["key"] &&
            spec["parameters"] == job["parameter"],
        "qualification scope differs from reviewed binding or parameter");
  make_inputs(work, stage, spec);

  auto old_path = qgetenv("PKG_CONFIG_PATH");
  const auto restore = qScopeGuard([&] {
    if (old_path.isNull())
      qunsetenv("PKG_CONFIG_PATH");
    else
      qputenv("PKG_CONFIG_PATH", old_path);
  });
  check(qputenv("PKG_CONFIG_PATH", (bundle / runtime / "pkgconfig").c_str()),
        "failed to set pinned runtime package path");
  auto options = flags({"jsoncpp", "brick_shared_inputs"});
  auto exe = scratch / "client";
  Strings cmd{"g++-15",           "-std=c++17",         "-O1", "-I",
              stage / "original", stage / "client.cpp", "-o",  exe};
  append(cmd, options);
  append(cmd, {(bundle / runtime / "libarchive.so.13").string(),
               "-Wl,-rpath-link," + (bundle / runtime).string()});
  run(cmd);
  auto tools = file_qualification_tools(), env = tools;
  for (const auto *name : {"tar", "bsdtar", "unzip", "gpgv", "ldd"})
    env["tool_sha256"][name] =
        digest(QStandardPaths::findExecutable(name).toStdString());
  env["client_flags"] = string_array(options);
  env["target"] = run({"g++-15", "-dumpmachine"});
  env["runtime_files"] = source["runtime_files"];
  std::istringstream link(
      run({"ldd", exe},
          {.environment = {{"LD_LIBRARY_PATH", (bundle / runtime).string()}}}));
  for (std::string line; std::getline(link, line);) {
    std::istringstream fields(line);
    std::string name, arrow, path;
    if (fields >> name >> arrow >> path && arrow == "=>")
      env["resolved_client_libraries"][name] = object(
          {{"path", fs::canonical(path).string()}, {"sha256", digest(path)}});
  }
  for (const auto &f : source["runtime_files"]) {
    auto name = f["path"].asString();
    if (name == library || name == "libarchive.so.13")
      check(env["resolved_client_libraries"][name]["sha256"] == f["sha256"] &&
                env["resolved_client_libraries"][name]["path"] ==
                    fs::canonical(bundle / runtime / name).string(),
            "standalone client linked the wrong retained runtime");
  }
  write(stage / "environment.json", env);
  write(stage / "standalone.json", repeated_calls(exe, stage, bundle, spec));
  write(stage / "integration.json",
        ordinary(work, stage, bundle, base, raw, spec));
  check(file_qualification_tools() == tools, "qualification tools changed");
  auto contract =
      file_contract(stage, package, record, src, stage / "sources.json");
  contract["source"]["implementation_sha256"] = source["kernel_sha256"];
  contract["qualification"]["scope"] =
      "Detached local evidence for one exact fields selector, ext,parent, on "
      "the pinned archived runtime and recorded ZIP fixtures.";
  validate(contract, load(package / "contract.schema.json"));
  write(stage / "contract.json", contract);
  write(stage / "domain.json",
        object({{"schema", "unllm.scoped-file-evidence-domain/1"},
                {"binding_key", record["key"]},
                {"raw_binding_sha256", record["binding_sha256"]},
                {"logical_id", record["logical_id"]},
                {"implementation_sha256", source["kernel_sha256"]},
                {"header_sha256", record["header_sha256"]},
                {"contract_sha256", digest(stage / "contract.json")},
                {"environment_sha256", digest(stage / "environment.json")},
                {"call", raw["call"]},
                {"argument_index", 1},
                {"argument_type", "const std::string&"},
                {"quantifier", "argument"},
                {"all_of", array({object({{"id", "exact_text"},
                                          {"value", "ext,parent"}})})},
                {"scope",
                 "Evidence applies to the named parameter projection. "
                 "Use this detached domain record for review only."}}));
  return finish_file_stage(work, package, pin, record["logical_id"].asString(),
                           "unllm.scoped-file-evidence/1");
}
} // namespace v5
