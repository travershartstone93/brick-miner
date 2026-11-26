#include "bundle.hpp"
#include <algorithm>
#include <future>
#include <iomanip>
#include <iostream>
#include <sstream>

using namespace miner;
namespace {
const std::string base_hash =
    "e4564e3079f0b32b3379a411b170ce9af61ec80ab7938b6a974e724abd538328";
Path replay() { return runs / "english-audit-bcsgf8pi/replay-staged"; }
Path review() { return runs / "shared-review-5ui86km8"; }
J emission(const Path &p) { return load(p / "run/components/emission.json"); }
using Pair = std::pair<std::string, std::string>;
Pair ports(const J &p) { return {p["from"].asString(), p["to"].asString()}; }
using Groups = std::map<std::string, std::set<std::string>>;
J groups(const Groups &groups) {
  J result = object();
  for (const auto &[k, ids] : groups)
    result[k] = string_array({ids.begin(), ids.end()});
  return result;
}
J rank() {
  Groups stops, validations, diagnostics;
  std::map<Pair, J> holes;
  std::set<std::string> actual;
  for (const auto &entry : fs::directory_iterator(replay()))
    if (entry.path().filename().string().starts_with("H"))
      actual.insert(entry.path().filename());
  for (int n = 1; n <= 100; ++n) {
    std::ostringstream s;
    s << "H" << std::setw(3) << std::setfill('0') << n;
    auto key = s.str();
    check(actual.erase(key), "missing request " + key);
    auto dir = replay() / key;
    auto plan = emission(dir), receipt = load(dir / "receipt.json");
    check(receipt["request"] == key && receipt["interpreted"].asBool() &&
              !receipt["editor"].asBool(),
          "unexpected replay arm " + key);
    bool hole = false, residual = false;
    for (const auto &p : plan["providers"]) {
      residual |= !p["uncovered"].empty();
      for (const auto &g : p["typed_gaps"]) {
        holes[ports(g)].append(array({key, p["slot"], g["name"]}));
        hole = true;
      }
    }
    auto errors = load(dir / "run/result.json")["interpretation_errors"];
    for (const auto &message : errors) {
      auto m = message.asString();
      validations[m.substr(0, m.find(" expects "))].insert(key);
    }
    bool empty_order = true;
    for (const auto &msg : plan["gaps"]) {
      auto m = msg.asString();
      diagnostics[m].insert(key);
      empty_order &= m.find("between one and 32") != m.npos;
    }
    std::string reason;
    if (!errors.empty())
      reason = "invalid interpretation";
    else if (hole)
      reason = "typed conversion gap";
    else if (!plan["gaps"].empty())
      reason = empty_order ? "no ordered outcomes"
                           : "no complete chain for the requested operations";
    else {
      check(residual, "unexplained replay stop " + key);
      reason = "uncovered obligations without a typed gap";
    }
    stops[reason].insert(key);
  }
  check(actual.empty(), "unexpected request in replay");
  std::vector<J> rows;
  for (const auto &[pair, occurrences] : holes) {
    std::set<std::string> ids;
    for (const auto &row : occurrences)
      ids.insert(row[0].asString());
    rows.push_back(object({{"source", pair.first},
                           {"destination", pair.second},
                           {"requests", string_array({ids.begin(), ids.end()})},
                           {"count", unsigned(ids.size())},
                           {"occurrences", occurrences}}));
  }
  std::sort(rows.begin(), rows.end(), [](const J &a, const J &b) {
    if (a["count"] != b["count"])
      return a["count"] > b["count"];
    return std::tie(a["source"], a["destination"]) <
           std::tie(b["source"], b["destination"]);
  });
  J typed = array();
  for (auto &r : rows)
    typed.append(std::move(r));
  return object({{"typed", typed},
                 {"stops", groups(stops)},
                 {"validation_parameters", groups(validations)},
                 {"role_diagnostics", groups(diagnostics)}});
}
std::pair<J, J> qualify(const Path &work, const Path &base, const J &contract) {
  J schema = load(base / "providers/contracts.json"), components = object(),
    tested = array(), winners = array();
  for (const auto &c : load(base / "components.json"))
    components[c["id"].asString()] = c;
  for (const auto &p : schema["providers"]) {
    check(ports(p) != ports(contract), "conversion already present");
    check(p["id"] != contract["provider_id"], "provider identity occupied");
    if (p["from"] != contract["from"] ||
        !p["to"].asString().starts_with("out:") ||
        !p["effect"].asString().empty() || !p["args"].empty() ||
        !p["when"].empty())
      continue;
    auto d = work / p["id"].asString();
    fs::create_directories(d);
    std::string args =
        p["destination_first"].asBool() ? "path, records" : "records, path";
    save(d / "binding.hpp",
         "#include \"" + p["header"].asString() +
             "\"\n#include <string>\n#include <vector>\ninline void "
             "write_candidate(const std::vector<std::string>& records, const "
             "char* path) {\n    " +
             p["call"].asString() + "(" + args + ");\n}\n");
    auto pkgs = components[p["brick"].asString()]["packages"];
    auto extra = flags(strings(pkgs));
    auto exe = d / "client";
    Strings cmd = {"g++-15",
                   "-std=c++17",
                   "-fPIC",
                   "-I",
                   d,
                   "-I",
                   base / "headers",
                   "-I",
                   base / "providers",
                   root / "replay-gaps/client.cpp",
                   "-o",
                   exe};
    append(cmd, extra);
    run(cmd);
    bool all = true;
    J checks = object();
    for (const auto &c : contract["cases"]) {
      auto path = d / c["name"].asString();
      bool error = c.isMember("error");
      if (error)
        save(path / "marker", "preserve");
      else
        save(path, "old bytes");
      auto argv = sandbox(exe, d);
      argv.push_back(path.filename());
      append(argv, strings(c["records"]));
      auto res = execute(argv, {.seconds = 5, .probe_limits = true});
      bool passed =
          error ? (res.status == 1 && bytes(path / "marker") == "preserve")
                : (res.status == 0 && bytes(path) == c["expected"].asString());
      checks[c["name"].asString()] = passed;
      all &= passed;
    }
    tested.append(
        object({{"provider", p},
                {"packages", pkgs},
                {"checks", checks},
                {"flags", string_array(extra)},
                {"header_sha256",
                 sha(bytes(base / "headers" / p["header"].asString()))}}));
    if (all)
      winners.append(p);
  }
  check(winners.size() == 1, "expected unique TSV writer");
  auto provider = winners[0];
  provider["id"] = contract["provider_id"];
  provider["to"] = contract["to"];
  return {provider, tested};
}
std::string stage(const Path &dest, const Path &base, const J &contract) {
  auto [provider, tested] = qualify(dest / "clients", base, contract);
  auto files = read_tree(base);
  files.erase("SHA256SUMS");
  auto schema = parse(files.at("providers/contracts.json"));
  schema["providers"].append(provider);
  files["providers/contracts.json"] = dump(schema);
  std::string prefix = "qualifications/replay-gaps/tsv/";
  for (const auto *file : {"tsv-contract.json", "client.cpp"})
    files[prefix + file] = bytes(root / "replay-gaps" / file);
  files[prefix + "discovery.json"] = dump(object({{"base_sha256", base_hash},
                                                  {"candidates", tested},
                                                  {"registration", provider}}));
  auto hash = seal(files);
  auto draft = dest / ".bundle";
  write_tree(draft, files);
  run({"sha256sum", "--strict", "--quiet", "-c", "SHA256SUMS"}, {.cwd = draft});
  fs::rename(draft, dest / "bundle");
  return hash;
}
std::set<int> affected(const J &plan, const Pair &target) {
  std::set<int> slots;
  for (const auto &p : plan["providers"])
    for (const auto &g : p["typed_gaps"])
      if (ports(g) == target)
        slots.insert(p["slot"].asInt());
  return slots;
}
J scores(const Path &dir) {
  auto receipt = load(dir / "receipt.json"), result = receipt["initial"];
  for (auto k : receipt["held"].getMemberNames())
    result[k] = receipt["held"][k];
  check(result.getMemberNames() ==
            Strings{"boundary", "empty", "ordinary", "sparse", "transfer"},
        "missing fixture scores: " + dir.string());
  return result;
}
J compare(const Path &work, const Path &base, const Path &staged, const J &top,
          const J &contract) {
  auto target = ports(contract);
  auto restored = load(staged / "providers/contracts.json"),
       providers = array();
  for (const auto &p : restored["providers"])
    if (p["id"] != contract["provider_id"])
      providers.append(p);
  restored["providers"] = providers;
  check(restored == load(base / "providers/contracts.json"),
        "ablation differs from baseline");
  auto refs = load(review() / "transfer/transfer.json");
  std::string ck = "H029";
  Path rel = "run/calls/interpret/response.json";
  auto ctrl = load(replay() / ck / rel), ops = array();
  check(refs[ck]["roles"].size() == 1, "unexpected control role count");
  for (const auto &item : refs[ck]["roles"][0])
    ops.append(object({{"outcome", item[0]}, {"parameters", item[1]}}));
  ctrl["roles"][0]["requirement"]["operations"] = ops;
  ctrl["roles"][0]["requirement"]["uncovered"] = array();
  auto control = work / "correct-operations.json";
  write(control, ctrl);
  auto ids = strings(top["requests"]);
  auto runner = main_repo / "build/native/brick-row-replay";
  for (bool with : {true, false}) {
    std::string arm = with ? "with-writer-staged" : "without-writer-staged";
    auto bundle = with ? staged : base;
    const auto bundle_pin = sha(bytes(bundle / "SHA256SUMS"));
    auto trial = [&](const std::string &label, const std::string &key,
                     const Path &spec) {
      run({runner, work, label, key, bundle, spec},
          {.seconds = 480,
           .environment = {
               {"B42_RUNTIME", (bundle / "runtime/duckdb/lib").string()},
               {"B42_ABLATION", ""},
               {"CORPUS_ROOT", (volume / "unllm").string()}}});
      auto receipt = load(work / label / key / "receipt.json");
      check(receipt["manifest"] == bundle_pin &&
                receipt["spec_sha256"] == sha(bytes(spec)) &&
                receipt["interpreted"] == true && receipt["editor"] == false,
            "changed comparison context in " + label + "/" + key);
    };
    for (size_t i = 0; i < ids.size(); i += 3) {
      std::vector<std::future<void>> jobs;
      for (size_t j = i; j < std::min(ids.size(), i + 3); ++j)
        jobs.push_back(std::async(std::launch::async, [&, key = ids[j]] {
          trial(arm, key, replay() / key / rel);
        }));
      for (auto &job : jobs)
        job.get();
    }
    trial("control-" + arm, ck, control);
  }
  auto requests = object();
  for (const auto &key : ids) {
    auto before = work / "without-writer-staged" / key,
         after = work / "with-writer-staged" / key;
    auto slots = affected(emission(before), target);
    check(!slots.empty() && slots == affected(emission(replay() / key), target),
          "baseline slot drift");
    auto plan = emission(after);
    std::set<int> bound;
    auto gaps = array(), uncovered = array(), slot_list = array();
    for (const auto &p : plan["providers"]) {
      for (const auto &s : p["plan"]["steps"])
        if (s["provider"] == contract["provider_id"])
          bound.insert(p["slot"].asInt());
      for (const auto &g : p["typed_gaps"])
        gaps.append(array({g["from"], g["to"]}));
      uncovered.append(p["uncovered"]);
    }
    check(slots == bound && affected(plan, target).empty(),
          "binding failed " + key);
    auto initial = scores(before);
    check(initial == scores(replay() / key), "baseline fixture drift " + key);
    for (int slot : slots)
      slot_list.append(slot);
    requests[key] =
        object({{"slots", slot_list},
                {"before", initial},
                {"after", scores(after)},
                {"remaining_typed_gaps", gaps},
                {"uncovered", uncovered},
                {"accepted", load(after / "receipt.json")["verified"]}});
  }
  auto ca = work / "control-without-writer-staged" / ck,
       cb = work / "control-with-writer-staged" / ck;
  check(affected(emission(ca), target) == std::set<int>{0} &&
            load(cb / "receipt.json")["verified"].asBool() &&
            affected(emission(cb), target).empty(),
        "control failed");
  return object({{"requests", requests},
                 {"control", object({{"request", ck},
                                     {"before", scores(ca)},
                                     {"after", scores(cb)}})}});
}

void record_replay_programs(const Path &work) {
  J records = object();
  std::vector<Path> programs;
  for (const auto &arm : fs::directory_iterator(work))
    if (arm.is_directory())
      for (const auto &trial : fs::directory_iterator(arm.path())) {
        const auto dir = trial.path();
        const auto receipt = dir / "receipt.json";
        if (!fs::is_regular_file(receipt))
          continue;
        const auto relative = dir.lexically_relative(work).generic_string();
        records[relative]["receipt_sha256"] = sha(bytes(receipt));
        const auto program = dir / "run/project/converter";
        if (fs::is_regular_file(program)) {
          records[relative]["program_sha256"] = sha(bytes(program));
          programs.push_back(program);
        }
      }
  write(work / "tested-programs.json", records);
  run({"sync", "-f", work / "tested-programs.json"});
  for (const auto &program : programs)
    fs::remove(program);
}
} // namespace

int main(int argc, char **argv) try {
  if (argc == 2 && std::string(argv[1]) == "--rank") {
    std::cout << dump(rank());
    return 0;
  }
  const bool existing =
      argc == 5 && std::string(argv[1]) == "--compare-existing";
  check(
      argc == 1 || existing,
      "usage: replay-gaps/run [--rank | --compare-existing WORK BASE STAGED]");
  auto ranking = rank();
  const auto top = ranking["typed"][0],
             contract = load(root / "replay-gaps/tsv-contract.json");
  check(ports(contract) ==
                Pair{top["source"].asString(), top["destination"].asString()} &&
            top["count"] > ranking["typed"][1]["count"],
        "leading gap changed");
  if (existing) {
    const auto work = Path(argv[2]), base = fs::canonical(argv[3]),
               staged = fs::canonical(argv[4]);
    check_corpus(work.parent_path());
    check(fs::create_directory(work), "comparison requires a fresh directory");
    for (const auto &bundle : {base, staged})
      run({"sha256sum", "--strict", "--quiet", "-c", "SHA256SUMS"},
          {.cwd = bundle});
    write(work / "ranking.json", ranking);
    auto report = compare(work, base, staged, top, contract);
    report["base_sha256"] = sha(bytes(base / "SHA256SUMS"));
    report["staged_sha256"] = sha(bytes(staged / "SHA256SUMS"));
    write(work / "report.json", report);
    record_replay_programs(work);
    std::cout << work / "report.json" << std::endl;
    return 0;
  }
  auto work = fresh_run("ranked-gaps");
  std::cout << work << std::endl;
  write(work / "ranking.json", ranking);
  auto base = Path(load(review() / "review.json")["bundle"].asString());
  check(sha(bytes(base / "SHA256SUMS")) == base_hash,
        "baseline digest mismatch");
  run({"sha256sum", "--strict", "--quiet", "-c", "SHA256SUMS"}, {.cwd = base});
  auto hash = stage(work / "run-1", base, contract),
       repeat = stage(work / "run-2", base, contract);
  check(hash == repeat, "staged manifests differ");
  auto staged = work / "run-1/bundle";
  auto report = compare(work, base, staged, top, contract);
  auto active = fs::canonical(volume / "unllm/boilerplate/active");
  report["source_replay"] = replay().string();
  report["base"] = base.string();
  report["base_sha256"] = base_hash;
  report["staged_bundle"] = staged.string();
  report["staged_sha256"] = hash;
  report["repeat_sha256"] = repeat;
  report["published_catalog"] = active.string();
  report["published_verification_sha256"] =
      sha(bytes(active / "verification.json"));
  write(work / "report.json", report);
  std::cout << work / "report.json" << std::endl;
  return 0;
} catch (const std::exception &e) {
  std::cerr << e.what() << "\n";
  return 1;
}
