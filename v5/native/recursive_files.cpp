#include "bundle.hpp"
#include "migration_reports.hpp"
#include "pilot.hpp"
#include <sys/stat.h>

namespace v5 {
namespace {
const std::string binding_id = "recursive_files";

void make_case(const Path &dir, const J &fixture) {
  fs::create_directories(dir);
  for (const auto &p : fixture["dirs"])
    fs::create_directories(dir / p.asString());
  for (const auto &p : fixture["files"])
    save(dir / p.asString(), "fixture");
  for (const auto &p : fixture["hardlinks"].getMemberNames())
    fs::create_hard_link(dir / fixture["hardlinks"][p].asString(), dir / p);
  for (const auto &p : fixture["symlinks"].getMemberNames())
    fs::create_symlink(fixture["symlinks"][p].asString(), dir / p);
  for (const auto &p : fixture["fifos"])
    check(::mkfifo((dir / p.asString()).c_str(), 0600) == 0,
          "create fixture FIFO");
}

J direct_checks(const Path &exe, const Path &work, const J &cases) {
  J tested = array(), shallow;
  for (const auto &c : cases) {
    auto dir = work / c["name"].asString();
    make_case(dir, c);
    auto cmd = sandbox(exe, dir);
    append(cmd, {"recursive", "input"});
    const bool failure = c.isMember("error");
    auto p = execute(cmd, {.seconds = 8, .probe_limits = true});
    check(p.status == (failure ? 1 : 0),
          "unexpected direct result " + c["name"].asString());
    J observed = failure ? J(p.out) : parse(p.out);
    check(observed == c[failure ? "error" : "expected"],
          "direct result mismatch " + c["name"].asString());
    tested.append(object(
        {{"case", c["name"]}, {"exit", p.status}, {"observed", observed}}));
    if (c["name"] == "nested-links-and-names") {
      cmd[cmd.size() - 2] = "shallow";
      auto result = run(cmd, {.seconds = 8, .probe_limits = true});
      shallow = parse(result);
      check(shallow == array({".hidden", "z"}) && shallow != observed,
            "shallow sibling did not distinguish recursion");
    }
  }
  check(!shallow.isNull(), "missing shallow control");
  return object(
      {{"cases", tested}, {"shallow_sibling_on_nested_tree", shallow}});
}

J ordinary_cases(const Path &exe, const Path &work, const J &cases) {
  J tested = array();
  for (const auto &c : cases) {
    bool failure = c.isMember("error");
    for (bool existing : {false, true}) {
      auto dir =
          work / (existing ? "existing" : "absent") / c["name"].asString();
      make_case(dir, c);
      if (existing)
        save(dir / "out", "sentinel");
      auto cmd = sandbox(exe, dir);
      append(cmd, {"input", "out"});
      auto p = execute(cmd, {.seconds = 8, .probe_limits = true});
      check(p.status == (failure ? 1 : 0),
            "unexpected ordinary exit " + c["name"].asString());
      bool present = fs::exists(dir / "out");
      check(present == (!failure || existing), "unexpected destination state");
      J out;
      if (present) {
        auto raw = bytes(dir / "out");
        out = failure ? J(raw) : parse(raw);
        check(out == (failure ? J("sentinel") : c["expected"]),
              "unexpected destination contents " + c["name"].asString());
      }
      tested.append(object({{"case", c["name"]},
                            {"destination_existed", existing},
                            {"exit", p.status},
                            {"destination_exists", present},
                            {"output", out}}));
    }
  }
  return tested;
}

J trial(const Path &work, const Path &bundle, const J &binding,
        const J &cases) {
  auto cut = work / "control-bundle";
  write_tree(cut, remove_binding(read_tree(bundle), binding));
  auto spec = spec_for("inode/directory", "application/json",
                       object({{"outcome", binding_id},
                               {"parameters", object()},
                               {"uncovered", array()}}));
  J result = object({{"spec", spec}});
  for (const std::string arm : {"baseline", "staged", "removed"}) {
    Path dir = work / arm;
    const auto report = plan(arm == "removed"    ? cut
                             : arm == "baseline" ? verified_base()
                                                 : bundle,
                             spec, dir);
    check(report["interpretation_errors"].empty() &&
              report["clarifications"].empty(),
          "unrelated directory planning failure");
    if (arm == "removed") {
      check(report["providers"].isArray() && report["providers"].empty() &&
                report["gaps"]["r"].asString().find(binding_id) !=
                    std::string::npos,
            "recursive removal failed to isolate the missing outcome: " +
                dump(report));
      result[arm] = object({{"bundle_sha256", digest(cut / "SHA256SUMS")},
                            {"providers", report["providers"]},
                            {"gaps", report["gaps"]}});
      continue;
    }
    check(report["providers"].size() == 1, "expected one directory program");
    const auto &p = report["providers"][0];
    J chain = array();
    for (const auto &step : p["plan"]["steps"])
      chain.append(step["provider"]);
    check(chain == array({binding_id, "write_texts"}) &&
              p["status"] == "complete" && p["uncovered"].empty(),
          "wrong ordinary directory route: " + dump(p));
    auto exe = work / ".test-programs" / arm;
    build_emission(p, dir, exe, "g++-15");
    result[arm] = object({{"chain", chain},
                          {"emission_sha256", digest(dir / "component.hpp")},
                          {"program_sha256", digest(exe)},
                          {"input_headers", tree_hashes(dir / "bricks")},
                          {"cases", ordinary_cases(exe, dir, cases)}});
  }
  auto shallow_spec = spec;
  shallow_spec["roles"][0]["requirement"]["outcome"] = "file_names";
  auto sibling = work / "remaining-shallow";
  const auto report = plan(cut, shallow_spec, sibling);
  const auto &p = report["providers"][0];
  const auto &steps = p["plan"]["steps"];
  check(report["providers"].size() == 1 && p["status"] == "complete" &&
            steps.size() == 2 && steps[0]["provider"] == "file_names" &&
            steps[1]["provider"] == "write_texts",
        "removal affected the shallow reader");
  auto exe = work / ".test-programs/remaining-shallow";
  build_emission(p, sibling, exe, "g++-15");
  J probe;
  for (const auto &c : cases)
    if (c["name"] == "nested-links-and-names")
      probe = c;
  check(probe.isObject(), "missing sibling control fixture");
  probe["expected"] = array({".hidden", "z"});
  result["remaining_shallow"] =
      object({{"steps", steps},
              {"program_sha256", digest(exe)},
              {"cases", ordinary_cases(exe, sibling, array({probe}))}});
  return result;
}
} // namespace

Path qualify_recursive_files(const Path &package, const std::string &pin,
                             const Path &registry,
                             const std::string &registry_pin) {
  verify(package, pin);
  const auto src = sources()["retained-recursive-files"];
  check(src["approval"] == "local-pilot" &&
            digest(src["archive"].asString()) == src["sha256"].asString(),
        "unapproved or changed directory provider source");
  auto work = fresh_run("v5-recursive-files"), stage = work / ".stage",
       scratch = work / ".test-programs";
  fs::create_directory(scratch);
  auto source = retained_source(stage, src["id"].asString(), "project-LICENSE");
  auto bundle = package / "bundle";
  auto rows = load(bundle / "providers/contracts.json")["providers"];
  J raw;
  for (const auto &r : rows)
    if (r["id"] == binding_id) {
      check(raw.isNull(), "ambiguous recursive directory binding");
      raw = r;
    }
  auto record = file_binding_identity(raw, bundle);
  check(raw["effect"] == binding_id && raw["args"] == array() &&
            raw["call"] == src["symbols"][0] &&
            record["header_sha256"] ==
                digest(stage / "original/headers/path_ops.hpp"),
        "directory binding mismatch");
  save_file_registry(stage, src, registry, registry_pin);
  for (const auto &[file, dest] :
       std::vector<std::pair<std::string, std::string>>{
           {"recursive-files-client.cpp", "client.cpp"},
           {"recursive-files.json", "spec.json"},
           {"recursive-files-annotation.json", "annotation.json"}})
    fs::copy_file(v5_root() / "fixtures" / file, stage / dest);
  const auto spec = load(stage / "spec.json");
  check(record["key"] == spec["binding"],
        "unexpected directory structural ports");
  auto archived = [&](const std::string &field) {
    auto text = run(
        {"bsdtar", "-xOf", src["archive"].asString(), src[field].asString()});
    check(sha(text) == src[field + "_sha256"].asString(), "changed " + field);
    return text;
  };
  const auto doc = archived("documentation");
  const auto start = doc.find("\nrecursive_files|");
  check(start != std::string::npos, "missing upstream directory declaration");
  source["declaration"] =
      doc.substr(start + 1, doc.find('\n', start + 1) - start - 1);
  source["archived_example"] = parse(archived("examples"))["cases"][binding_id];
  check(source["archived_example"]["expected"] ==
                spec["cases"][0]["expected"] &&
            spec["cases"][0]["name"] == "archived-example",
        "changed project example");
  source["binding"] = raw;
  write(stage / "source.json", source);
  auto env = file_qualification_tools();
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
  write(stage / "standalone.json",
        direct_checks(exe, work / "direct", spec["cases"]));
  write(stage / "integration.json", trial(work, bundle, raw, spec["cases"]));
  check(env == file_qualification_tools(),
        "directory toolchain changed during qualification");
  write(stage / "environment.json", path_client_environment(exe, env));
  auto contract =
      file_contract(stage, package, record, src, stage / "sources.json");
  return seal_file_qualification(work, package, pin, array({record}), 0,
                                 contract);
}
} // namespace v5
