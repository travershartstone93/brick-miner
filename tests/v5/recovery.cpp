#include "queue.hpp"
#include <chrono>
#include <csignal>
#include <fcntl.h>
#include <iostream>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
using namespace v5;
namespace {
Path tool() { return root / ".build/v5-miner"; }
Path create(unsigned steps) {
  auto out = run({tool(), "pilot", "--steps", std::to_string(steps)},
                 {.seconds = 180});
  Path path = out.substr(0, out.find('\n'));
  check_corpus(path);
  return path;
}
void negative(const Strings &cmd, const std::string &expected,
              const RunOptions &options = {}) {
  auto result = execute(cmd, options);
  check(result.status != 0 &&
            (result.err + result.out).find(expected) != std::string::npos,
        "expected rejection containing " + expected + ", saw " + result.out +
            result.err);
}
void stop_worker(const Path &q, unsigned attempt, bool concurrency) {
  auto child = fork();
  check(child >= 0, "cannot spawn recovery-test client");
  if (!child) {
    auto fd =
        open((q / ("interrupted-" + std::to_string(attempt) + ".log")).c_str(),
             O_CREAT | O_WRONLY | O_TRUNC, 0600);
    if (fd < 0)
      _exit(125);
    dup2(fd, 1);
    dup2(fd, 2);
    close(fd);
    execl(tool().c_str(), "v5-miner", "resume", q.c_str(), nullptr);
    _exit(126);
  }
  int status = 0;
  try {
    Database db(q / "queue.sqlite", true);
    db.script("PRAGMA busy_timeout=1000");
    auto until = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    for (;;) {
      check(waitpid(child, &status, WNOHANG) == 0,
            "client exited before interruption");
      auto r =
          db.query("SELECT name FROM job WHERE state='running' AND attempts=?",
                   {std::to_string(attempt)});
      if (!r.empty() && fs::exists(q / "attempts" /
                                   (r[0][0] + "." + std::to_string(attempt))))
        break;
      check(std::chrono::steady_clock::now() < until,
            "no worker lease to interrupt");
      std::this_thread::sleep_for(std::chrono::milliseconds(3));
    }
    check(kill(child, SIGSTOP) == 0, "cannot suspend test client");
    if (concurrency)
      negative({tool(), "resume", q, "--steps", "1"}, "holds the worker lease");
    check(kill(child, SIGKILL) == 0, "worker disappeared");
    check(waitpid(child, &status, 0) == child && WIFSIGNALED(status),
          "worker interruption failed");
  } catch (...) {
    kill(child, SIGKILL);
    waitpid(child, &status, 0);
    throw;
  }
}
void mapping() {
  auto fixture = load(v5_root() / "fixtures/alternative-providers.json"),
       rows = fixture["providers"];
  for (const auto &c : fixture["queries"]) {
    J got;
    try {
      got = select_binding(rows, c["capability_id"].asString(),
                           c["provider_id"].asString(), c["from"].asString(),
                           c["to"].asString());
    } catch (const std::runtime_error &) {
      if (!c["expected"].isNull())
        throw;
    }
    check(got["id"] == c["expected"], "incorrect alternative-provider join");
  }
  rows.append(rows[0]);
  bool ambiguous = false;
  try {
    select_binding(rows, "reverse_record_bytes", "component_b", "strings",
                   "strings");
  } catch (const std::runtime_error &) {
    ambiguous = true;
  }
  check(ambiguous, "accepted ambiguous provider binding");
}
} // namespace
int main() try {
  auto evidence = fresh_run("v5-recovery");
  std::cout << evidence.string() << std::endl;
  auto ref = tantivy_reference(), override_dir = evidence / "tantivy-reference";
  fs::create_directory(override_dir);
  for (auto *f : {"unllm_index", "libunllm_search.a"})
    fs::create_symlink(ref / f, override_dir / f);
  setenv("UNLLM_TANTIVY_REFERENCE_DIR", override_dir.c_str(), 1);
  mapping();
  J report = object();
  report["binding_fixture"] =
      digest(v5_root() / "fixtures/alternative-providers.json");
  auto q = create(1);
  report["queue"] = q.string();
  auto before = queue_status(q);
  auto first = digest(q / "inventory/checkpoint.json");
  unsigned completed = 0;
  for (const auto &j : before["jobs"])
    completed += j["state"] == "done";
  check(completed == 1, "step bound ignored");
  stop_worker(q, 1, true);
  {
    Database db(q / "queue.sqlite");
    db.script("CREATE TRIGGER simulate_commit_failure BEFORE UPDATE OF state "
              "ON job WHEN NEW.name='materials' AND NEW.state='done' BEGIN "
              "SELECT RAISE(ABORT,'test crash window'); END;");
  }
  negative({tool(), "resume", q}, "test crash window", {.seconds = 240});
  check(fs::exists(q / "materials/checkpoint.json"),
        "atomic output missing at crash window");
  auto orphan = digest(q / "materials/checkpoint.json");
  {
    Database db(q / "queue.sqlite");
    db.script("DROP TRIGGER simulate_commit_failure");
  }
  run({tool(), "resume", q}, {.seconds = 300});
  auto final = queue_status(q);
  check(digest(q / "inventory/checkpoint.json") == first &&
            digest(q / "materials/checkpoint.json") == orphan,
        "re-executed a committed checkpoint");
  {
    Database db(q / "queue.sqlite", true);
    check(
        db.query(
            "SELECT COUNT(*) FROM attempt WHERE outcome='interrupted'")[0][0] ==
            "1",
        "interrupted attempt missing");
    check(db.query("SELECT attempts FROM job WHERE name='materials'")[0][0] ==
              "1",
          "renamed output reran");
    check(db.query("SELECT COUNT(*) FROM job WHERE state!='done'")[0][0] == "0",
          "unfinished queue");
    report["attempt_count"] =
        std::stoi(db.query("SELECT COUNT(*) FROM attempt")[0][0]);
  }
  run({tool(), "resume", q}, {.seconds = 60});
  check(queue_status(q) == final, "completed replay mutated ledger");
  auto hash = final["manifest_sha256"].asString();
  check(verify(q / "stage/package", hash) ==
            array({"number_option_prefix", "reverse_record_bytes",
                   "threshold_entries"}),
        "unexpected distinct qualifications");
  auto pack = q / "stage/package";
  auto pins = load(pack / "cross-check.json");
  check(pins["logical"]["ObservedEvent"] ==
            array({"threshold_entries\thold\tsilent",
                   "threshold_entries\trearm\tsilent",
                   "threshold_entries\ttrigger\temitted",
                   "threshold_entries\ttrigger\tsilent"}),
        "component observations include harness events or omit latch "
        "transitions");
  check(pins["tantivy_binary"] == digest(override_dir / "unllm_index") &&
            pins["query_library"] == digest(override_dir / "libunllm_search.a"),
        "comparator pins differ");
  report["manifest_sha256"] = hash;
  report["completed_jobs"] = final["jobs"].size();
  report["streams"] = final["stream_service"];
  auto wrong = evidence / "different-reference";
  fs::create_directory(wrong);
  fs::create_symlink("/usr/bin/true", wrong / "unllm_index");
  fs::create_symlink(ref / "libunllm_search.a", wrong / "libunllm_search.a");
  negative({tool(), "resume", q}, "environment changed",
           {.environment = {{"UNLLM_TANTIVY_REFERENCE_DIR", wrong}}});
  auto cancelled = create(0);
  run({tool(), "cancel", cancelled});
  negative({tool(), "resume", cancelled}, "cancelled");
  auto cancellation = queue_status(cancelled);
  for (const auto &j : cancellation["jobs"])
    check(j["attempts"] == 0, "cancelled source executed");
  auto denied = create(1);
  save(denied / "inventory/inputs.json", "changed bytes\n");
  negative({tool(), "resume", denied}, "changed checkpoint payload");
  auto exhaust = create(0);
  stop_worker(exhaust, 1, false);
  stop_worker(exhaust, 2, false);
  negative({tool(), "resume", exhaust}, "requires review");
  auto limits = load(v5_root() / "sources.json")["queue_policy"]["limits"];
  limits["reserve_mib"] =
      Json::UInt64(fs::space(evidence).capacity / 1048576 + 100);
  bool refused = false;
  try {
    check_queue_space(evidence, limits);
  } catch (const std::runtime_error &e) {
    refused =
        std::string(e.what()).find("insufficient corpus") != std::string::npos;
  }
  check(refused, "unavailable disk reservation accepted");
  negative({tool(), "resume", "/tmp"}, "outside corpus experiment workspace");
  report["controls"] = array(
      {"step bound", "concurrent lease exclusion", "interrupted worker retry",
       "rename before SQLite commit", "idempotent completed resume",
       "changed tool pins", "cancelled queue", "modified checkpoint",
       "retry ceiling", "disk reservation", "wrong storage root",
       "alternative-provider identity"});
  report["negative_queues"] =
      array({cancelled.string(), denied.string(), exhaust.string()});
  write(evidence / "recovery.json", report);
  std::cout << dump(report);
  return 0;
} catch (const std::exception &e) {
  std::cerr << e.what() << "\n";
  return 1;
}
