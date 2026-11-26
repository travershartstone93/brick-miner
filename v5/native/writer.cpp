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
  return object({{"binding", fixture["binding"]["binding_id"]},
                 {"cases", observed},
                 {"rejected_mutants", controls},
                 {"client_sha256", digest(client)},
                 {"program_sha256", digest(exe)}});
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
    auto p = complete_file_plan(label == "baseline" ? base : bundle, dir, spec,
                                chain);
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
  auto json = complete_file_plan(
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

J linked_writer_tools(const Path &exe, J toolchain) {
  // Record the runtime Qt object resolved for the linked direct client.
  std::istringstream linked(run({"ldd", exe}));
  std::string soname, arrow, file;
  for (std::string line; std::getline(linked, line);) {
    std::istringstream fields(line);
    if (fields >> soname >> arrow >> file && soname == "libQt6Core.so.6" &&
        arrow == "=>")
      toolchain["linked_qt"] = object(
          {{"path", fs::canonical(file).string()}, {"sha256", digest(file)}});
  }
  check(toolchain.isMember("linked_qt"), "direct client did not link Qt6Core");
  return toolchain;
}

J tsv_integration(const Path &work, const Path &bundle, const Path &cut,
                  const J &binding, const J &specification) {
  auto requirement = object({{"outcome", "sort_lines"},
                             {"parameters", object()},
                             {"uncovered", array()}});
  auto spec = spec_for("text/plain", "text/tab-separated-values", requirement);
  auto chain = array({"read_lines", "sort_lines", "write_tsv_lines"});
  auto report = object({{"spec", spec}, {"chain", chain}});
  auto preferred = work / "fixed-tsv", exe = work / ".test-programs/emit-tsv";
  auto program = complete_file_plan(bundle, preferred, spec, chain);
  build_emission(program, preferred, exe, "g++-15");
  report["staged"] =
      object({{"program_sha256", digest(exe)},
              {"emitted_sha256", digest(preferred / "component.hpp")},
              {"cases", suite(exe, preferred, specification["ordinary"])}});

  const auto removed = plan(cut, spec, work / "removed");
  const auto &p = removed["providers"][0];
  auto holes = p["typed_gaps"];
  check(removed["interpretation_errors"].empty() &&
            removed["clarifications"].empty() &&
            removed["providers"].size() == 1 && p["status"] == "partial" &&
            p["uncovered"].empty() && holes.size() == 1 &&
            holes[0]["from"] == binding["from"] &&
            holes[0]["to"] == binding["to"],
        "exact TSV removal did not isolate the sink: " + dump(removed));
  report["removed"] = object({{"bundle_sha256", digest(cut / "SHA256SUMS")},
                              {"steps", p["plan"]["steps"]},
                              {"typed_gaps", holes}});

  auto plain = work / "remaining-plain",
       sibling = work / ".test-programs/emit-plain";
  auto remaining = array({"read_lines", "sort_lines", "write_lines"});
  auto alternate = complete_file_plan(
      cut, plain, spec_for("text/plain", "text/plain", requirement), remaining);
  build_emission(alternate, plain, sibling, "g++-15");
  auto observations = suite(sibling, plain, specification["ordinary"]);
  check(observations == report["staged"]["cases"],
        "plain sibling and TSV sink differ in the fixed byte cases");
  report["remaining_plain"] = object({{"chain", remaining},
                                      {"program_sha256", digest(sibling)},
                                      {"cases", observations}});
  return report;
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
  toolchain = linked_writer_tools(scratch / "writer-client", toolchain);
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

Path qualify_tsv_writer(const Path &package, const std::string &pin,
                        const Path &registry, const std::string &registry_pin) {
  verify(package, pin);
  const std::string id = "write_tsv_lines";
  const auto src = sources()[source_id];
  const auto job = retained_binding_job(v5_root() / "sources.json", id);
  check(src["approval"] == "local-pilot" &&
            digest(src["archive"].asString()) == src["sha256"].asString() &&
            job["source"] == source_id && job["route"] == "direct" &&
            job["stream"] == "foundational" &&
            job["tests"] == "v5/fixtures/write-tsv-lines.json" &&
            !job["approval_reference"].asString().empty(),
        "unapproved retained TSV writer");
  auto work = fresh_run("v5-tsv-writer"), stage = work / ".stage",
       scratch = work / ".test-programs";
  fs::create_directory(scratch);
  save_file_registry(stage, src, registry, registry_pin);
  check(retained_binding_job(stage / "sources.json", id) == job,
        "changed TSV writer job");
  auto historic = runs / "ranked-gaps-5x2_hyp7/run-1/bundle";
  check(digest(historic / "SHA256SUMS") ==
            job["historical_bundle_sha256"].asString(),
        "changed historical TSV bundle pin");
  run({"sha256sum", "--strict", "--quiet", "-c", "SHA256SUMS"},
      {.cwd = historic});
  const auto historical = load(historic / "providers/contracts.json");
  auto find = [](const J &providers, const std::string &binding) {
    J match;
    for (const auto &row : providers)
      if (row["id"] == binding) {
        check(match.isNull(), "ambiguous writer row " + binding);
        match = row;
      }
    check(match.isObject(), "missing writer row " + binding);
    return match;
  };
  const auto row = find(historical["providers"], id);
  auto draft = read_tree(package / "bundle");
  auto defs = parse(draft.at("providers/contracts.json"));
  auto plain = find(defs["providers"], "write_lines"), expected = plain;
  expected["id"] = id;
  expected["to"] = "out:text/tab-separated-values";
  check(row == expected && plain["brick"] == "x09" &&
            plain["from"] == "strings" && plain["to"] == "out:text/plain" &&
            !row.isMember("effect") && row["args"] == array() &&
            row["call"] == "bricks::save_lines",
        "historical TSV registration differs from the verified sibling");
  for (const auto &r : defs["providers"])
    check(r["id"] != id && !(r["from"] == row["from"] && r["to"] == row["to"]),
          "TSV route is already present on the parent");
  const auto parent_manifest = draft.at("SHA256SUMS");
  defs["providers"].append(row);
  draft["providers/contracts.json"] = dump(defs);
  const auto candidate_pin = seal(draft);
  auto reduced = remove_binding(draft, row);
  check(reduced.at("SHA256SUMS") == parent_manifest,
        "removing the TSV row did not restore the parent");
  const auto bundle = stage / "bundle", cut = work / "control-bundle";
  write_tree(bundle, draft);
  write_tree(cut, reduced);
  draft.clear();
  reduced.clear();
  auto record = file_binding_identity(row, bundle);
  const auto source =
      retained_source(stage, source_id, "text-measures-LICENSE");
  const auto members = source["unchanged_members"];
  for (const auto &file : members.getMemberNames())
    check(digest(bundle / file) == members[file][1].asString() &&
              digest(historic / file) == members[file][1].asString(),
          "TSV writer source differs from the approved retained archive");
  auto origin = source;
  origin.removeMember("usage");
  origin["binding"] = row;
  origin["plain_sibling"] = plain;
  origin["historical_bundle_sha256"] = job["historical_bundle_sha256"];
  origin["candidate_bundle_sha256"] = candidate_pin;
  origin["parent_bundle_sha256"] = digest(package / "bundle/SHA256SUMS");
  write(stage / "source.json", origin);

  for (const auto &[from, to] :
       std::vector<std::pair<std::string, std::string>>{
           {"write-lines-client.cpp", "client.cpp"},
           {"write-tsv-lines.json", "spec.json"},
           {"write-tsv-lines-annotation.json", "annotation.json"}})
    fs::copy_file(v5_root() / "fixtures" / from, stage / to);
  auto specification = load(stage / "spec.json");
  check(specification["binding"] == record["key"],
        "unexpected TSV binding identity");
  const auto tools = file_qualification_tools();
  const auto replay_executable =
      fs::canonical("/proc/self/exe").parent_path() / "replay-gaps";
  auto replay_tools = [&] {
    return object(
        {{"ranked_replay", digest(replay_executable)},
         {"row_replay", digest(main_repo / "build/native/brick-row-replay")},
         {"checker", digest(main_repo / "build/native/brick-end-user-check")}});
  };
  const auto replay_pins = replay_tools();
  write(stage / "standalone.json", standalone(stage, work, specification));
  write(stage / "integration.json",
        tsv_integration(work, bundle, cut, row, specification));
  auto replay = work / "replay";
  run({replay_executable, "--compare-existing", replay, cut, bundle},
      {.seconds = 480});
  const auto verified = load(replay / "report.json");
  auto prior_path = runs / "ranked-gaps-5x2_hyp7/report.json";
  check(digest(prior_path) ==
            "28c412fe208b20e6bab66365217ee9726c1e13e1ad0345d234a7607152168171",
        "changed historical replay scorecard");
  const auto prior = load(prior_path);
  check(verified["base_sha256"] == origin["parent_bundle_sha256"] &&
            verified["staged_sha256"] == candidate_pin &&
            verified["requests"] == prior["requests"] &&
            verified["control"] == prior["control"],
        "TSV replay differs from the historical controls");
  fs::copy_file(replay / "report.json", stage / "replay.json");
  check(digest(replay / "correct-operations.json") ==
            "f1b7f62163e4ebc56a4a20b0a2bb7fc19baefd9885fcec6e69301bf6c7c14d83",
        "changed H029 corrected interpretation");
  fs::copy_file(replay / "correct-operations.json",
                stage / "replay-control.json");
  auto environment = linked_writer_tools(scratch / "writer-client", tools);
  check(linked_writer_tools(scratch / "writer-client",
                            file_qualification_tools()) == environment &&
            replay_tools() == replay_pins,
        "writer qualification tools changed");
  environment["replay_tools"] = replay_pins;
  write(stage / "environment.json", environment);
  auto contract =
      file_contract(stage, package, record, src, stage / "sources.json");
  contract["qualification"]["scope"] =
      "Detached evidence for preformatted UTF-8 TSV records without embedded "
      "CR, LF or NUL, on the pinned Qt and filesystem environment.";
  validate(contract, load(package / "contract.schema.json"));
  write(stage / "contract.json", contract);
  write(stage / "domain.json",
        object({{"schema", "unllm.scoped-file-evidence-domain/1"},
                {"binding_key", record["key"]},
                {"raw_binding_sha256", record["binding_sha256"]},
                {"logical_id", record["logical_id"]},
                {"implementation_sha256", record["header_sha256"]},
                {"contract_sha256", digest(stage / "contract.json")},
                {"environment_sha256", digest(stage / "environment.json")},
                {"call", row["call"]},
                {"argument_index", 0},
                {"argument_type", "const std::vector<std::string>&"},
                {"quantifier", "each_record"},
                {"all_of",
                 array({object({{"id", "valid_utf8"}, {"value", true}}),
                        object({{"id", "no_embedded_cr_lf"}, {"value", true}}),
                        object({{"id", "no_nul"}, {"value", true}})})},
                {"scope", "The caller supplies already-formatted records. "
                          "The writer copies their bytes and appends LF."}}));
  return finish_file_stage(work, package, pin, record["logical_id"].asString(),
                           "unllm.scoped-file-evidence/1");
}
} // namespace v5
