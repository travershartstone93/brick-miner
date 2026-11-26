#include "bundle.hpp"
#include <algorithm>
#include <iostream>

using namespace miner;
namespace {
const Strings splits{"ordinary", "boundary", "empty", "transfer", "sparse"};
const auto rows_gap = array({"strings", "rows"});
const auto writer_gap = array({"strings", "out:text/tab-separated-values"});

std::string hash(const Path &file) { return sha(bytes(file)); }

J tools() {
  auto result = object();
  for (const auto *name :
       {"brick-row-replay", "brick-provider-plan", "brick-end-user-check"})
    result[name] = hash(main_repo / "build/native" / name);
  return result;
}

Path split_dir(const Path &trial, const std::string &split) {
  return trial /
         (split == "transfer" || split == "sparse" ? "held"
                                                   : "run/check-initial") /
         split;
}

J input_hashes(const Path &trial, const std::string &name) {
  auto hashes = object();
  for (const auto &split : splits)
    hashes[split] = hash(split_dir(trial, split) / "input" / name);
  return hashes;
}

J scores(const J &receipt) {
  J result = object();
  for (const auto &split : splits) {
    const auto &score =
        receipt[split == "transfer" || split == "sparse" ? "held" : "initial"]
               [split];
    check(score.isBool(), "missing checker score for " + split);
    result[split] = score;
  }
  return result;
}

J operations(const J &recipe) {
  J list = array();
  for (const auto &step : recipe)
    list.append(object({{"outcome", step[0]}, {"parameters", step[1]}}));
  return list;
}

J requirements(const J &spec, unsigned slot) {
  return spec["roles"][slot]["requirement"]["uncovered"];
}

J trial(const Path &work, const std::string &arm, const std::string &request,
        const Path &bundle, const Path &spec_file, const std::string &input,
        const J &expected_inputs) {
  std::cout << request << " " << arm << std::endl;
  const auto dir = work / arm / request;
  run({main_repo / "build/native/brick-row-replay", work, arm, request, bundle,
       spec_file},
      {.seconds = 480,
       .environment = {
           {"B42_RUNTIME", (bundle / "runtime/duckdb/lib").string()},
           {"B42_ABLATION", ""},
           {"CORPUS_ROOT", (volume / "unllm").string()}}});
  const auto receipt = load(dir / "receipt.json");
  const auto plan_file = dir / "run/components/emission.json";
  const auto plan = load(plan_file), spec = load(spec_file);
  check(receipt["manifest"] == hash(bundle / "SHA256SUMS") &&
            receipt["spec_sha256"] == hash(spec_file) &&
            receipt["interpreted"] == true && receipt["editor"] == false &&
            receipt["status"] != "runner_error" &&
            receipt["status"] != "time_budget" &&
            plan["interpretation_errors"].empty() &&
            plan["clarifications"].empty() &&
            load(dir / "run/calls/interpret/response.json") == spec &&
            input_hashes(dir, input) == expected_inputs,
        "changed replay context for " + request + " " + arm);
  auto roles = object();
  for (const auto &p : plan["providers"]) {
    auto slot = p["slot"].asUInt();
    auto key = std::to_string(slot);
    check(slot < spec["roles"].size() && !roles.isMember(key) &&
              p["uncovered"] == requirements(spec, slot),
          "unexpected emitted role or changed residual");
    roles[key] = object({{"status", p["status"]},
                         {"steps", p["plan"]["steps"]},
                         {"typed_gaps", p["typed_gaps"]},
                         {"uncovered", p["uncovered"]}});
  }
  auto outputs = object();
  for (const auto &split : splits) {
    auto values = array();
    for (unsigned i = 1; i <= spec["roles"].size(); ++i) {
      auto file = split_dir(dir, split) / "output" /
                  ("result-" + std::to_string(i) + ".txt");
      values.append(fs::is_regular_file(file) ? J(bytes(file)) : J());
    }
    outputs[split] = values;
  }
  auto exe = dir / "run/project/converter";
  return object(
      {{"receipt_sha256", hash(dir / "receipt.json")},
       {"plan_sha256", hash(plan_file)},
       {"spec_sha256", receipt["spec_sha256"]},
       {"catalog_sha256", receipt["manifest"]},
       {"status", receipt["status"]},
       {"verified", receipt["verified"]},
       {"scores", scores(receipt)},
       {"outputs", outputs},
       {"roles", roles},
       {"planner_diagnostics", plan["gaps"]},
       {"program_sha256", fs::is_regular_file(exe) ? J(hash(exe)) : J()}});
}

J ports(const J &role) {
  J gaps = array();
  for (const auto &g : role["typed_gaps"])
    gaps.append(array({g["from"], g["to"]}));
  return gaps;
}

bool selects(const J &role, const std::string &id) {
  const auto &steps = role["steps"];
  return std::any_of(steps.begin(), steps.end(), [&](const auto &step) {
    auto p = step["provider"].asString();
    return p == id || (p.starts_with("op_") && p.ends_with("_" + id));
  });
}

bool complete(const J &role) {
  return role["status"] == "complete" && role["typed_gaps"].empty() &&
         role["uncovered"].empty();
}

J exact_row(const Path &bundle, const std::string &id, const std::string &from,
            const std::string &to) {
  auto all = load(bundle / "providers/contracts.json");
  J row;
  for (const auto &r : all["providers"])
    if (r["id"] == id) {
      check(row.isNull(), "ambiguous binding " + id);
      row = r;
    }
  check(row["from"] == from && row["to"] == to, "binding port mismatch " + id);
  return row;
}

J extra_case(const Path &dir, const Path &exe, const Path &bundle,
             const std::string &input, const J &expected) {
  save(dir / "input", input);
  auto cmd = sandbox(exe, dir);
  cmd.pop_back();
  append(cmd, {"--ro-bind", bundle / "runtime/duckdb/lib", "/runtime",
               "--setenv", "LD_LIBRARY_PATH", "/runtime", "/probe", "input"});
  for (unsigned i = 0; i < expected.size(); ++i)
    cmd.push_back("result-" + std::to_string(i) + ".txt");
  run(cmd, {.seconds = 12, .probe_limits = true});
  auto actual = array();
  for (unsigned i = 0; i < expected.size(); ++i)
    actual.append(bytes(dir / ("result-" + std::to_string(i) + ".txt")));
  return object({{"input_sha256", hash(dir / "input")},
                 {"expected", expected},
                 {"actual", actual},
                 {"passed", actual == expected}});
}

void verify_bundle(const Path &bundle, const std::string &expected) {
  check(hash(bundle / "SHA256SUMS") == expected, "changed replay bundle");
  run({"sha256sum", "--strict", "--quiet", "-c", "SHA256SUMS"},
      {.cwd = bundle});
}
} // namespace

int main() try {
  const auto parent = runs / "v5-queue-XXH5XMi9/stage/package";
  const auto stage = runs / "v5-tsv-writer-XXAf0x0y/stage";
  check(hash(parent / "manifest.json") == "d15aee96917ff440f58f76270e3ca4857526"
                                          "8dcfcf2bce951805c865aae3af63" &&
            hash(stage / "manifest.json") == "0047b47009df08f7ee228729eb46b4438"
                                             "ec9d59eadd7c44647ec6761eeabb994",
        "changed parent or detached writer manifest");
  const auto base = parent / "bundle", writer = stage / "bundle";
  verify_bundle(
      base, "436db92629463daee559de2cbbc1c81519e12306d5bf01cc6d2883f3140e779c");
  verify_bundle(
      writer,
      "b2f8a40d44b1589779fc82fcfa23995df5cb2b2432167e9383f6f22e9c828d54");
  const auto active = fs::canonical(volume / "unllm/boilerplate/active");
  const auto active_pin = hash(active / "SHA256SUMS");
  check(active_pin ==
            "e4564e3079f0b32b3379a411b170ce9af61ec80ab7938b6a974e724abd538328",
        "changed active replay context");
  const auto recipes_file = root / "shared/request-recipes.json";
  check(hash(recipes_file) ==
            "768676cbc34ef7db9d869520bd3d26d86123f49a3c9803901eb5653de3a6f49b",
        "changed typed reference recipes");
  const auto recipes = load(recipes_file);
  const auto archive = runs / "english-audit-bcsgf8pi/replay-staged";
  const auto original =
      volume / "unllm/experiments/boilerplate-B42-VrtYbHSf/fresh-staged";
  const auto work = fresh_run("v5-row-boundary-replay");
  std::cout << work.string() << std::endl;
  auto report =
      object({{"active", active.string()},
              {"active_manifest_sha256", active_pin},
              {"parent_manifest_sha256", hash(parent / "manifest.json")},
              {"writer_manifest_sha256", hash(stage / "manifest.json")},
              {"recipes_sha256", hash(recipes_file)},
              {"tools", tools()}});
  std::vector<Path> programs;
  for (const std::string request : {"H076", "H088"}) {
    const bool uri = request == "H076";
    const std::string input = uri ? "saved.uri" : "build.tsv";
    const auto old = archive / request;
    const auto supplied = old / "run/calls/interpret/response.json";
    const auto pin =
        uri ? "33f9bf5a8b9af50a2c330ee9466bdbc9b58566ad0f7139312f8463326703e981"
            : "25006915c8032709c70aed14fa8c756d8965fa9afefe7ee5a393343879d9f9c"
              "7";
    check(hash(supplied) == pin &&
              hash(original / request / "run/calls/interpret/response.json") ==
                  pin,
          "changed saved interpretation lineage");
    const auto old_spec = load(supplied);
    const auto input_pins = input_hashes(old, input);
    auto &entry = report["requests"][request];
    entry["supplied_sha256"] = pin;
    entry["inputs"] = input_pins;
    const auto vocabulary = old / "run/vocabulary.json";
    const auto outcomes = load(vocabulary)["outcomes"];
    std::set<std::string> omitted;
    for (const auto &recipe : recipes[request])
      for (const auto &op : recipe)
        if (!outcomes.isMember(op[0].asString()))
          omitted.insert(op[0].asString());
    entry["replay_vocabulary_sha256"] = hash(vocabulary);
    entry["fixed_operations_absent_from_replay_menu"] =
        string_array({omitted.begin(), omitted.end()});

    auto run_arm = [&](const std::string &arm, const Path &bundle,
                       const Path &spec) {
      auto data = trial(work, arm, request, bundle, spec, input, input_pins);
      if (!data["program_sha256"].isNull())
        programs.push_back(work / arm / request / "run/project/converter");
      entry["arms"][arm] = data;
      write(work / "progress.json", report);
      return data;
    };
    auto variant = [&](const std::string &arm, const Path &bundle,
                       const J &spec) {
      auto path = work / "specs" / request / (arm + ".json");
      write(path, spec);
      return run_arm(arm, bundle, path);
    };
    auto removed = [&](const std::string &arm, const Path &bundle,
                       const J &spec, const J &row) {
      auto cut = work / (request + "-" + arm + "-bundle");
      write_tree(cut, remove_binding(read_tree(bundle), row));
      entry["removed_bindings"][arm] = row;
      auto result = variant(arm, cut, spec);
      fs::remove_all(cut);
      return result;
    };
    auto original_arm = run_arm("archived", base, supplied);
    check(original_arm["scores"] == scores(load(old / "receipt.json")) &&
              original_arm["verified"] == false,
          "archived fixture results changed");

    auto adjusted = old_spec, full = old_spec;
    check(full["roles"].size() == recipes[request].size(),
          "changed role count");
    for (unsigned slot = 0; slot < full["roles"].size(); ++slot) {
      auto &requirement = full["roles"][slot]["requirement"];
      requirement["operations"] = operations(recipes[request][slot]);
      requirement["uncovered"] = array();
    }
    if (uri) {
      auto &ops = adjusted["roles"][0]["requirement"]["operations"];
      check(ops.size() == 7 && ops[3]["outcome"] == "filter_extension" &&
                ops[4]["outcome"] == "group_rows",
            "unexpected archived URI operations");
      auto replacement = array({operations(recipes[request][0])[0]});
      for (unsigned i = 4; i < ops.size(); ++i)
        replacement.append(ops[i]);
      ops = replacement;
      const auto archived_writer =
          run_arm("archived-with-tsv", writer, supplied);
      const auto reader_only = variant("reader-only", base, adjusted);
      const auto reader_and_writer =
          variant("reader-only-with-tsv", writer, adjusted);
      const auto baseline_full = variant("fixed-without-tsv", base, full);
      const auto accepted = variant("fixed", writer, full);
      check(ports(original_arm["roles"]["0"]) ==
                    array({rows_gap, array({"row_groups", "strings"}),
                           array({"integers", "strings"}), writer_gap}) &&
                ports(archived_writer["roles"]["0"]) ==
                    array({rows_gap, array({"row_groups", "strings"}),
                           array({"integers", "strings"})}) &&
                selects(reader_only["roles"]["0"], "uri_records") &&
                ports(reader_only["roles"]["0"]) ==
                    array({array({"row_groups", "strings"}),
                           array({"integers", "strings"}), writer_gap}) &&
                ports(reader_and_writer["roles"]["0"]) ==
                    array({array({"row_groups", "strings"}),
                           array({"integers", "strings"})}) &&
                ports(baseline_full["roles"]["0"]) == array({writer_gap}) &&
                accepted["verified"] == true &&
                complete(accepted["roles"]["0"]),
            "URI interventions did not isolate the observed boundaries");
      const auto no_reader =
          removed("reader-removed", writer, full,
                  exact_row(writer, "uri_records", "in:text/uri-list", "rows"));
      check(no_reader["verified"] == false &&
                dump(no_reader["planner_diagnostics"]).find("uri_records") !=
                    std::string::npos,
            "removing exact URI reader did not block fixed route");
      auto counterfeit = full;
      counterfeit["roles"][0]["requirement"]["operations"][2]["parameters"]
                 ["measure"] = "count";
      auto count = variant("count-occurrences", writer, counterfeit);
      check(complete(count["roles"]["0"]), "count probe did not construct");
      auto witness = [&](const std::string &arm) {
        auto path = work / arm / request;
        return extra_case(
            path / "distinct-control", path / "run/project/converter", writer,
            "# comment\r\nfile:///duplicate#one\r\nfile:///duplicate#one\r\n"
            "https://HoSt/Keep?q=1#one\r\nhttps://HoSt/Keep?q=1#two",
            array({"https://HoSt/Keep?q=1\n"}));
      };
      auto good = witness("fixed"), wrong = witness("count-occurrences");
      check(good["passed"] == true && wrong["passed"] == false &&
                wrong["actual"] ==
                    array({"file:///duplicate\nhttps://HoSt/Keep?q=1\n"}),
            "duplicate-only URI group did not discriminate count semantics");
      entry["distinct_witness"] =
          object({{"distinct", good}, {"count", wrong}});
    } else {
      auto &ops = adjusted["roles"][1]["requirement"]["operations"];
      check(ops.size() == 2 && ops[0]["outcome"] == "ordered_column" &&
                ops[1]["outcome"] == "ordered_column",
            "unexpected archived TSV projections");
      ops = array({ops[1]});
      const auto projection = variant("one-projection", base, adjusted);
      const auto accepted = variant("fixed", base, full);
      check(selects(original_arm["roles"]["1"], "tab_records") &&
                ports(original_arm["roles"]["1"]) == array({rows_gap}) &&
                selects(projection["roles"]["1"], "tab_records") &&
                ports(projection["roles"]["1"]).empty() &&
                projection["verified"] == false &&
                accepted["verified"] == true &&
                complete(accepted["roles"]["0"]) &&
                complete(accepted["roles"]["1"]) &&
                selects(accepted["roles"]["0"], "tab_records") &&
                selects(accepted["roles"]["1"], "tab_records") &&
                selects(accepted["roles"]["1"], "graph_witnesses"),
            "TSV interventions did not isolate projection and graph "
            "requirements");
      auto no_reader =
          removed("reader-removed", base, full,
                  exact_row(base, "tab_records", "in:text/tab-separated-values",
                            "rows"));
      check(no_reader["verified"] == false &&
                !complete(no_reader["roles"]["0"]) &&
                !complete(no_reader["roles"]["1"]),
            "TSV reader removal did not block both roles");
      auto no_graph =
          removed("graph-removed", base, full,
                  exact_row(base, "graph_witnesses", "rows", "rows"));
      check(no_graph["verified"] == false && complete(no_graph["roles"]["0"]) &&
                !complete(no_graph["roles"]["1"]),
            "graph removal did not isolate the reciprocal report");
      auto counterfeit = full;
      auto &sequence = counterfeit["roles"][1]["requirement"]["operations"];
      check(sequence[1]["parameters"]["comparison"] == "eq" &&
                sequence[2]["parameters"]["comparison"] == "lt",
            "changed reciprocity or orientation controls");
      J removed_operation;
      sequence.removeIndex(2, &removed_operation);
      auto unorientated = variant("without-orientation", base, counterfeit);
      check(complete(unorientated["roles"]["1"]),
            "orientation control did not construct");
      auto witness = [&](const std::string &arm) {
        auto path = work / arm / request;
        return extra_case(path / "reciprocal-control",
                          path / "run/project/converter", base,
                          "b\ta\na\tb\ns\ts\ns\ts\na\tb\np\tq\nq\tr\n",
                          array({"s\n", "a\tb\n"}));
      };
      auto good = witness("fixed"), wrong = witness("without-orientation");
      check(good["passed"] == true && wrong["passed"] == false &&
                wrong["actual"] == array({"s\n", "a\tb\nb\ta\ns\ts\n"}),
            "reciprocal control did not discriminate orientation");
      entry["reciprocity_witness"] =
          object({{"oriented", good}, {"unorientated", wrong}});
    }
  }
  check(tools() == report["tools"] && hash(active / "SHA256SUMS") == active_pin,
        "replay inputs changed");
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
