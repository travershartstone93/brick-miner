#include "bundle.hpp"
#include "migration_reports.hpp"
#include "pilot.hpp"
#include <QByteArray>
#include <algorithm>
#include <iterator>
#include <sstream>

namespace v5 {
namespace {
const std::string effect = "archive_files", zip_id = "archive_files_zip";

// The byte offsets refer only to the immutable 494-byte transfer fixture.
J spelling_case(const Path &dir, const J &suite, const J &c) {
  auto bytes = QByteArray::fromHex(
      QByteArray::fromStdString(suite["source_hex"].asString()));
  check(bytes.size() == 494 &&
            sha(bytes.toStdString()) == suite["source_sha256"].asString() &&
            bytes.mid(180, 4).toHex() == "504b0304" &&
            bytes.mid(414, 6).toHex() == "504b01021403" &&
            bytes.mid(206, 2).toHex() == "0300" &&
            bytes.mid(442, 2).toHex() == "0300",
        "unexpected local or Unix central-directory header in fixture");
  auto name = QByteArray::fromHex(
      QByteArray::fromStdString(c["stored_name_hex"].asString()));
  check(name.size() == 3 && name.toStdString() == c["members"][3].asString(),
        "inconsistent stored ZIP filename");
  for (auto offset : {210, 460}) {
    check(bytes.mid(offset, 3).toHex() == "615c62", "source name span changed");
    bytes.replace(offset, 3, name);
  }
  auto path = dir / "input";
  save(path, bytes.toStdString());
  check(digest(path) == c["input_sha256"].asString(), "changed spelling fixture");
  run({"unzip", "-tqq", path});
  auto raw = run({"unzip", "-Z1", path});
  auto reported = run({"bsdtar", "-tf", path});
  check(raw == join(strings(c["members"]), "\n") + "\n" &&
            reported == join(strings(c["expected"]), "\n") + "\n",
        "unmatched independent archive inventories");
  return object(
      {{"input_sha256", digest(path)},
       {"unzip_listing", raw},
       {"bsdtar_listing", reported},
       {"local_name_offset", 210},
       {"central_name_offset", 460},
       {"creator_os_byte", 3},
       {"local_name_hex", bytes.mid(210, 3).toHex().toStdString()},
       {"central_name_hex", bytes.mid(460, 3).toHex().toStdString()}});
}
J add_spelling_suite(const Path &stage, J &spec,
                     const std::string &registry_pin) {
  const auto current = retained_binding_job(v5_root() / "sources.json", zip_id);
  const auto job = retained_binding_job(stage / "sources.json", zip_id);
  if (job.isNull()) {
    auto prior = strings(current["prior_registry_sha256"]);
    check(!registry_pin.empty() && std::find(prior.begin(), prior.end(),
                                             registry_pin) != prior.end(),
          "current ZIP qualification needs a spelling suite");
    return J();
  }
  check(job == current && job["source"] == "retained-zip-member-names" &&
            job["route"] == "direct" && job["stream"] == "foundational" &&
            job["tests"] == "v5/fixtures/archive-spelling.json",
        "unreviewed ZIP spelling suite");
  auto fixture = project_file(root, job["tests"].asString());
  fs::copy_file(fixture, stage / "archive-spelling.json");
  check(digest(stage / "archive-spelling.json") == job["tests_sha256"].asString(),
        "ZIP spelling suite hash mismatch");
  const auto s = load(stage / "archive-spelling.json");
  for (const auto &c : s["cases"])
    spec["cases"].append(c);
  check(s["cases"].size() == 2 &&
            s["cases"][0]["members"] != s["cases"][1]["members"] &&
            s["cases"][0]["expected"] == s["cases"][1]["expected"],
        "missing equivalent member-name observations");
  write(stage / "spec.json", spec);
  auto a = load(stage / "annotation.json");
  a["interface"]["semantic_output"] = s["semantic_output"];
  a["claims"]["refinements"]["detail"] = s["refinements"];
  a["claims"]["refinements"]["evidence"].append("spelling-observations.json");
  write(stage / "annotation.json", a);
  return s;
}
void prepare(const Path &dir, const J &c) {
  fs::create_directories(dir);
  auto path = dir / "input";
  const auto format = c["format"].asString();
  if (format == "missing")
    return;
  if (format == "unrecognized") {
    save(path, std::string("\x04\x01\x02\x03", 4));
    return;
  }
  if (format == "empty-zip") {
    save(path, std::string("PK\005\006", 4) + std::string(18, '\0'));
    return;
  }
  auto files = dir / "members";
  for (const auto &name : {"z", "d/a.txt", "empty", "plain file", ".hidden"})
    save(files / name, name == std::string("empty") ? "" : "data");
  fs::create_symlink("z", files / "shortcut");
  auto tar = dir / "members.tar";
  Strings cmd = {"tar",
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
  append(cmd, strings(c["members"]));
  run(cmd, {.cwd = files});
  if (format == "tar")
    fs::copy_file(tar, path);
  else {
    check(format == "zip", "unknown fixture format");
    run({"bsdtar", "--format", "zip", "--options", "zip:compression=store",
         "-cf", path, "@" + tar.string()});
    check(run({"unzip", "-Z1", path}) ==
              join(strings(c["members"]), "\n") + "\n",
          "independent ZIP inventory disagrees with intended fixture");
    run({"unzip", "-tqq", path});
  }
}

J observe(const Path &exe, const Path &dir, const Path &fixture, const J &c,
          bool ordinary, bool existing = false, bool all = false) {
  fs::create_directories(dir);
  if (fs::exists(fixture))
    fs::copy_file(fixture, dir / "in");
  if (existing)
    save(dir / "out", "sentinel");
  auto expected_tree = tree_hashes(dir);
  auto cmd = sandbox(exe, dir);
  if (!ordinary)
    cmd.push_back(all ? "all" : "regular");
  cmd.push_back("in");
  if (ordinary)
    cmd.push_back("out");
  auto result = execute(cmd, {.seconds = 8, .probe_limits = true});
  bool fail = c.get("error", false).asBool();
  check(result.status == (fail ? 1 : 0),
        "archive case " + c["name"].asString() + " exit " +
            std::to_string(result.status) + " " + result.err);
  J observed;
  if (!fail) {
    auto out = ordinary ? bytes(dir / "out") : result.out;
    observed = parse(out);
    check(observed == c[all ? "members" : "expected"],
          "wrong member sequence for " + c["name"].asString() + ": " +
              dump(observed));
    if (ordinary)
      expected_tree["out"] = sha(out);
  } else if (!ordinary)
    check(result.out == "runtime_error", "missing expected archive exception");
  check(tree_hashes(dir) == expected_tree &&
            size_t(std::distance(fs::directory_iterator(dir),
                                 fs::directory_iterator())) ==
                expected_tree.size(),
        "unexpected filesystem effect in " + dir.string());
  return object({{"case", c["name"]},
                 {"exit", result.status},
                 {"input_sha256", expected_tree.get("in", J())},
                 {"names", observed},
                 {"destination_before",
                  ordinary ? J(existing ? "existing" : "absent") : J()},
                 {"destination_after_sha256", expected_tree.get("out", J())}});
}

J ordinary_trials(const Path &work, const Path &bundle, const J &row,
                  const J &cases) {
  auto task = [](const std::string &mime) {
    return spec_for(mime, "application/json",
                    object({{"outcome", effect},
                            {"parameters", object()},
                            {"uncovered", array()}}));
  };
  auto spec = task("application/zip"), chain = array({zip_id, "write_texts"});
  J report = object({{"spec", spec}, {"chain", chain}});
  for (const std::string arm : {"baseline", "staged"}) {
    auto dir = work / arm, exe = work / ".test-programs" / arm;
    auto p = complete_file_plan(arm == "baseline" ? verified_base() : bundle,
                                dir, spec, chain);
    build_emission(p, dir, exe, "g++-15");
    J seen = array();
    for (const auto &c : cases) {
      auto name = c["name"].asString();
      for (bool existing : {false, true})
        seen.append(
            observe(exe, dir / name / (existing ? "existing" : "absent"),
                    work / "fixtures" / name / "input", c, true, existing));
    }
    report[arm] = object({{"program_sha256", digest(exe)},
                          {"emission_sha256", digest(dir / "component.hpp")},
                          {"input_headers", tree_hashes(dir / "bricks")},
                          {"cases", seen}});
  }
  auto reduced = work / "control-bundle";
  write_tree(reduced, remove_binding(read_tree(bundle), row));
  const auto removed = plan(reduced, spec, work / "removed");
  check(removed["clarifications"].empty() &&
            removed["interpretation_errors"].empty(),
        "unrelated removal error");
  for (const auto &p : removed["providers"])
    check(p["status"] == "partial" && !p["typed_gaps"].empty(),
          "ZIP binding removal still delivered the goal");
  check(!removed["gaps"].empty(), "ZIP removal diagnostic missing");
  J partial = array();
  for (const auto &p : removed["providers"])
    partial.append(object({{"steps", p["plan"]["steps"]},
                           {"typed_gaps", p["typed_gaps"]},
                           {"uncovered", p["uncovered"]}}));
  report["removed"] = object({{"bundle_sha256", digest(reduced / "SHA256SUMS")},
                              {"gaps", removed["gaps"]},
                              {"partial", partial}});
  auto dir = work / "tar-sibling", exe = work / ".test-programs/tar-sibling";
  auto ids = array({effect, "write_texts"});
  auto p = complete_file_plan(reduced, dir, task("application/x-tar"), ids);
  build_emission(p, dir, exe, "g++-15");
  for (const auto &c : cases)
    if (c["format"] == "tar")
      report["tar_sibling"] =
          object({{"chain", ids},
                  {"program_sha256", digest(exe)},
                  {"observed",
                   observe(exe, dir / "check",
                           work / "fixtures" / c["name"].asString() / "input",
                           c, true)}});
  check(report["tar_sibling"].isObject(), "missing TAR control");
  return report;
}
} // namespace

Path qualify_archive_names(const Path &package, const std::string &pin,
                           const Path &registry,
                           const std::string &registry_pin) {
  verify(package, pin);
  auto src = sources()["retained-zip-member-names"];
  check(src["approval"] == "local-pilot" &&
            digest(src["archive"].asString()) == src["sha256"].asString(),
        "unapproved or changed archive reader");
  auto work = fresh_run("v5-zip-names"), stage = work / ".stage",
       scratch = work / ".test-programs", bundle = package / "bundle";
  fs::create_directory(scratch);
  auto source = retained_source(stage, src["id"].asString(), "project-LICENSE");
  save_file_registry(stage, src, registry, registry_pin);
  auto defs = load(bundle / "providers/contracts.json");
  auto row = [&](const std::string &mime) {
    return select_binding(defs["providers"], effect, "x42", "in:" + mime,
                          "strings");
  };
  auto zip = row("application/zip"), tar = row("application/x-tar"),
       record = file_binding_identity(zip, bundle);
  auto aliased = tar;
  aliased["id"] = zip_id;
  aliased["from"] = "in:application/zip";
  check(aliased == zip && zip["args"] == array() &&
            zip["call"] == src["symbols"][0] &&
            record["header_sha256"] ==
                digest(stage / "original/headers/archive_ops.hpp"),
        "unexpected ZIP alias");
  auto archived = [&](const std::string &key) {
    auto data = archive_member(src, src[key].asString());
    check(sha(data) == src[key + "_sha256"].asString(),
          "changed archived " + key);
    return data;
  };
  auto docs = archived("documentation");
  auto declaration = effect + "|" + tar["from"].asString() + "|strings|" +
                     tar["call"].asString() + "||" +
                     defs["outcomes"][effect]["meaning"].asString();
  check(docs.find("\n" + declaration + "\n") != std::string::npos,
        "missing upstream operation");
  auto registration = archived("registration");
  check(registration.find("zip.set(\"id\", Json::string(effect + \"_zip\"))") !=
                std::string::npos &&
            registration.find(
                "zip.set(\"from\", Json::string(\"in:application/zip\"))") !=
                std::string::npos,
        "missing MIME alias evidence");
  source["declaration"] = declaration;
  source["registration_sha256"] = sha(registration);
  source["zip_binding"] = zip;
  source["tar_sibling"] = tar;
  source["tar_example"] = parse(archived("examples"))["cases"][effect];
  write(stage / "source.json", source);
  auto dep = src["runtime"];
  auto info = verified_runtime_package(work, dep);
  Strings lines = {"pkgname = libarchive",
                   "pkgver = " + dep["pkgver"].asString(),
                   "arch = " + dep["arch"].asString(),
                   "url = " + dep["upstream"].asString()};
  for (const auto &lic : dep["licenses"]) {
    lines.push_back("license = " + lic.asString());
    auto name = lic.asString() + ".txt";
    fs::copy_file(Path("/usr/share/licenses/libarchive") / name,
                  stage / "notices" / name);
  }
  for (const auto &line : lines)
    check(info.find("\n" + line + "\n") != std::string::npos,
          "libarchive package metadata mismatch");
  auto toolchain = file_qualification_tools(), env = toolchain;
  for (auto name : {"bsdtar", "tar", "unzip"})
    env["fixture_tools"][name] = digest(Path("/usr/bin") / name);
  env["libarchive_version"] = run({"pkg-config", "--modversion", "libarchive"});
  env["pkginfo_sha256"] = sha(info);
  env["runtime_keyring_sha256"] = digest(work / "runtime-keyring.gpg");
  for (const auto &[from, to] :
       std::vector<std::pair<std::string, std::string>>{
           {"archive-names.json", "spec.json"},
           {"archive-names-client.cpp", "client.cpp"},
           {"archive-names-annotation.json", "annotation.json"}})
    fs::copy_file(v5_root() / "fixtures" / from, stage / to);
  auto spec = load(stage / "spec.json");
  auto spelling = add_spelling_suite(stage, spec, registry_pin);
  check(spec["binding"] == record["key"], "wrong archive binding");
  auto exe = scratch / "direct";
  Strings cmd = {"g++-15",
                 "-std=c++17",
                 "-fPIC",
                 "-O1",
                 "-I",
                 stage / "original/headers",
                 stage / "client.cpp",
                 "-o",
                 exe};
  append(cmd, flags({"libarchive", "Qt6Core", "jsoncpp"}));
  run(cmd);
  std::istringstream linked(run({"ldd", exe}));
  for (std::string line; std::getline(linked, line);) {
    std::istringstream values(line);
    std::string name, arrow, path;
    if (values >> name >> arrow >> path && name == "libarchive.so.13" &&
        arrow == "=>")
      env["linked_libarchive_sha256"] = digest(path);
  }
  check(env["linked_libarchive_sha256"] ==
            dep["files"]["usr/lib/libarchive.so.13.8.11"],
        "loaded different libarchive");
  J direct = array(), all, spellings;
  for (const auto &c : spec["cases"]) {
    auto name = c["name"].asString();
    auto dir = work / "fixtures" / name, input = dir / "input";
    if (c["format"] == "pinned-spelling")
      spellings[name] = spelling_case(dir, spelling, c);
    else
      prepare(dir, c);
    direct.append(observe(exe, work / "direct" / name, input, c, false));
    if (name == "archive-order")
      all = observe(exe, work / "all-entry-control", input, c, false, false,
                    true);
  }
  if (!spelling.isNull())
    write(stage / "spelling-observations.json", spellings);
  check(all["names"].size() == 6, "all-entry witness missing");
  write(stage / "standalone.json",
        object({{"cases", direct}, {"all_entry_specialization", all}}));
  write(stage / "integration.json",
        ordinary_trials(work, bundle, zip, spec["cases"]));
  check(toolchain == file_qualification_tools(),
        "archive tools changed during qualification");
  write(stage / "environment.json", env);
  auto contract =
      file_contract(stage, package, record, src, stage / "sources.json");
  return seal_file_qualification(work, package, pin, array({record}), 0,
                                 contract);
}
} // namespace v5
