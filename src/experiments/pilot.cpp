#include "pilots.hpp"
#include <QTemporaryDir>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>

using namespace pilots;
namespace {
using Clock = std::chrono::steady_clock;
double elapsed(Clock::time_point t) {
  return std::chrono::duration<double>(Clock::now() - t).count();
}
J probe_spec(const std::string &key) {
  return spec_for("text/plain", "application/json",
                  object({{"outcome", key},
                          {"parameters", object()},
                          {"uncovered", array()}}));
}
struct Trial {
  Path work;
  bool first;
  J evaluate_arm(const std::string &key, const std::string &label,
                 const Path &bundle, const J &task) {
    auto dest = work / "checks" / key / label;
    auto emission = plan(bundle, probe_spec(key), dest);
    auto result = object(
        {{"spec_sha256", sha(bytes(dest / "calls/interpret/response.json"))},
         {"emission", emission}});
    if (label == "baseline" || label == "removed" ||
        label == "vocabulary_only") {
      check(emission["providers"].empty() &&
                emission["gaps"]["r"].asString().find(key) != std::string::npos,
            "unexpected control outcome: " + key);
      result["status"] = "no program";
      return result;
    }
    auto p = emission["providers"][0];
    check(emission["providers"].size() == 1 && emission["gaps"].empty() &&
              emission["interpretation_errors"].empty() &&
              p["status"] == "complete" && p["typed_gaps"].empty() &&
              p["uncovered"].empty(),
          "missing complete plan: " + key);
    bool manual = label == "manual", hash = key == "line_xxh32";
    std::string call = manual ? (hash ? "manual_hash" : "manual_escape")
                              : "brick_miner::" + key;
    check(p["source"].asString().find(call + "(") != std::string::npos,
          "emitter selected another callable");
    auto header = "miner_" + key + ".hpp";
    auto raw = bytes(bundle / "headers" / header);
    check(bytes(dest / "bricks" / header) == raw,
          "stager changed provider bytes");
    save(dest / "candidate.hpp", raw);
    Path client = first ? root / "tests/client.cpp"
                        : root / "batch2/clients" / (key + ".cpp");
    auto build = [&] {
      Strings cmd = {"c++", "-std=c++17", "-O1", "-fPIC", "-DAPPLY=" + call};
      if (hash)
        cmd.push_back("-DTEST_XXH32");
      append(cmd, {"-include", dest / "candidate.hpp", client});
      if (manual && !hash)
        cmd.push_back(work / "build/gdbsupport/xml-utils.o");
      append(cmd, {"-o", dest / "client"});
      append(cmd, flags(strings(task["packages"])));
      run(cmd);
    };
    build();
    run({dest / "client"}, {.cwd = dest});
    if (first && label == "staged" && hash) {
      auto wrong = replace(raw, "XXH32(s.data(), s.size(), 0)", "0");
      check(wrong != raw, "missing XXH32 mutation site");
      save(dest / "candidate.hpp", wrong);
      build();
      auto mutant = execute({dest / "client"}, {.cwd = dest});
      check(mutant.status == 1, "wrong XXH32 implementation escaped");
      save(dest / "candidate.hpp", raw);
      build();
    }
    if (manual && !hash) {
      save(dest / "component.hpp", p["source"].asString());
      save(dest / "converter.cpp",
           "#include \"component.hpp\"\nint main(int argc,char** "
           "argv){if(argc!=3)return "
           "2;try{component_0(argv[1],argv[2]);}catch(...){return 1;}}\n");
      std::set<std::string> pkgs;
      for (const auto &h : load(dest / "components/helpers.json"))
        for (const auto &q : h["packages"])
          pkgs.insert(q.asString());
      Strings cmd = {"c++",
                     "-std=c++17",
                     "-fPIC",
                     "-O1",
                     "-I",
                     dest / "bricks",
                     dest / "converter.cpp",
                     work / "build/gdbsupport/xml-utils.o",
                     "-o",
                     dest / "converter"};
      append(cmd, flags({pkgs.begin(), pkgs.end()}));
      run(cmd);
    } else
      build_emission(p, dest, dest / "converter");
    result["status"] = "passed";
    result["client"] = "passed";
    result["checks"] = evaluate(dest / "converter", task["cases"], dest);
    return result;
  }
  void qualify(const std::string &mode) {
    auto base = historical_base();
    auto files = read_tree(base);
    auto started = Clock::now();
    auto candidates = first ? original_pilot(work) : api_batch(work);
    J times = object({{"acquisition", elapsed(started)}});
    auto stage = register_tasks(files, candidates.tasks, candidates.headers,
                                candidates.meta, work);
    auto stage_path = work / "staged-arm";
    write_tree(stage_path, stage);
    auto optional = stage;
    Path other = work / "comparison-arm";
    if (first) {
      J manual = candidates.tasks;
      Files headers;
      for (auto &t : manual) {
        auto k = t["id"].asString();
        bool hash = k == "line_xxh32";
        t["call"] = hash ? "manual_hash" : "manual_escape";
        headers[k] =
            bytes(root / "comparison" / (hash ? "xxh32.hpp" : "xml.hpp"));
      }
      optional = register_tasks(files, manual, headers, candidates.meta, work);
    } else
      for (const auto &t : candidates.tasks)
        optional = ablate(std::move(optional), t["id"].asString());
    write_tree(other, optional);
    J reports = object(), hashes = object();
    for (auto task : candidates.tasks) {
      auto key = task["id"].asString();
      task.removeMember("id");
      auto t = Clock::now();
      J report = object();
      auto removed = work / "removed-arm";
      write_tree(removed, ablate(stage, key));
      for (const auto &[label, path] :
           std::vector<std::pair<std::string, Path>>{
               {"baseline", base},
               {"staged", stage_path},
               {"removed", removed},
               {first ? "manual" : "vocabulary_only", other}})
        report[label] = evaluate_arm(key, label, path, task);
      fs::remove_all(removed);
      times[key] = object({{"qualification", elapsed(t)}});
      auto q = report["staged"];
      auto p = q["emission"]["providers"][0];
      auto header = "miner_" + key + ".hpp";
      hashes[key] = sha(candidates.headers.at(key));
      Path client = first ? root / "tests/client.cpp"
                          : root / "batch2/clients" / (key + ".cpp");
      J receipt = object({{"header", header},
                          {"sha256", hashes[key]},
                          {"test_sha256", sha(bytes(client))},
                          {"spec_sha256", q["spec_sha256"]},
                          {"baseline", report["baseline"]["emission"]},
                          {"checks", q["checks"]},
                          {"removed", report["removed"]["emission"]},
                          {"bricks", p["bricks"]},
                          {"emitted_sha256", sha(p["source"].asString())}});
      if (!first) {
        auto control =
            load(root / "evidence/batch2/before.json")["baseline"][key];
        check(probe_spec(key) == control["interpretation"],
              "batch2 baseline interpretation drift");
        check(report["baseline"]["emission"] == control["emission"],
              "batch2 baseline emission drift");
        receipt["vocabulary_only"] = report["vocabulary_only"]["emission"];
      }
      auto &e = candidates.evidence[key];
      e["provenance.json"] = dump(candidates.meta[key]);
      e["client.cpp"] = bytes(client);
      e["task.json"] = dump(task);
      e["spec.json"] = dump(probe_spec(key));
      e["receipt.json"] = dump(receipt);
      for (const auto &[file, raw] : e)
        stage["qualifications/miner_" + key + "/" + file] = raw;
      reports[key] = report;
    }
    fs::remove_all(stage_path);
    fs::remove_all(other);
    auto hash = seal(stage);
    auto parent = mode == "fixture" ? Path("/tmp") : work;
    auto draft = parent / ".qualifying";
    write_tree(draft, stage);
    run({"sha256sum", "--strict", "--quiet", "--check", "SHA256SUMS"},
        {.cwd = draft});
    fs::rename(draft, parent / "bundle");
    auto pin = load(root / "contracts/inputs.json");
    J tools = object();
    for (const auto *tool :
         {"brick-provider-plan", "unllm_logic_worker", "brick-index-formats"})
      tools[tool] = sha(bytes(main_repo / "build/native" / tool));
    J summary =
        object({{"mode", mode},
                {"base", base.string()},
                {"base_tree_sha256", pin["base_tree_sha256"]},
                {"base_catalog_sha256", sha(files["catalog.json"])},
                {"base_verification_sha256", sha(files["verification.json"])},
                {"tools", tools},
                {"manifest_sha256", hash},
                {"header_sha256", hashes},
                {"verification_sha256", sha(stage["verification.json"])},
                {"published_files_checked", unsigned(stage.size() - 1)},
                {"results", reports},
                {"provenance", candidates.meta},
                {"seconds", times}});
    if (first) {
      summary["rejected"] = candidates.rejected;
      summary["duplicate"] =
          object({{"symbol", "xml_escape_text_append"},
                  {"reason", "The public wrapper delegates escaping to this "
                             "helper. Count the pair once."}});
      summary["negative_control"] =
          "incorrect XXH32 result rejected by the independent client";
      summary["counts"] = object({{"examined", 4},
                                  {"distinct_accepted", 2},
                                  {"duplicate_entry_points", 1},
                                  {"rejected", 1},
                                  {"newly_passing", 2}});
      for (const auto *tool : {"clang++", "c++", "make"})
        summary["tool_versions"][tool] = first_line(run({tool, "--version"}));
      summary["selected_source_hashes"] =
          object({{"manual_xxh32", sha(bytes(root / "comparison/xxh32.hpp"))},
                  {"manual_xml", sha(bytes(root / "comparison/xml.hpp"))}});
    } else {
      summary["counts"] =
          object({{"base_cards", parse(files["catalog.json"]).size()},
                  {"staged_cards", parse(stage["catalog.json"]).size()},
                  {"examined_capabilities", candidates.tasks.size()},
                  {"distinct_accepted", candidates.tasks.size()},
                  {"rejected", 0},
                  {"duplicates", 0},
                  {"newly_passing", reports.size()}});
      save(work / "payload.SHA256SUMS", stage["SHA256SUMS"]);
    }
    write(work / "report.json", summary);
  }
};
} // namespace
int main(int argc, char **argv) try {
  check(argc >= 2, "expected first or second pilot");
  std::string batch = argv[1];
  check(batch == "first" || batch == "second", "unknown pilot");
  bool first = batch == "first";
  if (argc == 4 && std::string(argv[2]) == "--worker") {
    Trial{fs::current_path(), first}.qualify(argv[3]);
    return 0;
  }
  if (argc == 3 && std::string(argv[2]) == "--help") {
    std::cout << "Run both clean trials. Use --fixture for a temporary bundle "
                 "and retained reports.\n";
    return 0;
  }
  bool fixture = argc == 3 && std::string(argv[2]) == "--fixture";
  check(argc == 2 || fixture, "unknown pilot argument");
  QTemporaryDir temp(
      QString::fromStdString((root / ".build/fixture-XXXXXX").string()));
  check(temp.isValid(), "cannot allocate fixture directory");
  auto dir = fixture ? Path(temp.path().toStdString())
                     : fresh_run(first ? "trial" : "batch2");
  std::cout << dir << std::endl;
  J previous;
  Path overlay;
  if (!first) {
    auto pins = load(root / "batch2/sources.json");
    bool mismatch = false;
    for (const auto &s : pins)
      for (const auto &file : s["package_files"].getMemberNames())
        mismatch |= !fs::is_regular_file(file) ||
                    sha(bytes(file)) != s["package_files"][file].asString();
    if (mismatch) {
      overlay = dir / "signed-inputs";
      fs::create_directory(overlay);
      auto ring = overlay / "keyring.gpg";
      keyring(ring);
      for (const auto &s : pins) {
        Path archive = Path("/var/cache/pacman/pkg") / s["archive"].asString();
        check(sha(bytes(archive)) == s["archive_sha256"].asString(),
              "unverified overlay archive");
        signature(archive, ring);
        run({"bsdtar", "-xf", archive, "-C", overlay, "usr/include", "usr/lib",
             "usr/share"});
      }
    }
  }
  for (int n = 1; n <= 2; ++n) {
    auto work = dir / ("run-" + std::to_string(n));
    fs::create_directory(work);
    auto worker = fs::canonical("/proc/self/exe");
    Strings cmd = {"bwrap",
                   "--die-with-parent",
                   "--unshare-net",
                   "--ro-bind",
                   "/",
                   "/",
                   "--dev",
                   "/dev",
                   "--proc",
                   "/proc",
                   "--tmpfs",
                   "/tmp",
                   "--bind",
                   work,
                   "/tmp/brick-trial",
                   "--chdir",
                   "/tmp/brick-trial",
                   "--setenv",
                   "LC_ALL",
                   "C",
                   "--setenv",
                   "TZ",
                   "UTC",
                   worker,
                   batch,
                   "--worker",
                   fixture ? "fixture" : "bundle"};
    if (!overlay.empty()) {
      Strings layers = {"--overlay-src", "/usr",         "--overlay-src",
                        overlay / "usr", "--ro-overlay", "/usr"};
      auto pos = std::find(cmd.begin(), cmd.end(), "--dev");
      cmd.insert(pos, layers.begin(), layers.end());
    }
    run(cmd, {.seconds = 1200});
    auto report = load(work / "report.json");
    report.removeMember("seconds");
    check(n == 1 || report == previous, "clean pilot runs differed");
    previous = report;
  }
  if (fixture) {
    auto parent = first ? root / "evidence" : root / "evidence/batch2";
    fs::create_directories(parent);
    auto pattern = (parent / "fixture-XXXXXXXX").string();
    check(mkdtemp(pattern.data()), "cannot allocate evidence directory");
    for (int n = 1; n <= 2; ++n) {
      auto id = "run-" + std::to_string(n);
      fs::copy_file(dir / id / "report.json", Path(pattern) / (id + ".json"));
    }
    if (!first)
      fs::copy_file(dir / "run-1/payload.SHA256SUMS",
                    Path(pattern) / "SHA256SUMS");
    std::cout << "Retained evidence " << pattern << "\n";
  } else
    std::cout << dir / "run-1/bundle" << "\n";
  std::cout << previous["manifest_sha256"].asString() << "\n";
  return 0;
} catch (const std::exception &e) {
  std::cerr << e.what() << "\n";
  return 1;
}
