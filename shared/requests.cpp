#include "qualification.hpp"
#include <algorithm>
#include <future>
#include <iostream>
#include <sstream>

namespace shared {
Path here() { return root / "shared"; }
Path first_batch() { return runs / "mined-rows-3ej65t4_/run-1/bundle"; }
Path stage_owner(Path pair, const std::string &batch) {
  std::set<Path> seen;
  while (seen.insert(pair).second) {
    if (fs::is_regular_file(pair / "run-1" / batch / "report.json"))
      return pair / "run-1";
    pair = fs::canonical(load(pair / "predecessor.json")["pair"].asString());
  }
  throw std::runtime_error("cyclic predecessor chain");
}
J operations(const J &steps) {
  auto result = array();
  for (const auto &step : steps)
    result.append(object({{"outcome", step[0]}, {"parameters", step[1]}}));
  return result;
}
J suite() {
  J tasks = object();
  for (const auto &t :
       parse(run({main_repo / "build/native/brick-end-user-check", "--tasks"})))
    tasks[t["id"].asString()] = t;
  check(tasks.size() == 100, "incomplete independent task suite");
  return tasks;
}
J assess(const std::string &id, const Path &exe, const Path &dest) {
  J checks = object();
  for (const auto *label : {"development", "held"}) {
    auto d = dest / label;
    fs::create_directories(d);
    Strings cmd = {main_repo / "build/native/brick-end-user-check", exe, d};
    if (std::string(label) == "held")
      cmd.push_back("held");
    auto res =
        parse(run(cmd, {.seconds = 60, .environment = {{"HARD_CASE", id}}}));
    for (const auto &k : res["checks"].getMemberNames())
      checks[k] = res["checks"][k];
  }
  check(checks.size() == 5, "wrong independent fixture count");
  return checks;
}
namespace {
const Strings mimes = {"text/tab-separated-values", "text/csv",
                       "application/vnd.sqlite3",   "application/json",
                       "application/xml",           "application/zip",
                       "inode/directory",           "text/uri-list",
                       "text/tab-separated-values", "application/json"};
J evaluate(const Path &bundle, const Path &dir, const J &task, const J &arms,
           bool baseline) {
  auto id = task["id"].asString();
  auto folder = dir / id;
  auto complete = array();
  std::set<std::string> packages;
  std::vector<Path> sources;
  bool ok = true;
  check(task["formats"].size() == arms.size(),
        "frozen role count differs from task: " + id);
  for (unsigned i = 0; i < arms.size(); ++i) {
    auto d = folder / std::to_string(i);
    auto mime = mimes.at(task["domain"].asUInt());
    auto spec = spec_for(
        mime, task["formats"][i] == "JSON" ? "application/json" : "text/plain",
        object({{"operations", operations(arms[i])}, {"uncovered", array()}}));
    auto emitted = plan(bundle, spec, d);
    const auto &providers = emitted["providers"];
    bool solved = emitted["gaps"].empty() && providers.size() == 1 &&
                  providers[0]["status"] == "complete";
    complete.append(solved);
    ok &= solved;
    if (!solved)
      continue;
    const auto &p = providers[0];
    save(d / "component.hpp", p["source"].asString());
    auto n = std::to_string(i);
    auto source = d / "arm.cpp";
    save(source, "#define component_0 arm_" + n +
                     "\n#include \"component.hpp\"\nvoid run_" + n +
                     "(const char* in,const char* out){arm_" + n +
                     "(in,out);}\n");
    sources.push_back(source);
    for (const auto &h : load(d / "components/helpers.json"))
      for (const auto &q : h["packages"])
        packages.insert(q.asString());
  }
  if (!ok) {
    if (baseline)
      return object({{"complete_roles", complete},
                     {"pass", false},
                     {"reason", "incomplete plan"}});
    throw std::runtime_error("incomplete staged roles for " + id);
  }
  auto extra = flags({packages.begin(), packages.end()});
  Strings objects;
  for (const auto &src : sources) {
    auto obj = Path(src).replace_extension(".o");
    Strings cmd = {"g++-15", "-std=c++17", "-I", src.parent_path() / "bricks"};
    append(cmd, extra);
    append(cmd, {"-c", src, "-o", obj});
    run(cmd);
    objects.push_back(obj);
  }
  std::string declarations, body;
  for (unsigned i = 0; i < arms.size(); ++i) {
    auto fn = "run_" + std::to_string(i);
    declarations += "void " + fn + "(const char*,const char*);\n";
    body += fn + "(v[1],v[" + std::to_string(i + 2) + "]);";
  }
  auto main = folder / "main.cpp", exe = folder / "program";
  save(main, declarations + "int main(int n,char**v){try{if(n!=" +
                 std::to_string(2 + arms.size()) + ")return 2;" + body +
                 "}catch(...){return 1;}}\n");
  Strings cmd = {"g++-15", "-std=c++17", main};
  append(cmd, objects);
  cmd.push_back("-Wl,-rpath,/usr/local/lib");
  append(cmd, extra);
  append(cmd, {"-o", exe});
  run(cmd);
  auto checks = assess(id, exe, folder / "fixtures");
  bool passed = std::all_of(checks.begin(), checks.end(),
                            [](const auto &c) { return c["pass_"].asBool(); });
  return baseline
             ? object({{"complete_roles", complete},
                       {"pass", passed},
                       {"checks", checks}})
             : object(
                   {{"roles", arms}, {"checks", checks}, {"passed", passed}});
}
} // namespace
J run_requests(const Path &bundle, const Path &work, bool baseline,
               const std::string &only) {
  auto tasks = suite(), recipes = load(here() / "request-recipes.json");
  check(tasks.getMemberNames() == recipes.getMemberNames(),
        "frozen interpretations do not cover task IDs");
  auto effects = load(bundle / "providers/contracts.json")["outcomes"];
  bool final = fs::is_regular_file(
      bundle / "qualifications/shared/decisions/receipts.json");
  auto result = object();
  auto path = work / (baseline ? "baseline.json" : "transfer.json");
  auto ids = tasks.getMemberNames();
  if (!only.empty()) {
    check(tasks.isMember(only), "unknown request ID");
    ids = {only};
  }
  auto one = [&](const std::string &id) -> J {
    if (baseline)
      return evaluate(bundle, work / "fixed", tasks[id], recipes[id], true);
    std::set<std::string> missing;
    for (const auto &arm : recipes[id])
      for (const auto &op : arm)
        if (!effects.isMember(op[0].asString()))
          missing.insert(op[0].asString());
    if (!missing.empty() && !final)
      return object(
          {{"passed", false},
           {"skipped", "not available in this earlier bundle"},
           {"missing", string_array({missing.begin(), missing.end()})}});
    try {
      return evaluate(bundle, work, tasks[id], recipes[id], false);
    } catch (const std::exception &e) {
      return object({{"passed", false}, {"error", e.what()}});
    }
  };
  for (size_t i = 0; i < ids.size(); i += 2) {
    std::vector<std::future<J>> jobs;
    for (size_t j = i; j < std::min(i + 2, ids.size()); ++j)
      jobs.push_back(std::async(std::launch::async, one, ids[j]));
    for (size_t j = 0; j < jobs.size(); ++j) {
      auto id = ids[i + j];
      if (baseline)
        result["fixed"][id] = jobs[j].get();
      else
        result[id] = jobs[j].get();
    }
    write(path, result);
  }
  if (baseline) {
    auto alternatives = load(here() / "baseline-recipes.json");
    for (const auto &id : alternatives.getMemberNames()) {
      if (!only.empty() && id != only)
        continue;
      auto r = evaluate(bundle, work / "alternatives", tasks[id],
                        alternatives[id], true);
      r["roles"] = alternatives[id];
      result["alternatives"][id] = r;
    }
    write(path, result);
  } else
    for (const auto &id : ids)
      check(result[id]["passed"].asBool() || result[id].isMember("skipped"),
            "failed request " + id + ": " + result[id]["error"].asString());
  return result;
}
} // namespace shared
