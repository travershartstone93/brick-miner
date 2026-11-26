#include "qualification.hpp"
#include "../shared/qualification.hpp"
#include "duckdb.hpp"
#include "pilots.hpp"
#include <algorithm>
#include <iostream>
#include <regex>
#include <sstream>

namespace rows {
using namespace qualification;
Path here() { return root / "row-ops"; }
const std::string prefix = "brick_miner::records::", pkg = "brick-row-kernels";
const Strings cpp = {"g++-15", "-std=c++17", "-O1",    "-fPIC",
                     "-Wall",  "-Wextra",    "-Werror"};
J audit() {
  auto raw = bytes(main_repo /
                   "experiments/boilerplate/stress/end-user-100/REQUESTS.md");
  check(sha(raw) ==
            "c1cbea642d46a3411ec6a49cf2c17e23d31f4a365242fa6287dd5c48734f06a5",
        "suite changed");
  std::regex expression("^## (H[0-9]+) · (.+)$", std::regex::multiline);
  std::istringstream lines(bytes(here() / "request-map.tsv"));
  std::string row;
  std::getline(lines, row);
  J result = object();
  std::map<std::string, std::set<std::string>> domains;
  unsigned count = 0;
  for (std::sregex_iterator it(raw.begin(), raw.end(), expression), end;
       it != end; ++it) {
    check(bool(std::getline(lines, row)), "incomplete request map");
    std::istringstream cols(row);
    std::string id, roles;
    std::getline(cols, id, '\t');
    std::getline(cols, roles, '\t');
    check(id == (*it)[1].str(), "unexpected request order");
    ++count;
    for (const auto &[code, name] :
         std::map<char, std::string>{{'G', "group_rows"},
                                     {'A', "aggregate_groups"},
                                     {'P', "select_group_row"}})
      if (roles.find(code) != roles.npos) {
        result[name]["requests"].append(id);
        domains[name].insert((*it)[2].str());
      }
  }
  check(count == 100 && !std::getline(lines, row),
        "incomplete or extra request rows");
  for (auto &[name, d] : domains)
    result[name]["workflows"] = string_array({d.begin(), d.end()});
  return result;
}
std::pair<J, Files> acquire(bool mined, const Path &work) {
  J meta = object();
  Files evidence;
  if (mined) {
    auto report = load(work / "native/provenance.json");
    for (auto &[id, names] : std::map<std::string, Strings>{
             {"group_rows", {"brick_list"}},
             {"aggregate_groups", {"brick_count", "brick_sum"}},
             {"select_group_row", {"brick_first", "brick_last"}}})
      meta[id] =
          object({{"route", "compiled modules from signed Arch debug source"},
                  {"package", "extra/duckdb"},
                  {"release", "1.5.6-1"},
                  {"native_provenance",
                   "qualifications/row-operations/native/provenance.json"},
                  {"native_provenance_sha256", sha(dump(report))},
                  {"pkg_config", pkg},
                  {"runtime", "runtime/duckdb"},
                  {"binding", "project-authored row marshalling and "
                              "parameterized relational queries"},
                  {"aggregate_names", string_array(names)}});
    return {meta, evidence};
  }
  auto source = load(here() / "sources.json");
  Path arc = Path("/var/cache/pacman/pkg") / source["archive"].asString(),
       bits = source["include_dir"].asString(), ring = work / "archlinux.gpg";
  check(duckdb_miner::hash_file(arc) == source["sha256"].asString(),
        "GCC archive changed");
  pilots::keyring(ring);
  auto signer = pilots::signature(arc, ring);
  check(run({"bsdtar", "-xOf", arc, ".BUILDINFO"})
                .find("pkgbuild_sha256sum = " +
                      source["pkgbuild_sha256"].asString() + "\n") !=
            std::string::npos,
        "GCC recipe pin mismatch");
  std::map<Path, std::string> verified;
  for (const auto &name : source["operations"].getMemberNames()) {
    auto &info = meta[name];
    for (const auto *k : {"package", "release", "upstream_revision", "archive",
                          "sha256", "pkgbuild_sha256"})
      info[k] = source[k];
    info["signing_key"] = signer;
    info["binding"] = "project-authored C++17 adapter";
    info["runtime"] = "libstdc++";
    info["license"] = "GPL-3.0-or-later WITH GCC-exception-3.1";
    info["build_flags"] = array({"-std=c++17"});
    for (const auto &h : source["operations"][name].getMemberNames()) {
      auto file = bits / h;
      if (!verified.contains(file)) {
        verified[file] = run({"bsdtar", "-xOf", arc, file.relative_path()});
        check(bytes(file) == verified[file],
              "installed GCC header differs from archive");
      }
      const auto &raw = verified.at(file);
      auto syms = source["operations"][name][h];
      for (const auto &sym : syms)
        check(std::regex_search(raw,
                                std::regex("\\b" + sym.asString() + "\\s*\\(")),
              "missing GCC operation");
      info["files"][file.string()] =
          object({{"sha256", sha(raw)}, {"symbols", syms}});
      evidence["notices/" + h + ".txt"] = raw.substr(0, raw.find("#ifndef"));
    }
  }
  evidence["COPYING3"] = bytes("/usr/share/licenses/spdx/GPL-3.0-or-later.txt");
  evidence["COPYING.RUNTIME"] =
      bytes("/usr/share/licenses/gcc/RUNTIME.LIBRARY.EXCEPTION");
  return {meta, evidence};
}
void compile(const Strings &args, const Strings &packages = {}) {
  Strings cmd = cpp;
  append(cmd, args);
  append(cmd, flags(packages));
  run(cmd);
}
void differential(const Path &work, const Path &base) {
  auto started = seconds();
  auto include = here() / "include", client = here() / "tests/client.cpp",
       source = here() / "mined/tests/differential.cpp";
  compile({"-I", include, "-I", base / "providers", "-I", base / "headers",
           client, "-o", work / "authored-client"});
  run({work / "authored-client"});
  auto elapsed = seconds() - started;
  std::string original;
  for (auto kind : {"original", "mined"}) {
    bool mined = std::string(kind) == "mined";
    auto exe = work / (std::string(kind) + "-differential");
    compile({"-I", mined ? here() / "mined/include" : include, "-I",
             base / "providers", source, "-o", exe},
            mined ? Strings{pkg} : Strings{});
    auto output = run({exe});
    if (!mined) {
      original = output;
      continue;
    }
    check(original == output &&
              sha(output) == "82fb12db32121bf9552a967d074c36f9992a4567b23c1c682"
                             "0118b8d9fa864af",
          "frozen row oracle differs");
    write(work / "comparison.json",
          object({{"implementation", "original hand-written row operations"},
                  {"differential",
                   object({{"bytes", Json::UInt64(output.size())},
                           {"sha256", sha(output)},
                           {"source_sha256", sha(bytes(source))}})},
                  {"client_sha256", sha(bytes(client))},
                  {"passed", true}}));
  }
  write(work / "comparison-timing.json", object({{"seconds", elapsed}}));
}
J check_program(const Path &bundle, const Path &dir, const J &spec, const J &c,
                const J &data, const J &contracts) {
  auto result = plan(bundle, spec, dir);
  check(result["providers"].size() == 1 && result["gaps"].empty(),
        "incomplete row emission");
  auto p = result["providers"][0];
  check(p["status"] == "complete" && p["uncovered"].empty(),
        "incomplete provider");
  for (const auto &op : spec["roles"][0]["requirement"]["operations"]) {
    auto id = op["outcome"].asString();
    if (contracts.isMember(id))
      check(std::find(p["bricks"].begin(), p["bricks"].end(), "miner_" + id) !=
                    p["bricks"].end() &&
                p["source"].asString().find(prefix + id + "(") !=
                    std::string::npos,
            "selected provider absent");
  }
  auto exe = dir / "program";
  build_emission(p, dir, exe, "g++-15");
  auto labels = array({"ordinary", "empty", "missing"});
  for (const auto &l : labels) {
    auto label = l.asString();
    auto input = dir / label, out = dir / (label + ".out");
    if (label != "missing")
      save(input,
           label == "ordinary" ? data[c["data"].asString()].asString() : "");
    auto res = execute({exe, input, out}, {.seconds = 10});
    if (label == "missing") {
      check(res.status == 1 && !fs::exists(out),
            "unexpected missing-file result");
      continue;
    }
    check(!res.status, "emitted program failed");
    auto actual = array();
    if (c["format"] == "text/plain") {
      std::istringstream lines(bytes(out));
      std::string line;
      while (std::getline(lines, line))
        actual.append(line);
    } else
      actual = load(out);
    check(actual == (label == "ordinary" ? c["expected"] : array()),
          "emitted row output differs");
  }
  return object(
      {{"bricks", p["bricks"]}, {"source", p["source"]}, {"checks", labels}});
}
void qualify(bool mined) {
  const auto work = fs::current_path(), base = pilots::historical_base(),
             file = here() / "contracts.json";
  auto started = seconds();
  if (mined)
    differential(work, base);
  auto coverage = audit(), contracts = load(file),
       cases = load(here() / "tests/cases.json"), tasks = array();
  auto [meta, notices] = acquire(mined, work);
  auto configs = pilots::original_keys(file);
  Strings packages = mined ? Strings{pkg} : Strings{};
  Files headers;
  auto args = object();
  for (const auto &key : configs) {
    auto name = "miner_" + key + ".hpp";
    headers[key] = bytes(here() / (mined ? "mined/include" : "include") / name);
    save(work / "client-headers" / name, headers[key]);
    auto task = contracts[key];
    task["id"] = key;
    task["arg_order"] = args[key] = argument_order(file, key);
    task["origin"] = mined ? "mined_application" : "authored";
    task["call"] = prefix + key;
    task["packages"] = string_array(packages);
    tasks.append(task);
  }
  auto conversion = object({{"id", "flatten_row_groups"},
                            {"brick", "miner_group_rows"},
                            {"from", "row_groups"},
                            {"to", "rows"},
                            {"call", prefix + "flatten_groups"},
                            {"header", "miner_group_rows.hpp"}});
  auto stage = register_tasks(read_tree(base), tasks, headers, meta, work,
                              object({{"row_groups", prefix + "Groups"}}),
                              array({conversion}));
  auto acquired = seconds() - started;
  auto overlay = work / "plan/staged";
  plan_inputs(overlay, stage);
  for (const auto &key : configs)
    plan_inputs(work / "plan" / key, ablate(stage, key));
  Strings includes = {"-I", work / "client-headers", "-I", base / "providers",
                      "-I", base / "headers"};
  auto cmd = includes;
  append(cmd, {"-MD", "-MF", work / "used.d", here() / "tests/client.cpp", "-o",
               work / "client"});
  compile(cmd, packages);
  run({work / "client"});
  auto deps = bytes(work / "used.d");
  if (mined) {
    auto needed = run({"readelf", "-d", work / "client"});
    for (const auto *lib : {"libduckdb.so", "libbrick_row_kernels.so"})
      check(needed.find("[" + std::string(lib) + "]") != needed.npos,
            "unlinked kernel");
    check(deps.find("duckdb.h") != deps.npos, "unused DuckDB header");
    save(work / "unregistered.cpp",
         "extern \"C\" void brick_register_row_kernels(void*) {}\n");
    run({"c++", "-fPIC", "-shared", work / "unregistered.cpp", "-o",
         work / "unregistered.so"});
    auto negative =
        execute({work / "client"},
                {.seconds = 10,
                 .environment = {{"LD_PRELOAD", work / "unregistered.so"}}});
    check(negative.status &&
              negative.err.find("brick_list") != negative.err.npos,
          "registration control insensitive");
  } else
    for (const auto &entry : meta)
      for (const auto &name : entry["files"].getMemberNames())
        check(deps.find(name) != deps.npos ||
                  deps.find(fs::canonical(name).string()) != deps.npos,
              "unused GCC header " + name);
  auto receipts = object(), hard = object();
  auto spec_for_ops = [&](const J &c, const J &ops) {
    return spec_for("text/tab-separated-values",
                    c.get("format", "application/json").asString(),
                    object({{"operations", shared::operations(ops)},
                            {"uncovered", array()}}));
  };
  for (const auto &c : cases["programs"]) {
    auto name = c["name"].asString();
    auto dir = work / "programs" / name;
    auto ops = array();
    for (const auto &pair : std::vector<std::pair<std::string, std::string>>{
             {"group", "group_rows"},
             {"aggregate", "aggregate_groups"},
             {"select", "select_group_row"}})
      if (c.isMember(pair.first))
        ops.append(
            array({pair.second, parameters(args[pair.second], c[pair.first])}));
    ops.append(array({"ordered_column", object({{"column", c["column"]}})}));
    auto spec = spec_for_ops(c, ops);
    auto before = plan(base, spec, dir / "baseline");
    reject(before);
    auto passed = check_program(overlay, dir / "staged", spec, c, cases["data"],
                                contracts),
         removed = object();
    for (const auto &op : ops) {
      auto key = op[0].asString();
      if (!contracts.isMember(key))
        continue;
      auto negative = plan(work / "plan" / key, spec, dir / ("without-" + key));
      reject(negative);
      auto status = array();
      for (const auto &p : negative["providers"])
        status.append(p["status"]);
      removed[key] =
          object({{"gaps", negative["gaps"]}, {"provider_status", status}});
    }
    receipts[name] = object({{"spec", spec},
                             {"spec_sha256", sha(dump(spec))},
                             {"baseline", before["gaps"]},
                             {"staged", passed},
                             {"removed", removed}});
    if (name == "H010") {
      auto legacy = spec_for_ops(
          c, array({array({"ordered_column", object({{"column", 1}})}),
                    array({"reverse_order", object()}),
                    array({"unique_lines", object()})}));
      check_program(base, work / "H010-legacy", legacy, c, cases["data"],
                    contracts);
      hard[name] =
          shared::assess(name, dir / "staged/program", work / "original-H010");
      hard["H010_legacy"] = shared::assess(name, work / "H010-legacy/program",
                                           work / "legacy-check");
    }
  }
  for (std::string id : {"H032", "H084"}) {
    cmd = includes;
    if (id == "H032")
      cmd.push_back("-DSENSOR_PEAK");
    append(cmd, {here() / "tests/requests.cpp", "-o", work / id});
    auto pkgs = packages;
    pkgs.push_back("json-c");
    compile(cmd, pkgs);
    hard[id] = shared::assess(id, work / id, work / ("original-" + id));
  }
  for (const auto &checks : hard)
    for (const auto &r : checks)
      check(r["pass_"].asBool(), "independent row benchmark failed");
  const std::string evidence = "qualifications/row-operations/";
  for (const auto &[name, raw] : notices)
    stage[evidence + name] = raw;
  for (const auto *name : {"contracts.json", "tests/cases.json",
                           "tests/client.cpp", "tests/requests.cpp"})
    stage[evidence + name] = bytes(here() / name);
  for (const auto &key : configs) {
    auto loc = "qualifications/miner_" + key + "/";
    stage[loc + "provenance.json"] = dump(meta[key]);
    auto selected = object();
    for (const auto &name : receipts.getMemberNames())
      if (receipts[name]["removed"].isMember(key))
        selected[name] = receipts[name];
    stage[loc + "receipt.json"] =
        dump(object({{"header_sha256", sha(headers[key])},
                     {"independent_client", "passed"},
                     {"programs", selected}}));
  }
  if (mined) {
    for (auto &[f, data] : read_tree(work / "native/original"))
      stage[evidence + "native/original/" + f] = std::move(data);
    for (auto &[f, data] : read_tree(work / "runtime"))
      stage["runtime/duckdb/" + f] = std::move(data);
    for (const auto *f :
         {"provenance.json", "register.cpp", "native_client.cpp"})
      stage[evidence + "native/" + f] = bytes(work / "native" / f);
    for (const auto *f :
         {"arch.json", "contracts.json", "register.cpp.in", "extract.cpp"})
      stage[evidence + "native/recipe/" + f] =
          bytes(here() / "mined/source" / f);
    for (const auto *f : {"duckdb.cpp", "duckdb.hpp"})
      stage[evidence + "native/recipe/" + f] =
          bytes(root / "src/experiments" / f);
    stage[evidence + "request-map.tsv"] = bytes(here() / "request-map.tsv");
    stage[evidence + "comparison.json"] = bytes(work / "comparison.json");
    stage[evidence + "tests/differential.cpp"] =
        bytes(here() / "mined/tests/differential.cpp");
  }
  publish(stage, work);
  auto pins = load(root / "contracts/inputs.json");
  write(work / "report.json",
        object({{"base", base.string()},
                {"base_verification_sha256", pins["verification.json"]},
                {"base_tree_sha256", pins["base_tree_sha256"]},
                {"manifest_sha256", sha(stage["SHA256SUMS"])},
                {"base_cards", load(base / "catalog.json").size()},
                {"staged_cards", parse(stage["catalog.json"]).size()},
                {"sources", meta},
                {"coverage", coverage},
                {"programs", receipts},
                {"end_user_evaluator", hard},
                {"client", "passed on stock and sensor data"},
                {"seconds", object({{"acquisition", acquired},
                                    {"total", seconds() - started}})}}));
}
} // namespace rows
