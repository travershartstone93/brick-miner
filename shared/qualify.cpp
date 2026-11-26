#include "../src/experiments/qualification.hpp"
#include "build.hpp"
#include "duckdb.hpp"
#include "pilots.hpp"
#include <algorithm>
#include <iostream>

namespace shared {
namespace {
using qualification::parameters;
Path work() { return fs::current_path(); }
J typed(const std::string &mime, const J &ops,
        const std::string &out = "application/json") {
  return spec_for(
      mime, out,
      object({{"operations", operations(ops)}, {"uncovered", array()}}));
}
Path program(const Path &bundle, const Path &dir, const J &spec,
             const Strings &required) {
  auto emitted = plan(bundle, spec, dir);
  if (required.empty()) {
    qualification::reject(emitted);
    return {};
  }
  check(emitted["gaps"].empty() && emitted["providers"].size() == 1,
        "incomplete emitted role in " + dir.string());
  const auto &p = emitted["providers"][0];
  check(p["status"] == "complete", "partial program");
  for (const auto &key : required) {
    auto ids = strings(p["bricks"]);
    check(std::find(ids.begin(), ids.end(), "miner_" + key) != ids.end(),
          "provider missing from plan " + key);
  }
  build_emission(p, dir, dir / "program", "g++-15");
  return dir / "program";
}
std::string declaration(const std::string &name, const J &c, const J &order) {
  auto from = c["from"].asString();
  bool reader = from.starts_with("in:");
  auto out = c["to"].asString();
  check(out == "rows" || out == "strings", "unknown result type");
  std::map<std::string, std::string> types = {{"text", "const std::string&"},
                                              {"column", "std::size_t"},
                                              {"integer", "std::int64_t"}};
  Strings args = {reader ? "const char*" : "const brick_data::Rows&"};
  for (const auto &key : order)
    args.push_back(types.at(c["args"][key.asString()].asString()));
  return "#pragma once\n#include \"provider_types.hpp\"\nnamespace "
         "brick_miner::shared {\n" +
         std::string(out == "rows" ? "brick_data::Rows"
                                   : "std::vector<std::string>") +
         " " + name + "(" + join(args, ", ") + ");\n}\n";
}
J executed(const Build &b, const Path &folder, const J &contract,
           const Path &lib) {
  auto merged = folder / "native.profdata";
  run({"llvm-profdata", "merge", "-sparse", folder / "native.profraw", "-o",
       merged});
  Strings cmd = {"llvm-cov", "export", lib,
                 "-instr-profile=" + merged.string()};
  std::set<Path> files;
  for (const auto &s : contract["symbols"])
    files.insert(b.symbols.at(s.asString()));
  for (const auto &f : files)
    cmd.push_back(f);
  auto exported = parse(run(cmd))["data"][0]["functions"];
  auto hits = object();
  for (const auto &s : contract["symbols"]) {
    auto name = s.asString(),
         target = contract["execution"].get(name, name).asString(),
         source = b.symbols.at(name).string();
    auto pos = target.rfind("::");
    std::set<std::string> found;
    for (const auto &fn : exported) {
      auto f = fn["name"].asString();
      if (!fn["count"].asUInt64() ||
          f.find(pos == target.npos ? target : target.substr(pos + 2)) ==
              f.npos)
        continue;
      if (pos != target.npos &&
          f.find(target.substr(0, target.find("::"))) == f.npos)
        continue;
      for (const auto &path : fn["filenames"])
        if (path.asString().ends_with(source))
          found.insert(f);
    }
    check(!found.empty(),
          "coverage missing " + name + " in " + folder.string());
    hits[name] = string_array({found.begin(), found.end()});
  }
  return hits;
}
J check_case(const Path &base, const Path &stage, const Path &batch, const J &c,
             const J &contract, const J &order, const Build &b) {
  auto name = c["brick"].asString();
  auto dir = batch / "checks" / c["id"].asString();
  fs::create_directories(dir);
  consumer(c, contract, dir);
  auto exe = dir / "consumer";
  Strings cmd = {"clang++",
                 "-std=c++17",
                 "-O0",
                 "-I",
                 batch / "headers",
                 "-I",
                 first_batch() / "providers",
                 "-I",
                 first_batch() / "headers",
                 "-I",
                 dir,
                 here() / "tests/consumer.cpp",
                 "-o",
                 exe};
  append(cmd, flags({b.name}));
  run(cmd);
  auto input = dir / "input", invalid = dir / "invalid";
  materialize(input, c["input"]);
  materialize(invalid, c["invalid"]);
  run({exe, input, "pass"});
  run({exe, input, "pass"},
      {.environment = {
           {"LD_LIBRARY_PATH", (batch / "coverage").string() + ":" +
                                   (work() / "runtime/lib").string()},
           {"LLVM_PROFILE_FILE", (dir / "native.profraw").string()}}});
  run({exe, invalid, "fail"});
  run({exe, dir / "nonexistent", "fail"});
  auto ops = array({array({name, parameters(order, c["args"])})});
  bool rows = contract["to"] == "rows";
  if (rows)
    ops.append(array({"ordered_column", object({{"column", c["column"]}})}));
  auto from = contract["from"].asString();
  auto task = typed(from.starts_with("in:") ? from.substr(3)
                                            : "text/tab-separated-values",
                    ops);
  program(base, dir / "before", task, {});
  auto generated = program(stage, dir / "after", task, {name});
  program(batch / "plan" / name, dir / "removed", task, {});
  auto checks = array({"ordinary", "empty", "invalid", "missing"});
  for (const auto &l : checks) {
    auto label = l.asString();
    auto p = dir / (label + ".input"), out = dir / (label + ".output");
    if (label != "missing")
      materialize(p, c[label == "ordinary" ? "input" : label]);
    auto res = execute({generated, p, out}, {.seconds = 10});
    if (label == "missing" || label == "invalid") {
      check(res.status && !fs::exists(out), "invalid input accepted");
      continue;
    }
    auto wanted = array();
    if (label == "ordinary") {
      if (rows) {
        for (const auto &row : c["rows"])
          wanted.append(row[c["column"].asUInt()]);
      } else
        wanted = c["strings"];
    }
    check(res.status == 0 && load(out) == wanted,
          "incorrect " + label + " output for " + c["id"].asString());
  }
  auto lib = batch / "coverage" / ("lib" + b.name + ".so");
  return object({{"input_family", c["id"]},
                 {"native", "all fields and positions matched"},
                 {"planner", "passed"},
                 {"baseline", "no complete plan"},
                 {"provider_removed", "no complete plan"},
                 {"checks", checks},
                 {"executed", executed(b, dir, contract, lib)}});
}
J request_checks(const Path &base, const Path &stage, const std::string &batch,
                 const J &contracts) {
  auto tasks = suite(), recipes = load(here() / "request-recipes.json"),
       ids = load(here() / "request-batches.json")[batch], receipts = object();
  const Strings mimes = {"text/tab-separated-values", "text/csv",
                         "application/vnd.sqlite3",   "application/json",
                         "application/xml",           "application/zip",
                         "inode/directory",           "text/uri-list",
                         "text/tab-separated-values", "application/json"};
  for (const auto &entry : ids) {
    auto id = entry.asString();
    auto &task = tasks[id];
    const auto &arms = recipes[id];
    check(arms.size() == 1, "unexpected role count in stage");
    auto &ops = arms[0];
    auto spec = typed(mimes.at(task["domain"].asUInt()), ops,
                      task["formats"] == array({"text"}) ? "text/plain"
                                                         : "application/json");
    Strings required;
    for (const auto &op : ops)
      if (contracts.isMember(op[0].asString()))
        required.push_back(op[0].asString());
    check(!required.empty(), "unrelated batch request");
    auto dir = work() / batch / "requests" / id;
    program(base, dir / "before", spec, {});
    auto exe = program(stage, dir / "after", spec, required);
    auto checks = assess(id, exe, dir / "evaluation");
    for (const auto &c : checks)
      check(c["pass_"].asBool(), "failed " + id);
    program(work() / batch / "plan" / required.back(), dir / "removed", spec,
            {});
    receipts[id] = object({{"checks", checks},
                           {"operations", ops},
                           {"baseline", "incomplete"},
                           {"removed", required.back()}});
  }
  return receipts;
}
} // namespace
void qualify_batch(const Path &base, const std::string &batch) {
  using qualification::seconds;
  auto start = seconds();
  auto dir = work() / batch, file = here() / batch / "contracts.json";
  auto contracts = load(file), cases = load(here() / batch / "cases.json");
  auto names = pilots::original_keys(file);
  for (const auto &name : names)
    check(std::count_if(cases.begin(), cases.end(),
                        [&](const auto &c) { return c["brick"] == name; }) >= 2,
          "missing independent examples");
  auto b = batch == "inputs" ? build_inputs(contracts)
                             : build_duckdb(batch, contracts);
  auto acquired = seconds();
  auto runtime = work() / "runtime/lib";
  auto lib = "lib" + b.name + ".so", pc = b.name + ".pc";
  duckdb_miner::copy(dir / "release" / lib, runtime / lib);
  save(runtime / "pkgconfig" / pc,
       "prefix=${pcfiledir}/../..\nlibdir=${prefix}/lib\nName: " + b.name +
           "\nDescription: Extracted " + batch +
           " modules\nVersion: 1.5.6\nLibs: -L${libdir} -Wl,-rpath,${libdir} "
           "-l" +
           b.name + " -lduckdb\n");
  auto origin = "qualifications/shared/" + batch + "/";
  auto tasks = array(), meta = object(), arg_order = object();
  Files headers;
  const std::map<std::string, Strings> sources = {
      {"sqlite_records",
       {"core/sqlite 3.53.3-1", "sqlite-src-3530300", "sqlite3_step"}},
      {"xml_records",
       {"extra/pugixml 1.16-1", "v1.16", "xpath_query::evaluate_node_set"}},
      {"zip_file_records",
       {"core/libarchive 3.8.9-1", "v3.8.9", "archive_read_next_header"}},
      {"tree_file_records",
       {"core/libarchive 3.8.9-1", "v3.8.9", "archive_read_next_header"}},
      {"uri_records",
       {"extra/uriparser 1.0.2-1", "1.0.2", "uriParseSingleUriA"}}};
  for (const auto &name : names) {
    auto task = contracts[name];
    task["id"] = name;
    task["arg_order"] = arg_order[name] =
        qualification::argument_order(file, name);
    headers[name] = declaration(name, task, arg_order[name]);
    save(dir / "headers" / ("miner_" + name + ".hpp"), headers[name]);
    auto units = object();
    for (const auto &sym : task["symbols"]) {
      auto s = sym.asString();
      units[s] = b.metadata["source_units"][s];
    }
    auto info = object({{"package", b.metadata["package"]},
                        {"upstream_revision", b.metadata["source_id"]},
                        {"units", units},
                        {"native_sha256", b.metadata["kernel_sha256"]},
                        {"provenance", origin + "source.json"},
                        {"binding", "project-authored typed adapter"}});
    task["call"] = "brick_miner::shared::" + name;
    task["packages"] = array({b.name});
    task["origin"] = "mined_application";
    if (batch == "inputs") {
      auto &values = sources.at(name);
      info["package"] = values[0];
      info["upstream_revision"] = values[1];
      info["selected_symbol"] = values[2];
      if (name != "xml_records")
        task["origin"] = "mined_api";
    }
    tasks.append(task);
    meta[name] = info;
  }
  auto stage = register_tasks(read_tree(base), tasks, headers, meta, dir);
  auto overlay = dir / "plan/stage";
  qualification::plan_inputs(overlay, stage);
  for (const auto &name : names)
    qualification::plan_inputs(dir / "plan" / name, ablate(stage, name));
  auto receipts = object();
  for (const auto &c : cases) {
    auto key = c["brick"].asString();
    receipts[c["id"].asString()] =
        check_case(base, overlay, dir, c, contracts[key], arg_order[key], b);
  }
  for (const auto &[f, p] : b.originals)
    stage["qualifications/shared/upstream/" +
          std::string(batch == "inputs" ? "formats/" : "") + f] = bytes(p);
  for (const auto *f : {"contracts.json", "cases.json", "adapter.cpp"})
    stage[origin + f] = bytes(here() / batch / f);
  if (batch != "inputs" && batch != "readers")
    stage[origin + "table.hpp"] = bytes(here() / "table.hpp");
  if (batch == "inputs") {
    stage[origin + "probe.cpp"] = bytes(here() / batch / "probe.cpp");
    for (const auto &name : b.metadata["runtime_sha256"].getMemberNames())
      stage["runtime/duckdb/lib/" + name] = bytes(runtime / name);
  }
  stage[origin + "package.json"] =
      bytes(batch == "inputs" ? here() / batch / "pins.json"
                              : root / "row-ops/mined/source/arch.json");
  auto checked = seconds();
  stage[origin + "requests.json"] =
      dump(request_checks(base, overlay, batch, contracts));
  stage[origin + "source.json"] = dump(b.metadata);
  stage[origin + "receipts.json"] = dump(receipts);
  stage["runtime/duckdb/lib/" + lib] = bytes(runtime / lib);
  stage["runtime/duckdb/lib/pkgconfig/" + pc] =
      bytes(runtime / "pkgconfig" / pc);
  qualification::publish(stage, dir);
  auto digest = sha(stage["SHA256SUMS"]);
  write(dir / "report.json",
        object({{"batch", batch},
                {"cards", parse(stage["catalog.json"]).size()},
                {"new_cards", string_array(names)},
                {"manifest_sha256", digest},
                {"cases", receipts}}));
  write(dir / "timing.json",
        object({{"acquire_and_build_seconds", acquired - start},
                {"brick_checks_seconds", checked - acquired},
                {"end_user_checks_and_staging_seconds", seconds() - checked}}));
  std::cout << batch << " " << digest << std::endl;
}
} // namespace shared
