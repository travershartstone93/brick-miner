#include "bundle.hpp"
#include "pilot.hpp"
#include <QByteArray>

namespace v5 {
namespace {
std::string unhex(const std::string &hex) {
  auto data = QByteArray::fromHex(QByteArray::fromStdString(hex));
  check(data.toHex().toStdString() == hex, "invalid byte fixture");
  return data.toStdString();
}
J probes(const Path &exe, const Path &dest, const J &cases) {
  J observed = array();
  for (unsigned i = 0; i < cases.size(); ++i) {
    const auto &c = cases[i];
    auto path = dest / ("case-" + std::to_string(i));
    fs::create_directory(path);
    auto failure = c["failure"].asString();
    if (failure != "missing-input")
      save(path / "input", unhex(c.get("input", "61620a").asString()));
    if (failure == "destination-directory")
      fs::create_directory(path / "output");
    auto argv = sandbox(exe, path);
    append(argv, {"input", "output"});
    auto proc = execute(argv, {.seconds = 8, .probe_limits = true});
    check(proc.status == (failure.empty() ? 0 : 1),
          "unexpected program exit: " + std::to_string(i));
    if (!failure.empty()) {
      check(!fs::is_regular_file(path / "output"), "partial output");
      observed.append(failure);
    } else {
      auto raw = bytes(path / "output");
      check(c.isMember("json") ? parse(raw) == c["json"]
                               : raw == unhex(c["output"].asString()),
            "incorrect fixture output");
      observed.append(sha(raw));
    }
  }
  return observed;
}
} // namespace
void integrate(const Path &extracted, const Path &dest) {
  auto base = verified_base();
  auto original = read_tree(base);
  original.erase("SHA256SUMS");
  auto registry = sources();
  auto anchors = load(v5_root() / "fixtures/annotations.json");
  J tasks = array(), meta = object();
  Files headers;
  for (const auto &a : anchors) {
    auto key = a["id"].asString();
    bool is_api = key == api;
    auto src = a["source_id"].asString();
    auto task =
        object({{"id", key},
                {"meaning", a["claims"]["postconditions"]["detail"]},
                {"from", is_api ? "strings" : "rows"},
                {"to", is_api ? "strings" : "integers"},
                {"packages", array()},
                {"origin", is_api ? "mined_api" : "mined_application"},
                {"call", (is_api ? "brick_miner::" : "brick_mined::") + key}});
    if (!is_api)
      task["args"] = object({{"column_1", "column"}});
    headers[key] = bytes(extracted / (key + ".hpp"));
    meta[key] = object({{"source_id", src},
                        {"revision", registry[src]["revision"]},
                        {"implementation_sha256", sha(headers[key])}});
    tasks.append(task);
  }
  auto stage = register_tasks(std::move(original), tasks, headers, meta, dest);
  seal(stage);
  auto draft = dest / ".bundle";
  write_tree(draft, stage);
  auto requirements = load(v5_root() / "fixtures/requirements.json");
  J report = object();
  for (const auto &t : tasks) {
    auto key = t["id"].asString();
    const auto &probe = requirements[key];
    auto spec = spec_for(probe["input_type"].asString(),
                         probe["output_type"].asString(),
                         object({{"outcome", key},
                                 {"parameters", probe["parameters"]},
                                 {"uncovered", array()}}));
    for (std::string label : {"baseline", "staged", "removed"}) {
      auto d = dest / key / label, control = d / "bundle";
      Path folder = label == "baseline" ? base : draft;
      if (label == "removed") {
        write_tree(control, ablate(stage, key));
        folder = control;
      }
      auto emission = plan(folder, spec, d);
      if (label == "removed")
        fs::remove_all(control);
      auto plans = array();
      for (const auto &p : emission["providers"])
        if (p["status"] == "complete" && p["uncovered"].empty())
          plans.append(p);
      check(emission["interpretation_errors"].empty() &&
                emission["clarifications"].empty(),
            "fixed interpretation rejected");
      if (label != "staged") {
        check(plans.empty(), "control unexpectedly passed");
        report[key][label] = object(
            {{"result", "no complete program"}, {"gaps", emission["gaps"]}});
        continue;
      }
      check(plans.size() == 1, "missing stage plan");
      auto p = plans[0];
      auto ids = strings(p["bricks"]);
      check(std::find(ids.begin(), ids.end(), "miner_" + key) != ids.end() &&
                p["source"].asString().find(t["call"].asString() + "(") !=
                    std::string::npos,
            "wrong provider");
      auto binary = d / "program";
      build_emission(p, d, binary, "g++-15");
      std::set<std::string> packages;
      for (const auto &h : load(d / "components/helpers.json"))
        for (const auto &pkg : h["packages"])
          packages.insert(pkg.asString());
      report[key][label] = object(
          {{"result", "passed"},
           {"bricks", p["bricks"]},
           {"emitted_sha256", sha(p["source"].asString())},
           {"probes", probes(binary, d, probe["cases"])},
           {"packages", string_array({packages.begin(), packages.end()})}});
    }
  }
  write(dest / "integration.json", report);
  run({"sha256sum", "--quiet", "--strict", "--check", "SHA256SUMS"},
      {.cwd = draft});
  fs::rename(draft, dest / "bundle");
}
} // namespace v5
