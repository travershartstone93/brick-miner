#include "queue.hpp"
#include <algorithm>
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
J semantic_controls(const Path &pack, Database &db) {
  auto sql = "SELECT provider FROM semantic_port WHERE direction='output' AND "
             "role=? AND unit=? AND cardinality=? AND role_status='observed' "
             "AND unit_status='observed' AND cardinality_status='observed'";
  auto match = [&](const Strings &constraints) {
    return db.query(sql, constraints);
  };
  check(match({"record_byte_length", "byte", "per_input_item"}) ==
                std::vector<Strings>{{"x19"}} &&
            match({"sequence_item_count", "unitless", "singleton"}) ==
                std::vector<Strings>{{"x20"}},
        "typed measurement lookup mismatch");
  check(match({"record_byte_length", "unitless", "per_input_item"}).empty() &&
            match({"sequence_item_count", "byte", "singleton"}).empty() &&
            match({"sequence_item_count", "unitless", "per_input_item"})
                .empty() &&
            match({"record_byte_length", "unknown", "per_input_item"}).empty(),
        "crossed semantic constraints passed");
  auto canonical = load(pack / "semantic-ports.json");
  std::vector<Strings> unknown;
  for (const auto &port : canonical)
    if (port["direction"] == "output" && port["unit_status"] == "unknown") {
      check(port["unit"].isNull(), "unknown canonical output carries a unit");
      unknown.push_back(
          {port["provider"].asString(), port["binding"].asString()});
    }
  std::sort(unknown.begin(), unknown.end());
  check(std::find(unknown.begin(), unknown.end(),
                  Strings{"x14", "nonempty_lines"}) != unknown.end() &&
            db.query("SELECT provider,binding FROM semantic_port WHERE "
                     "direction='output' AND unit_status='unknown' AND unit "
                     "IS NULL ORDER BY provider,binding") == unknown &&
            db.query("SELECT provider FROM semantic_port WHERE "
                     "direction='input' AND unit_status='not-applicable' AND "
                     "unit IS NULL ORDER BY provider") ==
                std::vector<Strings>{{"miner_line_xxh32"}, {"x19"}, {"x20"}},
        "unknown, inapplicable and explicit dimensionless were conflated");
  auto contracts = load(pack / "contracts.json"),
       bindings = load(pack / "bundle/providers/contracts.json")["providers"];
  auto reject = [&](J changed) {
    bool rejected = false;
    try {
      check_semantic_ports(pack, changed, contracts, bindings);
    } catch (const std::exception &) {
      rejected = true;
    }
    check(rejected, "invalid semantic view accepted");
  };
  auto changed = canonical;
  changed[0]["contract_hash"] = std::string(64, '0');
  reject(changed);
  changed = canonical;
  changed[0]["unit_status"] = "unknown";
  changed[0]["unit"] = "unitless";
  reject(changed);
  changed = canonical;
  changed.append(canonical[0]);
  reject(changed);
  changed = canonical;
  J ignored;
  changed.removeIndex(0, &ignored);
  reject(changed);
  changed = canonical;
  changed[0]["evidence"] = array({"evidence/standalone.json#missing"});
  reject(changed);
  changed = canonical;
  for (auto &p : changed)
    if (p["provider"] == "x19" && p["direction"] == "output") {
      p["role"] = "sequence_item_count";
      p["unit"] = "unitless";
      p["cardinality"] = "singleton";
    }
  reject(changed);
  auto hash_port =
      std::find_if(canonical.begin(), canonical.end(), [](const auto &p) {
        return p["provider"] == "miner_line_xxh32" &&
               p["direction"] == "output";
      });
  check(hash_port != canonical.end(), "missing checksum output");
  auto wrong_hash = *hash_port;
  wrong_hash["role"] = "record_byte_length";
  wrong_hash["unit_status"] = "observed";
  wrong_hash["unit"] = "byte";
  changed = canonical;
  changed[hash_port.index()] = wrong_hash;
  reject(changed);
  check(
      db.query(
          "SELECT provider FROM semantic_port WHERE direction='output' "
          "AND role='record_xxh32_seed_zero' AND unit_status='not-applicable' "
          "AND unit IS NULL AND cardinality='per_input_item'") ==
          std::vector<Strings>{{"miner_line_xxh32"}},
      "checksum lookup failed");
  return array({"checksum role and inapplicable unit",
                "checksum relabeled as byte length rejected",
                "same-representation exact matches",
                "role/unit/cardinality mismatch rejection",
                "null versus explicit unitless", "stale parent contract",
                "unknown positive identifier", "duplicate port",
                "missing direction", "broken evidence link",
                "valid identifiers contradict the observed callable"});
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
                   "threshold_entries", "byte_lengths", "item_count",
                   "line_xxh32", "sort_lines", "reverse_order", "unique_lines",
                   "adjacent_unique", "nonempty_lines", "u84"}),
        "unexpected distinct qualifications");
  Json::UInt64 released = 0;
  unsigned programs = 0;
  for (const auto &j : final["jobs"]) {
    auto dir = q / j["name"].asString();
    check(!fs::exists(dir / ".test-programs"),
          "test executable scratch was checkpointed");
    if (!fs::exists(dir / "tested-programs.json"))
      continue;
    auto log = load(dir / "tested-programs.json");
    check(log["phase"] == j["name"] &&
              log["config_sha256"] == final["config_sha256"],
          "program release receipt belongs to a different job");
    for (const auto &ref : log["checked_outputs"].getMemberNames())
      check(log["checked_outputs"][ref] == digest(dir / ref),
            "missing durable test verdict");
    for (const auto &parent : log["input_checkpoints"].getMemberNames())
      check(log["input_checkpoints"][parent] ==
                digest(q / parent / "checkpoint.json"),
            "stale test build context");
    programs += log["program_sha256"].size();
    released += log["released_bytes"].asUInt64();
  }
  check(programs > 0 && released > 0 &&
            load(q / "integration/tested-programs.json")
                    ["program_sha256"]["line_xxh32/staged/program"] ==
                load(q / "integration/integration.json")["line_xxh32"]["staged"]
                                                        ["binary_sha256"],
        "missing compiled XXH32 artifact identity");
  report["released_programs"] = programs;
  report["released_bytes"] = released;
  auto pack = q / "stage/package";
  auto pins = load(pack / "cross-check.json");
  check(pins["tantivy"]["oldest"] == "u84" &&
            pins["logical"]["TestedMethod"].size() == 6,
        "class retrieval or type coverage missing");
  Database catalog(pack / "contracts.sqlite", true);
  report["semantic_controls"] = semantic_controls(pack, catalog);
  const auto contract_rows = load(pack / "contracts.json");
  const auto binding_rows =
      load(pack / "bundle/providers/contracts.json")["providers"];
  for (auto c : contract_rows) {
    if (c["capability_id"] != "reverse_order")
      continue;
    const auto wrong = select_binding(binding_rows, "reverse_order", "x10",
                                      "layers", "layers");
    bool rejected = false;
    try {
      check_sequence_evidence(pack, c, wrong);
    } catch (const std::runtime_error &) {
      rejected = true;
    }
    check(rejected,
          "shared templated implementation qualified an untested binding");
    auto right = select_binding(binding_rows, "reverse_order", "x10", "strings",
                                "strings");
    c["claims"]["formal_predicates"]["status"] = "established";
    rejected = false;
    try {
      check_sequence_evidence(pack, c, right);
    } catch (const std::runtime_error &) {
      rejected = true;
    }
    check(rejected, "finite observations promoted to formal proof");
  }
  report["sequence_controls"] =
      array({"layers specialization excluded",
             "finite observations cannot establish a general proof"});
  check(catalog.query(
            "SELECT provider FROM class_binding JOIN property USING(provider) "
            "WHERE name='state_protocol' AND status='observed'") ==
                std::vector<Strings>{{"u84"}} &&
            catalog
                .query("SELECT provider FROM class_binding JOIN property "
                       "USING(provider) WHERE name='concurrency' AND "
                       "status='observed'")
                .empty() &&
            catalog
                .query("SELECT provider FROM class_binding JOIN port "
                       "USING(provider)")
                .empty(),
        "class property or interface-kind isolation failed");
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
  // Replay the last job in a private copy. Simulate a pin changing *after*
  // admission and make sure the prepared generation never becomes a stage.
  auto drift = fresh_run("v5-before-publication");
  for (const auto &entry : fs::directory_iterator(q)) {
    auto name = entry.path().filename();
    if (name != "stage" && name != "attempts")
      fs::copy(entry.path(), drift / name, fs::copy_options::recursive);
  }
  fs::create_directory(drift / "attempts");
  {
    Database db(drift / "queue.sqlite");
    db.script(
        "DELETE FROM attempt WHERE job='stage'; "
        "UPDATE job SET state='ready',attempts=0,receipt='' WHERE "
        "name='stage'; "
        "CREATE TRIGGER simulate_pin_change AFTER UPDATE OF state ON job "
        "WHEN NEW.name='stage' AND NEW.state='running' BEGIN "
        "UPDATE config SET value='changed' WHERE key='config_hash'; END;");
  }
  negative({tool(), "resume", drift},
           "qualification inputs changed before staged publication",
           {.seconds = 240});
  {
    Database db(drift / "queue.sqlite", true);
    check(fs::exists(drift / "attempts/stage.1/.test-programs/tantivy-query") &&
              !fs::exists(drift / "stage") &&
              db.query("SELECT state FROM job WHERE name='stage'")[0][0] ==
                  "failed",
          "unverified generation reached the stage");
  }
  auto cancelled = create(0);
  run({tool(), "cancel", cancelled});
  negative({tool(), "resume", cancelled}, "cancelled");
  auto cancellation = queue_status(cancelled);
  for (const auto &j : cancellation["jobs"])
    check(j["attempts"] == 0, "cancelled source executed");
  auto interrupted = create(0);
  stop_worker(interrupted, 1, false);
  run({tool(), "cancel", interrupted});
  auto stopped = queue_status(interrupted);
  for (const auto &j : stopped["jobs"])
    check(j["state"] == "cancelled",
          "released worker still reported as running after cancel");
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
       "changed tool pins", "changed pins during stage", "cancelled queue",
       "cancel after orphaned worker", "modified checkpoint", "retry ceiling",
       "disk reservation", "wrong storage root",
       "alternative-provider identity"});
  report["negative_queues"] =
      array({drift.string(), cancelled.string(), interrupted.string(),
             denied.string(), exhaust.string()});
  write(evidence / "recovery.json", report);
  std::cout << dump(report);
  return 0;
} catch (const std::exception &e) {
  std::cerr << e.what() << "\n";
  return 1;
}
