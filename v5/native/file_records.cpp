#include "bundle.hpp"
#include "migration_reports.hpp"
#include "pilot.hpp"
#include <QScopeGuard>
#include <QStandardPaths>
#include <sstream>
#include <sys/stat.h>

namespace v5 {
namespace {
const std::string source_id = "retained-zip-records";
const std::string library = "libbrick_shared_inputs.so";
const Path history = "qualifications/shared/inputs";
const Path runtime = "runtime/duckdb/lib";
const Path readers_history = "qualifications/shared/readers";
const std::string readers_library = "libbrick_shared_readers.so";

struct Projection {
  std::string binding, input_type, fields, request, fixture;
  J chain;
  bool directory() const { return input_type == "inode/directory"; }
  bool json() const { return input_type == "application/json"; }
};

Path arch_keyring(const Path &stage) {
  auto ring = stage.parent_path() / "arch.gpg";
  run({"gpg", "--batch", "--dearmor", "--output", ring,
       "/usr/share/pacman/keyrings/archlinux.gpg"});
  return ring;
}

std::string signed_build_info(const J &src, const Path &recipe,
                              const Path &ring) {
  Path arc = src["archive"].asString(), sig = arc.string() + ".sig";
  check(digest(arc) == src["sha256"].asString() &&
            digest(sig) == src["signature_sha256"].asString() &&
            digest(recipe) == src["recipe_sha256"].asString(),
        "changed retained Arch inputs");
  const auto signature =
      run({"gpgv", "--keyring", ring, "--status-fd", "1", sig, arc});
  check(signature.find("[GNUPG:] VALIDSIG " + src["signer"].asString() + " ") !=
            std::string::npos,
        "unexpected Arch archive signer");
  auto build = archive_member(src, ".BUILDINFO");
  check(build.find("\npkgbuild_sha256sum = " + digest(recipe) + "\n") !=
            std::string::npos,
        "unexpected Arch build attestation");
  return build;
}

std::string package_info(const J &src, const std::string &name) {
  auto info = archive_member(src, ".PKGINFO");
  check(info.find("\npkgname = " + name + "\n") != std::string::npos &&
            info.find("\npkgver = " + src["release"].asString() + "\n") !=
                std::string::npos &&
            info.find("\nurl = " + src["upstream"].asString() + "\n") !=
                std::string::npos,
        "unexpected Arch package identity");
  return info;
}

J runtime_files(const Path &bundle, const Path &base, const Strings &files) {
  auto link_files = array();
  for (const auto &file : files) {
    auto hash = digest(bundle / runtime / file);
    check(hash == digest(base / runtime / file), "retained runtime changed");
    link_files.append(object({{"path", file}, {"sha256", hash}}));
  }
  return link_files;
}

void retain_adapter(const Path &stage, const Path &bundle, const Path &origin,
                    const J &src, const J &binding, const J &provenance,
                    const std::string &git_adapter) {
  auto adapter = bytes(bundle / origin / "adapter.cpp");
  check(sha(adapter) == provenance["adapter_sha256"].asString() &&
            adapter ==
                run({"git", "-C", root, "show",
                     src["adapter_revision"].asString() + ":" + git_adapter}),
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
  fs::copy_file(bundle / origin / "source.json", stage / "legacy-source.json");
  fs::copy_file(bundle / origin / "contracts.json",
                stage / "legacy-contracts.json");
  fs::copy_file(bundle / "headers" / binding["header"].asString(),
                stage / "original" / binding["header"].asString());
  save(stage / "notices/provider-abi-MIT", notice);
}

void verify_archive_materials(const Path &stage, const Path &bundle,
                              const Path &base, const J &src,
                              const J &binding) {
  const Path arc = src["archive"].asString();
  auto recipe = arc.parent_path() / "libarchive.PKGBUILD";
  auto ring = arch_keyring(stage);
  auto build = signed_build_info(src, recipe, ring);
  save(stage / "arch-pkginfo.txt", package_info(src, "libarchive"));
  save(stage / "arch-pkgbuild.txt", bytes(recipe));
  const auto provenance = load(bundle / history / "source.json");
  auto kernel = digest(bundle / runtime / library);
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
  auto source =
      object({{"binding", binding},
              {"source_id", src["id"]},
              {"kernel_sha256", kernel},
              {"arch_signature_sha256", src["signature_sha256"]},
              {"arch_signer", src["signer"]},
              {"arch_keyring_sha256", digest(ring)},
              {"arch_buildinfo_sha256", sha(build)},
              {"archive_entrypoint", "archive_read_next_header"},
              {"runtime_files",
               runtime_files(bundle, base,
                             {library, "libarchive.so.13", "libsqlite3.so.0",
                              "liburiparser.so.1", "libduckdb.so",
                              "pkgconfig/brick_shared_inputs.pc"})}});
  retain_adapter(stage, bundle, history, src, binding, provenance,
                 "shared/inputs/adapter.cpp");
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

void verify_json_materials(const Path &stage, const Path &bundle,
                           const Path &base, const J &src, const J &binding) {
  const Path arc = src["archive"].asString();
  auto recipe = arc.parent_path() / "PKGBUILD";
  auto ring = arch_keyring(stage);
  auto build = signed_build_info(src, recipe, ring);
  auto debug = src;
  debug["archive"] =
      (arc.parent_path() / src["debug_archive"].asString()).string();
  debug["sha256"] = src["debug_sha256"];
  debug["signature_sha256"] = src["debug_signature_sha256"];
  auto debug_build = signed_build_info(debug, recipe, ring);
  const auto info = package_info(src, "duckdb");
  check(info.find("\nlicense = MIT\n") != std::string::npos,
        "signed DuckDB package license changed");
  save(stage / "arch-pkginfo.txt", info);
  save(stage / "arch-pkgbuild.txt", bytes(recipe));
  const auto provenance = load(bundle / readers_history / "source.json");
  const auto kernel = digest(bundle / runtime / readers_library);
  const auto definition = provenance["definitions"]["yyjson_read_file"];
  check(provenance["source_id"] == src["upstream_source_id"] &&
            provenance["kernel_sha256"] == kernel &&
            provenance["source_units"]["yyjson_read_file"] ==
                "third_party/yyjson/yyjson.cpp" &&
            run({"nm", "-D", "--defined-only",
                 bundle / runtime / readers_library})
                    .find(" " + definition["name"].asString() + "\n") !=
                std::string::npos,
        "retained yyjson implementation identity changed");

  auto packed_library = stage.parent_path() / "signed-libduckdb.so";
  run({"bsdtar", "-xOf", arc, "usr/lib/libduckdb.so"},
      {.output = packed_library});
  check(digest(packed_library) == digest(bundle / runtime / "libduckdb.so"),
        "DuckDB runtime differs from signed binary package");
  fs::remove(packed_library);
  auto extracted = stage.parent_path() / "signed-debug";
  fs::create_directory(extracted);
  const Path prefix = "usr/src/debug/duckdb/duckdb";
  const Strings members{"third_party/yyjson/yyjson.cpp",
                        "third_party/yyjson/include/yyjson.hpp"};
  Strings command{"bsdtar", "-xf", debug["archive"].asString(), "-C",
                  extracted};
  for (const auto &name : members)
    command.push_back((prefix / name).string());
  run(command);
  for (const auto &name : members) {
    auto data = bytes(extracted / prefix / name);
    auto retained = bundle / "qualifications/shared/upstream" / name;
    check(sha(data) == provenance["headers"][name].asString() &&
              data == bytes(retained),
          "retained yyjson source differs from signed debug package");
    save(stage / "original" / Path(name).filename(), data);
  }
  fs::remove_all(extracted);
  save(stage / "notices/duckdb-LICENSE",
       archive_member(src, "usr/share/licenses/duckdb/LICENSE"));
  retain_adapter(stage, bundle, readers_history, src, binding, provenance,
                 "shared/readers/adapter.cpp");
  write(stage / "source.json",
        object({{"binding", binding},
                {"source_id", src["id"]},
                {"kernel_sha256", kernel},
                {"arch_signature_sha256", src["signature_sha256"]},
                {"arch_debug_signature_sha256", debug["signature_sha256"]},
                {"arch_signer", src["signer"]},
                {"arch_keyring_sha256", digest(ring)},
                {"arch_buildinfo_sha256", sha(build)},
                {"arch_debug_buildinfo_sha256", sha(debug_build)},
                {"parser_symbol", definition},
                {"runtime_files",
                 runtime_files(bundle, base,
                               {readers_library, "libduckdb.so",
                                "pkgconfig/brick_shared_readers.pc"})}}));
}

void make_zip_inputs(const Path &work, const Path &stage, const J &spec) {
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

void make_tree_input(const Path &dir, const J &fixture) {
  auto input = dir / "in";
  const auto kind = fixture["kind"].asString();
  if (kind == "missing")
    return;
  if (kind == "file") {
    save(input, "regular file");
    return;
  }
  if (kind == "symlink") {
    save(dir / "target/hidden.TXT", "regular file behind link");
    fs::create_symlink("target", input);
    return;
  }
  check(kind == "directory", "unknown directory fixture kind");
  fs::create_directories(input);
  for (const auto &folder : fixture["directories"])
    fs::create_directories(input / folder.asString());
  for (const auto &name : fixture["files"])
    save(input / name.asString(), "data");
  const auto links = fixture.get("symlinks", object());
  for (const auto &name : links.getMemberNames())
    fs::create_symlink(links[name].asString(), input / name);
  for (const auto &name : fixture["fifos"])
    check(::mkfifo((input / name.asString()).c_str(), 0600) == 0,
          "cannot create tree fixture FIFO");
}

J fixture_inventory(const Path &dir, bool directory) {
  if (!directory)
    return tree_hashes(dir);
  J files = object();
  for (const auto &entry : fs::recursive_directory_iterator(dir)) {
    const auto key = entry.path().lexically_relative(dir).generic_string();
    const auto type = entry.symlink_status().type();
    switch (type) {
    case fs::file_type::regular:
      files[key] = digest(entry.path());
      break;
    case fs::file_type::directory:
      files[key] = object({{"directory", true}});
      break;
    case fs::file_type::symlink:
      files[key] =
          object({{"symlink", fs::read_symlink(entry.path()).string()}});
      break;
    case fs::file_type::fifo:
      files[key] = object({{"fifo", true}});
      break;
    default:
      throw std::runtime_error("unexpected tree fixture entry: " + key);
    }
  }
  return files;
}

Strings invoke(const Path &exe, const Path &dir, const Path &bundle) {
  auto cmd = sandbox(exe, dir);
  cmd.pop_back();
  append(cmd, {"--ro-bind", bundle / runtime, "/retained-runtime", "--setenv",
               "LD_LIBRARY_PATH", "/retained-runtime", "/probe"});
  return cmd;
}

J repeated_calls(const Path &exe, const Path &fixtures, const Path &bundle,
                 const J &spec, const Projection &projection) {
  const auto before = fixture_inventory(fixtures, projection.directory());
  J observed = array();
  auto probe = [&](const std::string &path, const std::string &fields) {
    auto cmd = invoke(exe, fixtures, bundle);
    // The sandbox binds the runtime and case directory separately. The
    // recorded fixture inventory checks that neither invocation changed input.
    append(cmd, {path, fields});
    auto first = parse(run(cmd, {.seconds = 8, .probe_limits = true}));
    check(first == parse(run(cmd, {.seconds = 8, .probe_limits = true})) &&
              (projection.json() || first["libarchive"] == 3008009),
          "different runtime or process result");
    return first;
  };
  auto expected = [&](const J &c, J &seen) {
    J rows = array();
    int pos = 0;
    for (const auto &r : c["rows"])
      rows.append(object({{"position", ++pos}, {"fields", r}}));
    check(seen["rows"] == rows &&
              seen["phase"].asString() == c.get("error", "").asString() &&
              seen["exception"] ==
                  c.get("exception",
                        c.isMember("error") ? J("runtime_error") : J()) &&
              (!(projection.directory() || projection.json()) ||
               c.isMember("error") || seen["same_instance_repeat"] == true),
          projection.binding + " row observation differs for " +
              c["name"].asString() + ": " + dump(seen));
  };
  for (const auto &c : spec["cases"]) {
    auto name = c["name"].asString();
    auto input = projection.directory() ? name + "/in"
                 : projection.json()    ? name
                                        : c["format"].asString();
    auto seen = probe(input, projection.fields);
    expected(c, seen);
    observed.append(object({{"name", c["name"]}, {"observation", seen}}));
  }
  if (projection.json()) {
    auto controls = array();
    for (const auto &c : spec["selector_controls"]) {
      auto observed_control =
          probe(c["input_case"].asString(), c["fields"].asString());
      expected(c, observed_control);
      controls.append(
          object({{"name", c["name"]}, {"observation", observed_control}}));
    }
    check(fixture_inventory(fixtures, false) == before,
          "JSON reader changed a fixture");
    return object({{"processes_per_case", 2},
                   {"cases", observed},
                   {"input_sha256", before},
                   {"selector_controls", controls}});
  }
  auto control_input = projection.directory() ? "bare-root/in" : "top-level";
  auto reorder = probe(control_input,
                       projection.directory() ? "ext,parent" : "parent,ext");
  const auto expected_control =
      projection.directory()
          ? array({object({{"position", 1}, {"fields", array({"txt", "."})}}),
                   object({{"position", 2}, {"fields", array({"", "."})}})})
          : array({object({{"position", 1}, {"fields", array({".", "csv"})}})});
  check(reorder["phase"] == "" && reorder["rows"] == expected_control,
        "reordered projection did not preserve field semantics");
  auto invalid = probe(control_input, "ext,unknown");
  check(invalid["phase"] == "select" &&
            invalid["exception"] == "invalid_argument" &&
            invalid["rows"] == array() &&
            fixture_inventory(fixtures, projection.directory()) == before,
        "unexpected projection rejection or fixture mutation");
  return object({{"processes_per_case", 2},
                 {"cases", observed},
                 {"input_sha256", before},
                 {"reordered_control", reorder},
                 {"invalid_projection", invalid}});
}

J ordinary(const Path &work, const Path &stage, const Path &bundle,
           const Path &base, const J &row, const J &suite,
           const Projection &projection) {
  auto recipes = root / "shared/request-recipes.json";
  check(digest(recipes) ==
            "768676cbc34ef7db9d869520bd3d26d86123f49a3c9803901eb5653de3a6f49b",
        "changed retained operation recipes");
  const auto tasks = load(recipes);
  J ops = array();
  for (const auto &step : tasks[projection.request][0])
    ops.append(object({{"outcome", step[0]}, {"parameters", step[1]}}));
  check(ops[0]["outcome"] == projection.binding &&
            ops[0]["parameters"] == suite["parameters"],
        "reference recipe does not request the qualified projection");
  const auto spec =
      spec_for(projection.input_type, "text/plain",
               object({{"operations", ops}, {"uncovered", array()}}));
  auto report = object({{"spec", spec},
                        {"chain", projection.chain},
                        {"recipe_sha256", digest(recipes)}});
  for (const std::string arm : {"baseline", "staged"}) {
    auto dest = work / arm, exe = work / ".test-programs" / arm;
    auto p = complete_file_plan(arm == "baseline" ? base : bundle, dest, spec,
                                projection.chain);
    build_emission(p, dest, exe, "g++-15");
    J observations = array();
    for (const auto &c : suite["cases"]) {
      if (c.get("standalone_only", false).asBool())
        continue;
      for (const bool exists : {false, true}) {
        auto dir =
            dest / c["name"].asString() / (exists ? "existing" : "absent");
        fs::create_directories(dir);
        if (projection.directory())
          make_tree_input(dir, c);
        else {
          auto fixture = stage / "fixtures" /
                         c[projection.json() ? "name" : "format"].asString();
          if (fs::exists(fixture))
            fs::copy_file(fixture, dir / "in");
        }
        if (exists)
          save(dir / "out", "sentinel");
        auto before = fixture_inventory(dir, projection.directory()),
             after = before;
        auto cmd = invoke(exe, dir, bundle);
        append(cmd, {"in", "out"});
        auto result = execute(cmd, {.seconds = 8, .probe_limits = true});
        const bool fails = c.isMember("error");
        if (!fails)
          after["out"] = sha(c["output"].asString());
        check(result.status == (fails ? 1 : 0) &&
                  fixture_inventory(dir, projection.directory()) == after,
              "unexpected CLI file record result for " + c["name"].asString() +
                  " " + result.err);
        observations.append(object({{"case", c["name"]},
                                    {"exit", result.status},
                                    {"destination_existed", exists},
                                    {"before", before},
                                    {"after", after}}));
      }
    }
    report[arm] = object({{"execution", observations},
                          {"program_sha256", digest(exe)},
                          {"emitted_sha256", digest(dest / "component.hpp")}});
  }
  check(report["baseline"] == report["staged"],
        "retained file record recipe differs between arms");
  auto cut = work / "control-bundle";
  write_tree(cut, remove_binding(read_tree(bundle), row));
  auto absent = plan(cut, spec, work / "removed");
  check(absent["providers"].isArray() && absent["providers"].empty() &&
            absent["interpretation_errors"].empty() &&
            absent["clarifications"].empty() &&
            absent["gaps"]["r"].asString().find(projection.binding) !=
                std::string::npos,
        "removal did not isolate the file record binding");
  report["removed"] = object({{"gaps", absent["gaps"]},
                              {"bundle_sha256", digest(cut / "SHA256SUMS")}});
  return report;
}

Path qualify_records(const Path &package, const std::string &pin,
                     const Path &registry, const std::string &registry_pin,
                     const Projection &projection) {
  verify(package, pin);
  const auto selected_source =
      projection.json() ? "retained-json-records" : source_id;
  auto src = sources()[selected_source];
  const auto job =
      retained_binding_job(v5_root() / "sources.json", projection.binding);
  check(src["approval"] == "local-pilot" &&
            src["method"] == "signed-local-package" &&
            job["source"] == selected_source && job["route"] == "api" &&
            job["stream"] == "foundational" &&
            job["tests"] == "v5/fixtures/" + projection.fixture + ".json" &&
            job["parameter"] == object({{"fields", projection.fields}}) &&
            (!projection.directory() ||
             !job["approval_reference"].asString().empty()),
        "no reviewed local file record projection job");
  const auto work = fresh_run("v5-" + projection.fixture),
             stage = work / ".stage", scratch = work / ".test-programs",
             bundle = package / "bundle", base = verified_base();
  fs::create_directory(scratch);
  save_file_registry(stage, src, registry, registry_pin);
  check(retained_binding_job(stage / "sources.json", projection.binding) == job,
        projection.directory() ? "changed tree projection job"
        : projection.json()    ? "changed JSON projection job"
                               : "changed ZIP projection job");
  auto defs = load(bundle / "providers/contracts.json");
  auto raw = select_binding(defs["providers"], projection.binding,
                            "miner_" + projection.binding,
                            "in:" + projection.input_type, "rows");
  auto record = file_binding_identity(raw, bundle);
  const auto origin = projection.json() ? readers_history : history;
  auto legacy = load(bundle / origin / "contracts.json")[projection.binding];
  check(raw["id"] == projection.binding &&
            raw["call"] == "brick_miner::shared::" + projection.binding &&
            raw["args"] == array({"fields"}) && raw["from"] == legacy["from"] &&
            raw["to"] == legacy["to"] &&
            legacy["args"] == object({{"fields", "text"}}) &&
            defs["outcomes"][projection.binding]["parameters"] ==
                legacy["args"],
        "file record binding differs from retained declaration");
  if (projection.json())
    verify_json_materials(stage, bundle, base, src, raw);
  else
    verify_archive_materials(stage, bundle, base, src, raw);
  const auto source = load(stage / "source.json");
  for (const auto &[from, to] :
       std::vector<std::pair<std::string, std::string>>{
           {projection.fixture + ".json", "spec.json"},
           {projection.fixture + "-client.cpp", "client.cpp"},
           {projection.fixture + "-annotation.json", "annotation.json"}})
    fs::copy_file(v5_root() / "fixtures" / from, stage / to);
  const auto spec = load(stage / "spec.json");
  check(spec["binding"] == record["key"] &&
            spec["parameters"] == job["parameter"],
        "qualification scope differs from reviewed binding or parameter");
  auto fixtures = stage / "fixtures";
  if (projection.directory()) {
    fixtures = work / "directories";
    for (const auto &fixture : spec["cases"]) {
      const auto dir = fixtures / fixture["name"].asString();
      fs::create_directories(dir);
      make_tree_input(dir, fixture);
    }
  } else if (projection.json()) {
    for (const auto &c : spec["cases"])
      if (c.isMember("input"))
        save(fixtures / c["name"].asString(), c["input"].asString());
  } else {
    make_zip_inputs(work, stage, spec);
  }

  auto old_path = qgetenv("PKG_CONFIG_PATH");
  const auto restore = qScopeGuard([&] {
    if (old_path.isNull())
      qunsetenv("PKG_CONFIG_PATH");
    else
      qputenv("PKG_CONFIG_PATH", old_path);
  });
  check(qputenv("PKG_CONFIG_PATH", (bundle / runtime / "pkgconfig").c_str()),
        "failed to set pinned runtime package path");
  auto options = flags({"jsoncpp", projection.json() ? "brick_shared_readers"
                                                     : "brick_shared_inputs"});
  auto exe = scratch / "client";
  Strings cmd{"g++-15",           "-std=c++17",         "-O1", "-I",
              stage / "original", stage / "client.cpp", "-o",  exe};
  append(cmd, options);
  if (!projection.json())
    cmd.push_back((bundle / runtime / "libarchive.so.13").string());
  cmd.push_back("-Wl,-rpath-link," + (bundle / runtime).string());
  run(cmd);
  auto tools = file_qualification_tools(), env = tools;
  auto tool_names = Strings{"bsdtar", "gpgv", "ldd"};
  if (projection.json())
    tool_names.push_back("nm");
  else if (!projection.directory())
    append(tool_names, {"tar", "unzip"});
  for (const auto &name : tool_names)
    env["tool_sha256"][name] =
        digest(QStandardPaths::findExecutable(QString::fromStdString(name))
                   .toStdString());
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
    if (projection.json() ? (name == readers_library || name == "libduckdb.so")
                          : (name == library || name == "libarchive.so.13"))
      check(env["resolved_client_libraries"][name]["sha256"] == f["sha256"] &&
                env["resolved_client_libraries"][name]["path"] ==
                    fs::canonical(bundle / runtime / name).string(),
            "standalone client linked the wrong retained runtime");
  }
  write(stage / "environment.json", env);
  write(stage / "standalone.json",
        repeated_calls(exe, fixtures, bundle, spec, projection));
  write(stage / "integration.json",
        ordinary(work, stage, bundle, base, raw, spec, projection));
  check(file_qualification_tools() == tools, "qualification tools changed");
  auto contract =
      file_contract(stage, package, record, src, stage / "sources.json");
  contract["source"]["implementation_sha256"] = source["kernel_sha256"];
  contract["qualification"]["scope"] =
      "Detached local evidence for one exact fields selector, " +
      projection.fields + ", on the pinned archived runtime and recorded " +
      (projection.directory() ? "directory"
       : projection.json()    ? "JSON"
                              : "ZIP") +
      " fixtures.";
  validate(contract, load(package / "contract.schema.json"));
  write(stage / "contract.json", contract);
  write(
      stage / "domain.json",
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
                                        {"value", projection.fields}})})},
              {"scope", "Evidence applies to the named parameter projection. "
                        "Use this detached domain record for review only."}}));
  return finish_file_stage(work, package, pin, record["logical_id"].asString(),
                           "unllm.scoped-file-evidence/1");
}
} // namespace

Path qualify_zip_records(const Path &package, const std::string &pin,
                         const Path &registry,
                         const std::string &registry_pin) {
  const Projection projection{
      "zip_file_records",
      "application/zip",
      "ext,parent",
      "H058",
      "zip-records",
      array({"op_0_zip_file_records", "op_1_filter_records", "op_2_column",
             "op_3_unique_lines", "op_4_sort_lines", "write_lines"})};
  return qualify_records(package, pin, registry, registry_pin, projection);
}

Path qualify_tree_records(const Path &package, const std::string &pin,
                          const Path &registry,
                          const std::string &registry_pin) {
  const Projection projection{
      "tree_file_records",
      "inode/directory",
      "parent,ext",
      "H070",
      "tree-records",
      array({"op_0_tree_file_records", "op_1_annotate_group_measure",
             "op_2_annotate_group_measure", "op_3_filter_records",
             "op_4_filter_records", "op_5_column", "op_6_unique_lines",
             "op_7_sort_lines", "write_lines"})};
  return qualify_records(package, pin, registry, registry_pin, projection);
}

Path qualify_json_records(const Path &package, const std::string &pin,
                          const Path &registry,
                          const std::string &registry_pin) {
  const Projection projection{
      "read_json_records",
      "application/json",
      R"(["sensor", "room", "minute", "temp"])",
      "H035",
      "json-records",
      array({"op_0_read_json_records", "op_1_derive_integer",
             "op_2_annotate_group_measure", "op_3_annotate_group_measure",
             "op_4_derive_integer", "op_5_group_rows", "op_6_select_group_row",
             "op_7_order_records", "op_8_report_fields", "write_lines"})};
  return qualify_records(package, pin, registry, registry_pin, projection);
}
} // namespace v5
