#include "bundle.hpp"
#include <iostream>

using namespace miner;
namespace {
const std::string base_pin =
    "e4564e3079f0b32b3379a411b170ce9af61ec80ab7938b6a974e724abd538328";
const Strings cases{"ordinary", "boundary", "empty", "transfer", "sparse"};

std::string hash(const Path &path) { return sha(bytes(path)); }

Path case_folder(const Path &trial, const std::string &name) {
  return trial /
         (name == "transfer" || name == "sparse" ? "held"
                                                 : "run/check-initial") /
         name;
}

J fixtures(const Path &trial) {
  J result = object();
  for (const auto &name : cases) {
    auto dir = case_folder(trial, name) / "input";
    for (const auto &file : fs::recursive_directory_iterator(dir)) {
      auto relative = file.path().lexically_relative(dir).generic_string();
      if (fs::is_symlink(file.symlink_status()))
        result[name][relative] =
            object({{"link", fs::read_symlink(file.path()).generic_string()}});
      else if (file.is_directory())
        result[name][relative] = object({{"directory", true}});
      else {
        check(file.is_regular_file(), "unsupported fixture input");
        result[name][relative] = object({{"sha256", hash(file.path())}});
      }
    }
  }
  return result;
}

J operation(const std::string &effect, const J &parameters = object()) {
  return object({{"outcome", effect}, {"parameters", parameters}});
}

std::string fixed_output(const Path &dest, const Path &input, const Path &exe,
                         const Path &bundle) {
  fs::create_directories(dest);
  auto argv = sandbox(exe, dest);
  argv.pop_back();
  auto runtime = bundle / "runtime/duckdb/lib";
  append(argv, {"--ro-bind", input, "/case/input", "--ro-bind", runtime,
                "/runtime", "--setenv", "LD_LIBRARY_PATH", "/runtime", "/probe",
                "input", "result.txt"});
  run(argv, {.seconds = 8, .probe_limits = true});
  return bytes(dest / "result.txt");
}

J trial(const Path &work, const std::string &arm, const std::string &id,
        const Path &bundle, const Path &spec_path,
        const Path &original_fixtures, const J &inputs) {
  auto dir = work / arm / id;
  auto runner = main_repo / "build/native/brick-row-replay";
  std::cout << arm << " " << id << std::endl;
  run({runner, work, arm, id, bundle, spec_path},
      {.seconds = 480,
       .environment = {
           {"B42_RUNTIME", (bundle / "runtime/duckdb/lib").string()},
           {"B42_ABLATION", ""},
           {"CORPUS_ROOT", (volume / "unllm").string()}}});
  const auto rec = load(dir / "receipt.json");
  const auto emitted = load(dir / "run/components/emission.json");
  const auto spec = load(spec_path);
  check(rec["manifest"] == hash(bundle / "SHA256SUMS") &&
            rec["spec_sha256"] == hash(spec_path) &&
            rec["interpreted"] == true && rec["editor"] == false &&
            rec["status"] != "runner_error" && rec["status"] != "time_budget" &&
            emitted["interpretation_errors"].empty() &&
            load(dir / "run/calls/interpret/response.json") == spec,
        "invalid path replay: " + id + " " + arm);
  auto report = object({{"catalog_sha256", rec["manifest"]},
                        {"spec_sha256", rec["spec_sha256"]},
                        {"receipt_sha256", hash(dir / "receipt.json")},
                        {"status", rec["status"]},
                        {"verified", rec["verified"]},
                        {"planner_diagnostics", emitted["gaps"]},
                        {"providers", array()}});
  for (const auto &p : emitted["providers"])
    report["providers"].append(object({{"steps", p["plan"]["steps"]},
                                       {"typed_gaps", p["typed_gaps"]},
                                       {"uncovered", p["uncovered"]}}));
  auto exe = dir / "run/project/converter";
  if (emitted["providers"].empty()) {
    check(rec["verified"] == false && rec["emitted"].empty(),
          "unexpected delivery without emitted providers");
    if (fs::is_regular_file(exe)) {
      check(bytes(dir / "run/project/converter.cpp")
                    .find("#include \"gaps.hpp\"") != std::string::npos,
            "unexpected executable for a blocked replay");
      report["placeholder_sha256"] = hash(exe);
      for (const auto &name : cases) {
        const bool held = name == "transfer" || name == "sparse";
        const auto score = rec[held ? "held" : "initial"][name];
        check(score == false, "gap placeholder passed an application check");
      }
    }
  } else {
    check(fs::is_regular_file(exe), "emitted path provider without executable");
    const bool zip = id == "H058";
    const auto generated = fixtures(dir);
    check((zip || generated == inputs) && emitted["providers"].size() == 1,
          "fixture or role changed");
    const auto steps = emitted["providers"][0]["plan"]["steps"];
    const auto ops = spec["roles"][0]["requirement"]["operations"];
    check(steps.size() == ops.size() + 1 &&
              steps[steps.size() - 1]["provider"] == "write_lines",
          "unexpected path operation chain");
    for (unsigned i = 0; i < ops.size(); ++i) {
      auto got = steps[i]["provider"].asString();
      auto want = ops[i]["outcome"].asString();
      const auto label = "op_" + std::to_string(i) + "_";
      if (got.starts_with(label))
        got.erase(0, label.size());
      if (i == 0 && want == "archive_files")
        want = "archive_files_zip";
      if (want == "ordered_column")
        want = "column";
      check(got == want,
            "unexpected emitted operation " + got + " for " + want);
    }
    report["program_sha256"] = hash(exe);
    for (const auto &name : cases) {
      const bool held = name == "transfer" || name == "sparse";
      auto score = rec[held ? "held" : "initial"][name];
      auto output = case_folder(dir, name) / "output/result-1.txt";
      check(score.isBool(), "missing fixture verdict " + name);
      report["cases"][name] =
          object({{"passed", score}, {"output", bytes(output)}});
      if (zip) {
        auto fixed = dir / "fixed-bytes" / name;
        auto source = case_folder(original_fixtures, name) / "input/backup.zip";
        auto fixed_bytes = fixed_output(fixed, source, exe, bundle);
        check(fixed_bytes == bytes(output),
              "exact archived ZIP bytes produced different output");
        report["cases"][name]["generated_inputs"] = generated[name];
        report["cases"][name]["archived_input_sha256"] = hash(source);
        report["cases"][name]["archived_output_sha256"] = sha(fixed_bytes);
      }
    }
  }
  return report;
}
} // namespace

int main() try {
  const auto active = fs::canonical(volume / "unllm/boilerplate/active");
  const auto base = runs / "shared-needs-ot7p54bf/run-2/decisions/bundle";
  check(active != base && hash(active / "SHA256SUMS") == base_pin &&
            hash(base / "SHA256SUMS") == base_pin,
        "unexpected path replay parent");
  run({"sha256sum", "--strict", "--quiet", "-c", "SHA256SUMS"}, {.cwd = base});
  const auto archive = runs / "english-audit-bcsgf8pi/replay-staged";
  const auto recipes_file = root / "shared/request-recipes.json";
  check(hash(recipes_file) ==
            "768676cbc34ef7db9d869520bd3d26d86123f49a3c9803901eb5653de3a6f49b",
        "reference recipes changed");
  const auto recipes = load(recipes_file);
  const auto tool = main_repo / "build/native/brick-row-replay";
  const auto checker = main_repo / "build/native/brick-end-user-check";
  const auto work = fresh_run("v5-path-replay");
  std::cout << work.string() << std::endl;
  auto report = object({{"active", active.string()},
                        {"baseline_sha256", base_pin},
                        {"recipes_sha256", hash(recipes_file)},
                        {"runner_sha256", hash(tool)},
                        {"checker_sha256", hash(checker)}});
  const auto top_zip =
      volume / "unllm/experiments/v5-parent-transfer-QCn3p5ZQ/top-level.zip";
  const auto root_tree = work / "root-fixtures/project";
  const Strings root_files{".profile",    "ROOT.TxT",    "notes/README",
                           "notes/a.tXt", "left/README", "right/ONLY.TXT"};
  for (const auto &file : root_files)
    save(root_tree / file, "fixed");
  check(hash(top_zip) ==
            "e32722224571988df699d7138592e6e0fc020c5c6d683889ccef68486374734e",
        "changed independent root ZIP fixture");
  report["root_inputs"] =
      object({{"H058",
               object({{"path", top_zip.string()}, {"sha256", hash(top_zip)}})},
              {"H070", object({{"files", string_array(root_files)},
                               {"file_sha256", sha("fixed")}})}});
  const auto bindings = load(base / "providers/contracts.json")["providers"];
  std::vector<Path> programs;
  for (const auto &[id, pin] : std::map<std::string, std::string>{
           {"H058",
            "3b1baaa83c4eea3bc7f798671e0400f815e574d1fbd5cd2f8e72443710738d1f"},
           {"H070", "f3dc9b91685af5e17ef231a5a996efb298e92656772149905e3ed5f691"
                    "0574f6"}}) {
    auto saved = archive / id / "run/calls/interpret/response.json";
    check(hash(saved) == pin, "archived path response changed");
    auto full = load(saved);
    auto &req = full["roles"][0]["requirement"];
    const auto folder = work / "specs" / id;
    check(full["roles"].size() == 1 &&
              full["roles"][0]["output_type"] == "text/plain" &&
              recipes[id].size() == 1 && !req["uncovered"].empty(),
          "unexpected path recipe or output requirement");
    auto reference = array();
    for (const auto &op : recipes[id][0])
      reference.append(operation(op[0].asString(), op[1]));
    req["operations"] = reference;
    req["uncovered"] = array();
    write(folder / "reference.json", full);
    const auto inputs = fixtures(archive / id);
    auto &row = report["requests"][id];
    row["interpretation_sha256"] = pin;
    row["inputs"] = inputs;
    auto measure = [&](const std::string &arm, const Path &bundle,
                       const Path &spec) {
      auto result = trial(work, arm, id, bundle, spec, archive / id, inputs);
      const auto dir = work / arm / id;
      if (result.isMember("program_sha256") ||
          result.isMember("placeholder_sha256"))
        programs.push_back(dir / "run/project/converter");
      if (arm == "correlated" || arm == "exact-extension") {
        const auto output =
            fixed_output(dir / "root-check", id == "H058" ? top_zip : root_tree,
                         dir / "run/project/converter", bundle);
        auto expected = id == "H058" ? ".\n" : ".\nnotes\n";
        check(output == (arm == "exact-extension" ? "\n" : expected),
              "unexpected dot-root counterexample");
        result["root_check"] = object({{"output", output},
                                       {"expected", expected},
                                       {"passed", output == expected}});
      }
      row[arm] = result;
      write(work / "progress.json", report);
    };
    measure("archived", base, saved);
    const auto old = load(archive / id / "receipt.json");
    for (const auto &name : cases)
      check(row["archived"]["cases"][name]["passed"] ==
                old[name == "transfer" || name == "sparse" ? "held" : "initial"]
                   [name],
            "archived score changed");
    if (id == "H058") {
      auto exact = full;
      exact["roles"][0]["requirement"]["operations"] =
          array({operation("archive_files"),
                 operation("filter_extension", object({{"extension", ".csv"}})),
                 operation("parent_paths"), operation("unique_lines"),
                 operation("sort_lines")});
      write(folder / "exact-extension.json", exact);
      measure("exact-extension", base, folder / "exact-extension.json");
      check(row["exact-extension"]["verified"] == false,
            "case-sensitive control unexpectedly met the full request");
    }
    measure("correlated", base, folder / "reference.json");
    check(row["correlated"]["verified"] == true, "correlated reader failed");
    const auto reader = reference[0]["outcome"].asString();
    J raw;
    for (const auto &binding : bindings)
      if (binding["id"] == reader) {
        check(raw.isNull(), "ambiguous correlated reader");
        raw = binding;
      }
    check(raw["brick"] == "miner_" + reader && raw["effect"] == reader &&
              raw["from"] == "in:" + full["input_type"].asString() &&
              raw["to"] == "rows" && raw["args"] == array({"fields"}),
          "correlated reader identity changed");
    auto cut = work / ("control-" + id);
    write_tree(cut, remove_binding(read_tree(base), raw));
    row["removed_binding"] = raw;
    measure("reader-removed", cut, folder / "reference.json");
    const auto absent = row["reader-removed"];
    check(!absent.isMember("program_sha256") && absent["providers"].empty() &&
              dump(absent["planner_diagnostics"]).find(reader) !=
                  std::string::npos,
          "reader removal did not block " + id);
    fs::remove_all(cut);
  }
  check(hash(tool) == report["runner_sha256"].asString() &&
            hash(checker) == report["checker_sha256"].asString() &&
            hash(active / "SHA256SUMS") == base_pin,
        "path replay inputs changed during trial");
  write(work / ".report.json", report);
  run({"sync", "-f", work / ".report.json"});
  for (const auto &exe : programs)
    fs::remove(exe);
  fs::rename(work / ".report.json", work / "report.json");
  std::cout << (work / "report.json").string() << std::endl;
  return 0;
} catch (const std::exception &e) {
  std::cerr << e.what() << '\n';
  return 1;
}
