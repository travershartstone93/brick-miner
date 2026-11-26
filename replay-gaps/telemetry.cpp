#include "bundle.hpp"
#include <iostream>
#include <sstream>

using namespace miner;
namespace {
Path archive() { return runs / "english-audit-bcsgf8pi/replay-staged"; }
Path base() { return runs / "shared-needs-ot7p54bf/run-2/decisions/bundle"; }
Path with_writer() { return runs / "ranked-gaps-5x2_hyp7/run-1/bundle"; }
Path replay_tool() { return main_repo / "build/native/brick-row-replay"; }
const std::string base_pin =
    "e4564e3079f0b32b3379a411b170ce9af61ec80ab7938b6a974e724abd538328";
const std::string writer_pin =
    "88eff82d2bba78d6049a50be4d625e7bac5b21dda8019c52bca1c6e25ec7d7bc";
const auto fields = string_array({"sensor", "room", "minute", "temp"});
const Strings labels{"ordinary", "boundary", "empty", "transfer", "sparse"};
const std::map<std::string, std::string> requests{
    {"H033",
     "68e7bb736a957875229f85e42963262ef0e96ed9b9e6c3e5b4b474a0fbfab146"},
    {"H035",
     "d2cb2a43d5728e5f4301a8c75641656e9857cfed9ed6feeb06f9aa93953d2274"},
    {"H038",
     "7ac1bff07b7d8ffd6f20ecb15ac1198e0a24ed6dc6ce3a5fbe78cd429ebc6fd8"}};
const std::map<std::string, std::string> inputs{
    {"ordinary",
     "b17b8e0a111a4cfe363d1671c59d9ffdd96a6dca8adbd86581725a46800cd5a3"},
    {"boundary",
     "698f9a43afff42b48ca69ade3912ff5b12c98135ad203758807dab36ac0c5fc3"},
    {"empty",
     "4f53cda18c2baa0c0354bb5f9a3ecbe5ed12ab4d8e11ba873c2f11161202b945"},
    {"transfer",
     "1343d9dccdc939d2a104798ed5df6914603eb36cb72da389c2df7639fa9b1b66"},
    {"sparse",
     "11c65f32f5af68dd6491a4424db4e0363a29f8f3e8482568f8ad34887e0f53b9"}};

std::string hash(const Path &p) { return sha(bytes(p)); }
Path fixture(const Path &dir, const std::string &label) {
  return dir /
         (label == "transfer" || label == "sparse" ? "held"
                                                   : "run/check-initial") /
         label / "input/readings.json";
}
J reader_trial(const Path &work, const Path &exe) {
  auto src = root / "replay-gaps/telemetry-client.cpp";
  auto lib = base() / "runtime/duckdb/lib";
  auto origin = base() / "qualifications/shared/readers";
  auto provenance = load(origin / "source.json");
  check(hash(lib / "libbrick_shared_readers.so") ==
                provenance["kernel_sha256"].asString() &&
            hash(origin / "adapter.cpp") ==
                provenance["adapter_sha256"].asString(),
        "reader implementation provenance mismatch");
  fs::create_directories(exe.parent_path());
  Strings cmd{"g++-15",
              "-std=c++17",
              "-O1",
              "-fPIC",
              "-I",
              base() / "headers",
              "-I",
              base() / "providers",
              src,
              "-o",
              exe,
              "-L" + lib.string(),
              "-Wl,-rpath-link," + lib.string(),
              "-lbrick_shared_readers",
              "-lduckdb"};
  append(cmd, flags({"Qt6Core", "jsoncpp", "pugixml"}));
  run(cmd);
  auto report =
      object({{"source_sha256", hash(origin / "source.json")},
              {"library_sha256", hash(lib / "libbrick_shared_readers.so")},
              {"duckdb_sha256", hash(lib / "libduckdb.so")},
              {"build", string_array(cmd)},
              {"client_sha256", hash(src)},
              {"program_sha256", hash(exe)}});
  for (const auto &label : labels) {
    auto file = fixture(archive() / "H033", label);
    for (const auto &[id, pin] : requests)
      check(hash(fixture(archive() / id, label)) == inputs.at(label),
            "input drift: " + id + " " + label);
    auto objects = load(file);
    check(objects.isArray(), "telemetry array required");
    auto &item = report["fixtures"][label];
    item["input_sha256"] = hash(file);
    for (const J &projection : {fields, string_array({"temp", "sensor"})}) {
      auto oracle = array();
      for (const auto &record : objects) {
        auto cells = array();
        for (const auto &key : projection) {
          const auto &v = record[key.asString()];
          check(v.isString() || v.isIntegral(), "out-of-scope telemetry value");
          cells.append(v.asString());
        }
        oracle.append(object(
            {{"position", Json::Int64(oracle.size()) + 1}, {"fields", cells}}));
      }
      auto argv = sandbox(exe);
      argv.insert(argv.end() - 1,
                  {"--ro-bind", lib, "/runtime", "--setenv", "LD_LIBRARY_PATH",
                   "/runtime", "--ro-bind", file, "/input"});
      append(argv, {"/input", dump(projection, false)});
      auto observed = parse(run(argv, {.seconds = 8, .probe_limits = true}));
      check(observed["records"] == oracle,
            "wrong row positions, field values or order: " + label);
      check(objects.empty()
                ? observed["nested_list_count"] == 0
                : observed["nested_list_error"] == "Expected an array",
            "unexpected nested-list control: " + label);
      item["projections"].append(
          object({{"fields", projection}, {"observed", observed}}));
    }
  }
  return report;
}

J verdicts(const J &receipt) {
  auto out = object();
  for (const auto &label : labels) {
    auto &s =
        receipt[label == "transfer" || label == "sparse" ? "held" : "initial"];
    if (s.isMember(label)) {
      check(s[label].isBool(), "nonboolean fixture verdict");
      out[label] = s[label];
    }
  }
  return out;
}

J prompt_menu(const Path &dir) {
  const auto prompt = dir / "calls/interpret/prompt.txt";
  std::istringstream lines(bytes(prompt));
  for (std::string line; std::getline(lines, line);) {
    if (!line.starts_with("{\"formats\":"))
      continue;
    auto menu = parse(line);
    check(menu["outcomes"] == load(dir / "vocabulary.json")["outcomes"],
          "archived prompt and vocabulary disagree");
    return menu["outcomes"];
  }
  throw std::runtime_error("missing operation menu in " + prompt.string());
}

J trial(const Path &work, const std::string &arm, const std::string &id,
        const Path &bundle, const Path &spec) {
  auto dir = work / arm / id, runtime = bundle / "runtime/duckdb/lib";
  run({replay_tool(), work, arm, id, bundle, spec},
      {.seconds = 480,
       .environment = {{"B42_RUNTIME", runtime.string()},
                       {"B42_ABLATION", ""},
                       {"CORPUS_ROOT", (volume / "unllm").string()}}});
  auto rec = load(dir / "receipt.json"),
       emitted = load(dir / "run/components/emission.json");
  check(rec["manifest"] == hash(bundle / "SHA256SUMS") &&
            rec["spec_sha256"] == hash(spec) && rec["interpreted"] == true &&
            rec["editor"] == false && rec["status"] != "runner_error" &&
            rec["status"] != "time_budget" &&
            load(dir / "run/calls/interpret/response.json") == load(spec),
        "invalid replay: " + id + " " + arm);
  auto roles = array();
  for (const auto &p : emitted["providers"])
    roles.append(object({{"steps", p["plan"]["steps"]},
                         {"typed_gaps", p["typed_gaps"]},
                         {"uncovered", p["uncovered"]},
                         {"status", p["status"]}}));
  auto scores = verdicts(rec);
  const auto requirement = load(spec)["roles"][0]["requirement"];
  check(roles.size() == 1 && roles[0]["uncovered"] == requirement["uncovered"],
        "unexpected change in role or residual obligations");
  const auto &steps = roles[0]["steps"];
  const auto &ops = requirement["operations"];
  check(steps.size() >= ops.size(), "requested operations missing from plan");
  for (unsigned i = 0; i < ops.size(); ++i) {
    auto name = ops[i]["outcome"].asString();
    check(steps[i]["provider"] == name ||
              steps[i]["provider"] == "op_" + std::to_string(i) + "_" + name,
          "wrong emitted operation: " + name);
  }
  auto report = object({{"catalog_sha256", rec["manifest"]},
                        {"spec_sha256", rec["spec_sha256"]},
                        {"receipt_sha256", hash(dir / "receipt.json")},
                        {"verdicts", scores},
                        {"verified", rec["verified"]},
                        {"status", rec["status"]},
                        {"roles", roles},
                        {"planner_diagnostics", emitted["gaps"]}});
  auto exe = dir / "run/project/converter";
  if (fs::is_regular_file(exe)) {
    check(scores.size() == labels.size(),
          "compiled program missing fixture scores");
    for (const auto &label : labels)
      check(hash(fixture(dir, label)) == inputs.at(label),
            "checker input differs: " + label);
    report["program_sha256"] = hash(exe);
  } else {
    check(scores.empty(), "verdict without program");
    report["program_sha256"] = J();
  }
  if (arm == "archived")
    check(scores == verdicts(load(archive() / id / "receipt.json")),
          "archived baseline changed");
  return report;
}
} // namespace

int main() try {
  auto work = fresh_run("v5-telemetry-replay");
  std::cout << work.string() << std::endl;
  const auto active = fs::canonical(volume / "unllm/boilerplate/active");
  check(hash(active / "SHA256SUMS") == base_pin && active != base(),
        "active generation changed");
  for (const auto &[bundle, pin] : std::vector<std::pair<Path, std::string>>{
           {base(), base_pin}, {with_writer(), writer_pin}}) {
    check(hash(bundle / "SHA256SUMS") == pin, "unexpected staged catalog");
    run({"sha256sum", "--strict", "--quiet", "-c", "SHA256SUMS"},
        {.cwd = bundle});
  }
  auto recipes_file = root / "shared/request-recipes.json";
  check(hash(recipes_file) ==
            "768676cbc34ef7db9d869520bd3d26d86123f49a3c9803901eb5653de3a6f49b",
        "reference recipes changed");
  auto recipes = load(recipes_file);
  auto client = work / ".test-programs/reader";
  auto direct = reader_trial(work, client);
  write(work / "reader.json", direct);
  auto report =
      object({{"active", active.string()},
              {"active_sha256", base_pin},
              {"staged_baseline", base().string()},
              {"writer_stage", with_writer().string()},
              {"writer_stage_sha256", writer_pin},
              {"reader_sha256", hash(work / "reader.json")},
              {"recipes_sha256", hash(recipes_file)},
              {"runner_sha256", hash(replay_tool())},
              {"checker_sha256",
               hash(main_repo / "build/native/brick-end-user-check")}});
  std::vector<Path> binaries{client};
  for (const auto &[id, pin] : requests) {
    auto original = archive() / id / "run/calls/interpret/response.json";
    check(hash(original) == pin, "archived interpretation changed: " + id);
    auto old = volume /
               "unllm/experiments/boilerplate-B42-VrtYbHSf/fresh-staged" / id /
               "run";
    auto replayed = archive() / id / "run";
    const auto old_menu = prompt_menu(old), menu = prompt_menu(replayed);
    check(hash(old / "calls/interpret/response.json") == pin &&
              old_menu.isMember("nested_lists") &&
              !old_menu.isMember("read_json_records") &&
              menu.isMember("nested_lists") &&
              menu.isMember("read_json_records"),
          "wrong original or replayed vocabulary");
    auto &lineage = report["interpretation_lineage"][id];
    lineage["source_run"] = old.string();
    lineage["response_sha256"] = pin;
    lineage["original_prompt_sha256"] =
        hash(old / "calls/interpret/prompt.txt");
    lineage["replay_prompt_sha256"] =
        hash(replayed / "calls/interpret/prompt.txt");
    lineage["original_104_menu"] = string_array(old_menu.getMemberNames());
    lineage["archived_127_replay_menu"] = string_array(menu.getMemberNames());
    std::set<std::string> omitted;
    for (const auto &op : recipes[id][0])
      if (!menu.isMember(op[0].asString()))
        omitted.insert(op[0].asString());
    lineage["reference_operations_absent_from_replay_menu"] =
        string_array({omitted.begin(), omitted.end()});
    auto saved = load(original), modified = saved;
    auto &req = modified["roles"][0]["requirement"];
    auto nested =
        object({{"outcome", "nested_lists"}, {"parameters", object()}});
    check(modified["roles"].size() == 1 &&
              req["operations"] == array({nested}) && !req["uncovered"].empty(),
          "unexpected saved telemetry interpretation");
    auto reader =
        object({{"outcome", "read_json_records"},
                {"parameters", object({{"fields", dump(fields, false)}})}});
    req["operations"] = array({reader});
    auto input = work / "specs" / id;
    write(input / "reader.json", modified);
    auto ops = array();
    check(recipes[id].size() == 1, "expected single reference role");
    for (const auto &op : recipes[id][0])
      ops.append(object({{"outcome", op[0]}, {"parameters", op[1]}}));
    check(ops[0] == reader, "author control uses different reader");
    req["operations"] = ops;
    req["uncovered"] = array();
    write(input / "reference.json", modified);
    auto measure = [&](const std::string &arm, const Path &bundle,
                       const Path &spec) {
      std::cout << arm << " " << id << std::endl;
      auto result = trial(work, arm, id, bundle, spec);
      if (!result["program_sha256"].isNull())
        binaries.push_back(work / arm / id / "run/project/converter");
      report["requests"][id][arm] = result;
      write(work / "progress.json", report);
    };
    measure("archived", base(), original);
    measure("reader-only", base(), input / "reader.json");
    measure("reference", base(), input / "reference.json");
    if (saved["roles"][0]["output_type"] == "text/tab-separated-values") {
      measure("reader-only-with-tsv-writer", with_writer(),
              input / "reader.json");
      measure("reference-with-tsv-writer", with_writer(),
              input / "reference.json");
    }
  }
  check(hash(active / "SHA256SUMS") == base_pin, "active manifest changed");
  write(work / ".report.json", report);
  run({"sync", "-f", work / ".report.json"});
  for (const auto &exe : binaries)
    fs::remove(exe);
  fs::rename(work / ".report.json", work / "report.json");
  std::cout << (work / "report.json").string() << std::endl;
  return 0;
} catch (const std::exception &e) {
  std::cerr << e.what() << '\n';
  return 1;
}
