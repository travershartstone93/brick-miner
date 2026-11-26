#include "bundle.hpp"
#include "migration_reports.hpp"
#include "pilot.hpp"
#include <QByteArray>
#include <algorithm>
#include <sstream>

namespace v5 {
namespace {
const std::string source_id = "retained-text-measures";
std::string unhex(const J &value) {
  check(value.isString(), "missing byte fixture");
  auto hex = QByteArray::fromStdString(value.asString());
  auto raw = QByteArray::fromHex(hex);
  check(raw.toHex() == hex, "invalid hex in writer fixture");
  return raw.toStdString();
}
J observe(const Path &exe, const Path &dir, const J &c) {
  fs::create_directories(dir);
  const bool direct = c.isMember("records");
  const auto fault = c["fault"].asString();
  const std::string target = fault == "missing-parent" ? "missing/out" : "out";
  if (!direct)
    save(dir / "in", unhex(c["input_hex"]));
  if (c.get("existing", false).asBool())
    save(dir / "out", "prior");
  if (fault == "directory")
    save(dir / "out/member", "untouched");
  auto expected_files = tree_hashes(dir);
  auto cmd = sandbox(exe, dir);
  if (fault == "limit-8") {
    check(cmd.back() == "/probe", "unexpected sandbox executable");
    cmd.insert(cmd.end() - 1, {"/usr/bin/env", "--ignore-signal=XFSZ",
                               "/usr/bin/prlimit", "--fsize=8:8", "--"});
  }
  if (!direct)
    cmd.push_back("in");
  cmd.push_back(target);
  size_t total = 0;
  if (direct)
    for (const auto &record : c["records"]) {
      total += unhex(record).size() + 1;
      cmd.push_back(record.asString());
    }
  auto result = execute(cmd, {.seconds = 8, .probe_limits = true});
  bool passed = result.status == (fault.empty() ? 0 : 1);
  if (fault.empty()) {
    auto expected = unhex(c["expected_hex"]);
    check(!direct || expected.size() == total,
          "inconsistent byte-count oracle");
    expected_files[target] = sha(expected);
  } else if (direct) {
    auto allowed = strings(c["exception"]);
    passed &= std::any_of(allowed.begin(), allowed.end(), [&](const auto &msg) {
      return result.out == msg + "\n";
    });
  }
  const auto actual = tree_hashes(dir);
  passed &= actual == expected_files;
  if (fault == "directory")
    passed &= fs::is_directory(dir / target);
  if (fault == "missing-parent")
    passed &= !fs::exists(dir / "missing");
  if (fault == "limit-8" && !c.get("existing", false).asBool())
    passed &= !fs::exists(dir / target);
  return object({{"case", c["name"]},
                 {"exit", result.status},
                 {"stdout", result.out},
                 {"files_sha256", actual},
                 {"passed", passed}});
}
J suite(const Path &exe, const Path &dir, const J &cases) {
  J observations = array();
  for (const auto &c : cases) {
    auto obs = observe(exe, dir / c["name"].asString(), c);
    check(obs["passed"] == true,
          "writer probe failed in " + dir.string() + ": " + dump(obs));
    observations.append(obs);
  }
  return observations;
}
void compile_client(const Path &source, const Path &headers, const Path &types,
                    const Path &exe) {
  Strings argv = {"g++-15", "-std=c++17", "-fPIC", "-O1", "-I", headers,
                  "-I",     types,        source,  "-o",  exe};
  append(argv, flags({"Qt6Core"}));
  run(argv);
}
J standalone(const Path &stage, const Path &work, const J &fixture) {
  auto hdr = stage / "original/headers", types = stage / "original/providers",
       scratch = work / ".test-programs", exe = scratch / "writer-client";
  auto client = stage / "client.cpp";
  compile_client(client, hdr, types, exe);
  auto observed = suite(exe, work / "direct", fixture["standalone"]);
  auto header = bytes(hdr / "text_ops.hpp");
  J controls = array();
  struct Change {
    std::string name, from, to, witness;
  };
  for (const Change &m : std::vector<Change>{
           {"cr-terminator", "file.putChar('\\n')", "file.putChar('\\r')",
            "one-empty-record"},
           {"nul-truncation", "file.write(line.data(), line.size())",
            "file.write(line.c_str())", "binary-and-utf8"},
           {"silent-open-failure",
            "throw std::runtime_error(\"Cannot open output\");", "return;",
            "directory"}}) {
    auto dir = work / "mutants" / m.name;
    auto found = header.find(m.from);
    check(found != std::string::npos &&
              header.find(m.from, found + 1) == std::string::npos,
          "ambiguous writer mutation");
    auto modified = header;
    modified.replace(found, m.from.size(), m.to);
    save(dir / "text_ops.hpp", modified);
    auto binary = scratch / m.name;
    compile_client(client, dir, types, binary);
    const auto &cases = fixture["standalone"];
    auto test = std::find_if(cases.begin(), cases.end(), [&](const J &c) {
      return c["name"] == m.witness;
    });
    check(test != cases.end(), "missing mutation witness");
    auto obs = observe(binary, dir / "case", *test);
    check(obs["passed"] == false && (obs["exit"] == 0 || obs["exit"] == 1),
          "writer mutant survived or failed outside the probe");
    controls.append(object({{"mutation", m.name},
                            {"source_sha256", sha(modified)},
                            {"observed", obs}}));
  }
  return object({{"binding", "write_lines"},
                 {"cases", observed},
                 {"rejected_mutants", controls},
                 {"client_sha256", digest(client)},
                 {"program_sha256", digest(exe)}});
}
J complete(const Path &bundle, const Path &out, const J &spec,
           const J &expected) {
  auto emitted = plan(bundle, spec, out);
  check(emitted["providers"].size() == 1 &&
            emitted["interpretation_errors"].empty() &&
            emitted["clarifications"].empty(),
        "writer interpretation did not bind");
  auto p = emitted["providers"][0];
  J ids = array();
  for (const auto &step : p["plan"]["steps"])
    ids.append(step["provider"]);
  check(p["status"] == "complete" && p["uncovered"].empty() &&
            p["typed_gaps"].empty() && ids == expected,
        "unexpected emitted bindings " + dump(ids));
  return p;
}
J integration(const Path &base, const Path &bundle, const J &binding,
              const Path &work, const J &fixture) {
  J requirement = object({{"outcome", "sort_lines"},
                          {"parameters", object()},
                          {"uncovered", array()}});
  auto spec = spec_for("text/plain", "text/plain", requirement);
  const auto chain = array({"read_lines", "sort_lines", "write_lines"});
  J result = object({{"spec", spec}, {"bindings", chain}});
  for (std::string label : {"baseline", "staged"}) {
    auto dir = work / label, bin = work / ".test-programs" / label;
    auto p = complete(label == "baseline" ? base : bundle, dir, spec, chain);
    build_emission(p, dir, bin, "g++-15");
    result[label] =
        object({{"program_sha256", digest(bin)},
                {"emitted_sha256", digest(dir / "component.hpp")},
                {"main_sha256", digest(dir / "main.cpp")},
                {"helpers_sha256", digest(dir / "components/helpers.json")},
                {"input_headers", tree_hashes(dir / "bricks")},
                {"cases", suite(bin, dir, fixture["ordinary"])}});
  }
  check(result["baseline"]["cases"] == result["staged"]["cases"],
        "baseline vs staged verdict drift");
  auto reduced = remove_binding(read_tree(bundle), binding);
  auto cut = work / "control-bundle";
  write_tree(cut, reduced);
  auto removed = plan(cut, spec, work / "removed");
  check(removed["interpretation_errors"].empty() &&
            removed["clarifications"].empty() &&
            removed["providers"].size() == 1,
        "writer removal produced unrelated rejection");
  auto p = removed["providers"][0];
  auto gaps = p["typed_gaps"];
  check(p["status"] == "partial" && gaps.size() == 1 &&
            gaps[0]["from"] == binding["from"] &&
            gaps[0]["to"] == binding["to"] && p["uncovered"].empty(),
        "writer removal did not isolate the sink: " + dump(p));
  result["removed"] = object({{"bundle_sha256", digest(cut / "SHA256SUMS")},
                              {"steps", p["plan"]["steps"]},
                              {"typed_gaps", gaps}});
  auto alt = work / "sort-to-json",
       exe = work / ".test-programs" / "sort-to-json";
  const auto ids = array({"read_lines", "sort_lines", "write_texts"});
  auto json = complete(
      cut, alt, spec_for("text/plain", "application/json", requirement), ids);
  build_emission(json, alt, exe, "g++-15");
  fs::create_directory(alt / "case");
  save(alt / "case/in", "z\na\nz\n");
  auto cmd = sandbox(exe, alt / "case");
  append(cmd, {"in", "out"});
  run(cmd, {.seconds = 8, .probe_limits = true});
  auto out = load(alt / "case/out");
  check(out == array({"a", "z", "z"}), "sibling sort output mismatch");
  result["remaining_sort"] =
      object({{"bindings", ids},
              {"output", out},
              {"program_sha256", digest(exe)},
              {"emitted_sha256", digest(alt / "component.hpp")}});
  return result;
}
} // namespace
Path qualify_writer(const Path &package, const std::string &pin) {
  verify(package, pin);
  const auto base = verified_base(), bundle = package / "bundle";
  auto bindings = sequence_writer_identities(package);
  auto &writer = bindings[1];
  auto raw = load(bundle / "providers/contracts.json")["providers"];
  auto pos = std::find_if(raw.begin(), raw.end(), [&](const J &r) {
    return r["id"] == writer["key"]["binding_id"] &&
           compact_hash(r) == writer["binding_sha256"].asString();
  });
  check(pos != raw.end() && !pos->isMember("effect") &&
            (*pos)["args"] == array(),
        "unexpected writer source binding");
  auto approved = sources()[source_id];
  const auto registry = load(package / "sources.json");
  J src;
  for (const auto &entry : registry["sources"])
    if (entry["id"] == source_id) {
      check(src.isNull(), "duplicate retained source in parent registry");
      src = entry;
    }
  check(approved["approval"] == "local-pilot" &&
            src["approval"] == "local-pilot",
        "writer source lacks a scoped approval");
  for (auto field : {"method", "upstream", "revision", "archive", "sha256",
                     "members", "rights"})
    check(src[field] == approved[field],
          "writer source changed: " + std::string(field));
  check(digest(src["archive"].asString()) == src["sha256"].asString(),
        "writer archive pin mismatch");
  auto work = fresh_run("v5-writer"), stage = work / ".stage",
       scratch = work / ".test-programs";
  fs::create_directory(scratch);
  auto toolchain = file_qualification_tools();
  auto source = retained_source(stage, source_id, "text-measures-LICENSE");
  source.removeMember("usage");
  source["binding"] = *pos;
  source["raw_binding_sha256"] = compact_hash(*pos);
  source["rights"] = src["rights"];
  source["stage_registry_sha256"] = digest(package / "sources.json");
  auto members = source["unchanged_members"];
  for (const auto &file : members.getMemberNames())
    check(digest(bundle / file) == members[file][1].asString(),
          "stage source differs from retained archive");
  write(stage / "source.json", source);
  for (auto [from, to] : std::vector<std::pair<std::string, std::string>>{
           {"write-lines-client.cpp", "client.cpp"},
           {"write-lines.json", "spec.json"},
           {"write-lines-annotation.json", "annotation.json"}})
    fs::copy_file(v5_root() / "fixtures" / from, stage / to);
  auto fixture = load(stage / "spec.json");
  check(fixture["binding"] == writer["key"],
        "frozen contract selects a different writer");
  write(stage / "standalone.json", standalone(stage, work, fixture));
  write(stage / "integration.json",
        integration(base, bundle, *pos, work, fixture));
  // Record the runtime Qt object resolved for the linked direct client.
  std::istringstream linked(run({"ldd", scratch / "writer-client"}));
  std::string soname, arrow, file;
  for (std::string line; std::getline(linked, line);) {
    std::istringstream fields(line);
    if (fields >> soname >> arrow >> file && soname == "libQt6Core.so.6" &&
        arrow == "=>")
      toolchain["linked_qt"] = object(
          {{"path", fs::canonical(file).string()}, {"sha256", digest(file)}});
  }
  check(toolchain.isMember("linked_qt"), "direct client did not link Qt6Core");
  auto linked_qt = toolchain["linked_qt"];
  auto final_tools = file_qualification_tools();
  final_tools["linked_qt"] =
      object({{"path", linked_qt["path"]},
              {"sha256", digest(linked_qt["path"].asString())}});
  check(toolchain == final_tools,
        "writer toolchain changed during qualification");
  write(stage / "environment.json", toolchain);
  auto contract =
      file_contract(stage, package, writer, src, package / "sources.json");
  return seal_file_qualification(work, package, pin, bindings, 1, contract);
}
} // namespace v5
