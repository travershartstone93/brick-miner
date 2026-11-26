#include "bundle.hpp"
#include "migration_reports.hpp"
#include "pilot.hpp"
#include <QByteArray>
#include <algorithm>
#include <sstream>

namespace v5 {
namespace {
const std::string id = "gir_g_uri_list_extract_uris";
J runtime(const Path &work, const J &s) {
  auto info = verified_runtime_package(work, s);
  const std::string license = "SPDX-License-Identifier: LGPL-2.1-or-later";
  check(bytes("/usr/include/glib-2.0/glib/gconvert.h").find(license) !=
            std::string::npos,
        "missing GLib header notice");
  for (const auto &line :
       Strings{"pkgname = glib2", "pkgver = " + s["pkgver"].asString(),
               "arch = " + s["arch"].asString(), "license = LGPL-2.1-or-later",
               "url = " + s["upstream"].asString()})
    check(info.find("\n" + line + "\n") != std::string::npos,
          "unexpected GLib metadata");
  return object({{"archive", s},
                 {"pkginfo_sha256", sha(info)},
                 {"version", run({"pkg-config", "--modversion", "glib-2.0"})},
                 {"keyring_sha256", digest(work / "runtime-keyring.gpg")},
                 {"dependency_header_license", license}});
}
void build(const Path &include, const Path &source, const Path &exe,
           Strings extra = {}) {
  Strings cmd = {"g++-15", "-std=c++17", "-fPIC", "-O1", "-I", include};
  append(cmd, extra);
  append(cmd, {source, "-o", exe});
  append(cmd, flags({"glib-2.0"}));
  run(cmd);
}
J observe(const Path &exe, const Path &dest, const J &c, bool ordinary) {
  fs::create_directories(dest);
  std::string input;
  if (c.isMember("input_hex")) {
    auto hex = QByteArray::fromStdString(c["input_hex"].asString());
    auto raw = QByteArray::fromHex(hex);
    check(raw.toHex() == hex, "invalid reader byte fixture");
    input = raw.toStdString();
  } else
    input = c["input"].asString();
  const bool supplied = c.isMember("input_hex") || c.isMember("input");
  if (supplied)
    save(dest / "in", input);
  auto cmd = sandbox(exe, dest);
  cmd.push_back("in");
  if (ordinary) {
    save(dest / "out", "prior");
    cmd.push_back("out");
  }
  auto proc = execute(cmd, {.seconds = 8, .probe_limits = true});
  const bool failure = c.isMember("error");
  bool passed = proc.status == (failure ? 1 : 0);
  std::string out = proc.out;
  if (ordinary) {
    out = bytes(dest / "out");
    if (failure)
      passed &= out == "prior";
    else {
      auto expected = strings(c["records"]);
      std::reverse(expected.begin(), expected.end());
      passed &= parse(out) == string_array(expected);
    }
  } else {
    std::string expected;
    if (failure)
      expected = c["error"].asString() + "\n";
    else
      for (const auto &value : c["records"])
        expected +=
            QByteArray::fromStdString(value.asString()).toHex().toStdString() +
            "\n";
    passed &= out == expected;
  }
  if (supplied)
    passed &= bytes(dest / "in") == input;
  return object({{"case", c["name"]},
                 {"exit", proc.status},
                 {"observed", out},
                 {"passed", passed}});
}
J checks(const Path &exe, const Path &dest, const J &cases, bool ordinary) {
  J results = array();
  for (const auto &c : cases) {
    auto obs = observe(exe, dest / c["name"].asString(), c, ordinary);
    check(obs["passed"] == true, "reader case failed: " + dump(obs));
    results.append(obs);
  }
  return results;
}
J ordinary_trials(const Path &work, const Path &bundle, const J &binding,
                  const J &cases) {
  auto spec = spec_for("text/uri-list", "application/json",
                       object({{"outcome", "reverse_order"},
                               {"parameters", object()},
                               {"uncovered", array()}}));
  auto cut = work / "control-bundle";
  write_tree(cut, remove_binding(read_tree(bundle), binding));
  auto base = verified_base();
  J result = object({{"spec", spec}});
  for (std::string arm : {"baseline", "staged", "removed"}) {
    auto dir = work / arm;
    auto emission = plan(arm == "baseline"  ? base
                         : arm == "removed" ? cut
                                            : bundle,
                         spec, dir);
    check(emission["interpretation_errors"].empty() &&
              emission["clarifications"].empty(),
          "unrelated URI interpretation failure");
    if (arm == "removed") {
      const auto &p = emission["providers"][0];
      const auto &holes = p["typed_gaps"];
      check(emission["providers"].size() == 1 && p["status"] == "partial" &&
                holes.size() == 1 && holes[0]["from"] == binding["from"] &&
                p["uncovered"].empty(),
            "reader removal did not isolate the missing input");
      result[arm] = object({{"bundle_sha256", digest(cut / "SHA256SUMS")},
                            {"gaps", emission["gaps"]},
                            {"steps", p["plan"]["steps"]},
                            {"typed_gaps", holes}});
      continue;
    }
    auto p = emission["providers"][0];
    J chain = array();
    for (const auto &step : p["plan"]["steps"])
      chain.append(step["provider"]);
    check(p["status"] == "complete" && p["uncovered"].empty() &&
              emission["providers"].size() == 1 &&
              chain == array({id, "reverse_order", "write_texts"}),
          "incorrect URI route: " + dump(chain));
    auto exe = work / ".test-programs" / arm;
    build_emission(p, dir, exe, "g++-15");
    result[arm] = object({{"bindings", chain},
                          {"program_sha256", digest(exe)},
                          {"emitted_sha256", digest(dir / "component.hpp")},
                          {"input_headers", tree_hashes(dir / "bricks")},
                          {"cases", checks(exe, dir, cases, true)}});
  }
  return result;
}
} // namespace
Path qualify_uri_reader(const Path &package, const std::string &pin,
                        const Path &registry, const std::string &registry_pin) {
  verify(package, pin);
  const auto s = sources()["retained-gir-uri-reader"];
  check(s["approval"] == "local-pilot" &&
            digest(s["archive"].asString()) == s["sha256"].asString(),
        "unapproved URI source");
  auto work = fresh_run("v5-uri-reader"), stage = work / ".stage",
       scratch = work / ".test-programs";
  fs::create_directory(scratch);
  const auto bundle = package / "bundle";
  auto raw = load(bundle / "providers/contracts.json")["providers"];
  J binding;
  for (const auto &row : raw)
    if (row["id"] == id) {
      check(binding.isNull(), "duplicate URI binding");
      binding = row;
    }
  check(binding["brick"] == id && !binding.isMember("effect"),
        "incorrect URI binding");
  auto record = file_binding_identity(binding, bundle);
  J source = retained_source(stage, s["id"].asString(), "project-LICENSE");
  auto env = runtime(work, s["runtime"]);
  auto header = binding["header"].asString();
  auto include = stage / "original/headers";
  check(digest(include / header) == record["header_sha256"].asString(),
        "URI header differs from executable parent");
  const Path generator = main_repo / "build/native/brick-gir-adapter",
             gir = s["documentation"].asString();
  auto generator_sha = digest(generator);
  auto generator_source = archive_member(s, s["generator"].asString());
  check(sha(generator_source) == digest(main_repo / s["generator"].asString()),
        "generator source revision mismatch");
  save(stage / "historical-client.cpp", archive_member(s, s["examples"].asString()));
  save_file_registry(stage, s, registry, registry_pin);
  for (auto [f, d] : std::vector<std::pair<std::string, std::string>>{
           {"uri-reader-client.cpp", "client.cpp"},
           {"uri-reader.json", "spec.json"},
           {"uri-reader-annotation.json", "annotation.json"}})
    fs::copy_file(v5_root() / "fixtures" / f, stage / d);
  auto fixture = load(stage / "spec.json");
  check(record["key"] == fixture["binding"], "URI contract binding mismatch");
  write(work / "typed-gap.json",
        object({{"reader",
                 object({{"from", binding["from"]}, {"to", binding["to"]}})}}));
  for (std::string mode : {"whole-file", "per-line"}) {
    auto generated =
        parse(run({generator, work / "typed-gap.json", gir, mode}));
    check(generated["metadata_sha256"] == digest(gir) &&
              generated["symbol"] == s["symbols"][0] && generated["id"] == id &&
              generated["call"] == binding["call"] && generated["mode"] == mode,
          "generated URI identity mismatch");
    auto code = generated["source"].asString() + "\n";
    generated.removeMember("source");
    generated["header_sha256"] = sha(code);
    source["generated"][mode] = generated;
    if (mode == "per-line")
      check(code == bytes(include / header),
            "GIR regeneration differs from retained bytes");
    else
      save(work / "whole-file" / header, code);
  }
  source["binding"] = binding;
  source["generator_source_sha256"] = sha(generator_source);
  source["runtime"] = env;
  source["rights"] = s["rights"];
  write(stage / "source.json", source);
  auto exe = scratch / "direct";
  build(include, stage / "client.cpp", exe);
  const auto linked = run({"ldd", exe});
  check(linked.find("libglib-2.0.so.0 => /") != std::string::npos,
        "client did not link GLib");
  std::istringstream libs(linked);
  for (std::string line; std::getline(libs, line);) {
    std::string name, arrow, path;
    std::istringstream fields(line);
    if (fields >> name >> arrow >> path && name == "libglib-2.0.so.0" &&
        arrow == "=>")
      env["linked_library_sha256"] = digest(path);
  }
  check(env["linked_library_sha256"] ==
            s["runtime"]["files"]["usr/lib/libglib-2.0.so.0.9000.1"],
        "running reader uses different GLib bytes");
  auto cases = fixture["cases"],
       direct = checks(exe, work / "client-checks", cases, false);
  auto trial = scratch / "whole-file";
  build(work / "whole-file", stage / "client.cpp", trial);
  J wrong = array();
  for (const auto &c : cases) {
    auto obs = observe(trial, work / "whole-file-cases" / c["name"].asString(),
                       c, false);
    if (obs["passed"] == false)
      wrong.append(obs);
  }
  check(std::any_of(wrong.begin(), wrong.end(),
                    [](const J &w) {
                      return w["case"] == "comment-after-blank" &&
                             w["exit"] == 0;
                    }),
        "whole-file variant lacked its historical semantic counterexample");
  auto archived = scratch / "archived-client";
  build(
      include, stage / "historical-client.cpp", archived,
      {"-include", header, "-DUNLLM_TEST_BRICK=" + binding["call"].asString()});
  auto historical = work / "historical-cases";
  fs::create_directory(historical);
  run(sandbox(archived, historical), {.seconds = 8, .probe_limits = true});
  write(stage / "standalone.json",
        object({{"cases", direct},
                {"whole_file_mismatches", wrong},
                {"archived_client", "passed"},
                {"raw_binding_sha256", compact_hash(binding)},
                {"direct_program_sha256", digest(exe)}}));
  write(stage / "integration.json",
        ordinary_trials(work, bundle, binding, cases));
  env["compiler_sha256"] = digest("/usr/bin/g++-15");
  env["generator_sha256"] = generator_sha;
  check(digest(generator) == generator_sha &&
            digest(gir) ==
                s["runtime"]["files"]["usr/share/gir-1.0/GLib-2.0.gir"]
                    .asString(),
        "GIR context changed");
  write(stage / "environment.json", env);
  auto contract =
      file_contract(stage, package, record, s, stage / "sources.json");
  return seal_file_qualification(work, package, pin, array({record}), 0,
                                 contract);
}
} // namespace v5
