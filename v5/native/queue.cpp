#include "queue.hpp"
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <fcntl.h>
#include <fstream>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

namespace v5 {
namespace {
class Lock {
  int fd;

public:
  explicit Lock(const Path &path, bool optional = false)
      : fd(open(path.c_str(), O_CREAT | O_CLOEXEC | O_NOFOLLOW | O_RDWR,
                0600)) {
    check(fd >= 0, "cannot open queue lease");
    if (flock(fd, LOCK_EX | LOCK_NB)) {
      bool busy = errno == EWOULDBLOCK;
      close(fd);
      fd = -1;
      if (optional && busy)
        return;
      throw std::runtime_error(busy ? "another miner holds the worker lease"
                                    : "cannot acquire worker lease");
    }
  }
  ~Lock() {
    if (fd >= 0)
      close(fd);
  }
  bool held() const { return fd >= 0; }
  Lock(const Lock &) = delete;
};
const std::string schema = R"sql(
  PRAGMA journal_mode=DELETE; PRAGMA synchronous=FULL; PRAGMA foreign_keys=ON;
  BEGIN IMMEDIATE;
  CREATE TABLE config(key TEXT PRIMARY KEY,value TEXT NOT NULL);
  CREATE TABLE source(id TEXT PRIMARY KEY, disposition TEXT NOT NULL, reason TEXT NOT NULL);
  CREATE TABLE job(name TEXT PRIMARY KEY, id TEXT UNIQUE NOT NULL,source TEXT,route TEXT,stream TEXT,
    score INTEGER,state TEXT NOT NULL DEFAULT 'ready', attempts INTEGER NOT NULL DEFAULT 0, receipt TEXT NOT NULL DEFAULT '');
  CREATE TABLE dependency(job TEXT REFERENCES job(name),requires TEXT REFERENCES job(name), PRIMARY KEY(job,requires));
  CREATE TABLE attempt(job TEXT REFERENCES job(name),n INTEGER,started INTEGER NOT NULL,finished INTEGER,
    outcome TEXT NOT NULL,detail TEXT NOT NULL DEFAULT '',PRIMARY KEY(job,n));
  CREATE TABLE stream(name TEXT PRIMARY KEY,weight INTEGER NOT NULL,service INTEGER NOT NULL DEFAULT 0);
)sql";
std::string setting(Database &db, const std::string &k) {
  auto v = db.query("SELECT value FROM config WHERE key=?", {k});
  check(v.size() == 1, "missing queue setting " + k);
  return v[0][0];
}
std::uintmax_t file_bytes(const Path &path) {
  std::uintmax_t used = 0;
  for (auto &e : fs::recursive_directory_iterator(path))
    if (e.is_regular_file())
      used += e.file_size();
  return used;
}
void sync_path(const Path &p) {
  int fd = open(p.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
  check(fd >= 0, "cannot sync " + p.string());
  auto rc = fsync(fd);
  close(fd);
  check(rc == 0, "sync failed " + p.string());
}
void sync_tree(const Path &root) {
  std::vector<Path> dirs = {root};
  for (const auto &e : fs::recursive_directory_iterator(root)) {
    check(!e.is_symlink(), "symbolic link in job result");
    if (e.is_directory())
      dirs.push_back(e.path());
    else {
      check(e.is_regular_file(), "non-file in job result");
      sync_path(e.path());
    }
  }
  for (auto i = dirs.rbegin(); i != dirs.rend(); ++i)
    sync_path(*i);
}
// Test programs are created only inside this attempt's reserved directory.
void release_programs(const Path &attempt, const Job &job,
                      const std::string &config, const Path &work) {
  auto scratch = attempt / ".test-programs";
  if (!fs::exists(scratch))
    return;
  check(fs::is_directory(scratch) && !fs::is_symlink(scratch),
        "unexpected program scratch path");
  auto hashes = tree_hashes(scratch);
  check(!hashes.empty(), "missing disposable program");
  for (const auto &name : hashes.getMemberNames()) {
    std::ifstream program(scratch / name, std::ios::binary);
    char magic[4]{};
    check(program.read(magic, 4) && std::string_view(magic, 4) == "\177ELF",
          "non-program in qualification scratch");
  }
  J inputs = object(), verdicts = object();
  for (const auto &parent : job.parents)
    inputs[parent] = digest(work / parent / "checkpoint.json");
  for (const auto *file : {"standalone.json", "lifecycle.json",
                           "integration.json", "fifo/fifo-integration.json",
                           "package/cross-check.json", "package/manifest.json"})
    if (fs::is_regular_file(attempt / file))
      verdicts[file] = digest(attempt / file);
  check(!verdicts.empty(), "missing qualification verdicts");
  write(attempt / "tested-programs.json",
        object({{"schema", "unllm.tested-programs/1"},
                {"phase", job.name},
                {"config_sha256", config},
                {"input_checkpoints", inputs},
                {"checked_outputs", verdicts},
                {"program_sha256", hashes},
                {"released_bytes", Json::UInt64(file_bytes(scratch))}}));
  sync_tree(attempt);
  fs::remove_all(scratch);
  sync_path(attempt);
}
void validate_location(const Path &work) {
  check_corpus(work);
  check(!fs::is_symlink(work) && fs::is_directory(work) &&
            fs::is_regular_file(work / "queue.sqlite") &&
            !fs::is_symlink(work / "queue.sqlite"),
        "expected an initialized acquisition directory");
}
std::string check_artifact(const Path &path, const Strings &row) {
  auto c = load(path / "checkpoint.json");
  check(c["job_id"] == row[1] && c["attempt"].asInt() == std::stoi(row[3]),
        "checkpoint ownership mismatch");
  auto hashes = tree_hashes(path);
  hashes.removeMember("checkpoint.json");
  check(sha(dump(hashes)) == c["tree_sha256"].asString(),
        "changed checkpoint payload " + path.string());
  auto hash = digest(path / "checkpoint.json");
  if (row[2] == "done")
    check(hash == row[4], "checkpoint receipt changed");
  return hash;
}
void finish(Database &db, const Strings &row, const std::string &hash) {
  db.script("BEGIN IMMEDIATE");
  db.insert(
      "UPDATE job SET state='done',receipt=? WHERE name=? AND state='running'",
      {hash, row[0]});
  db.insert(
      "UPDATE attempt SET finished=unixepoch(),outcome='committed',detail=? "
      "WHERE job=? AND n=?",
      {hash, row[0], row[3]});
  db.insert("UPDATE stream SET service=service+1 WHERE name=(SELECT stream "
            "FROM job WHERE name=?)",
            {row[0]});
  db.script("COMMIT");
}
void failed(Database &db, const Strings &row, const std::string &state,
            const std::string &outcome, const std::string &detail) {
  db.script("BEGIN IMMEDIATE");
  db.insert("UPDATE job SET state=? WHERE name=?", {state, row[0]});
  db.insert("UPDATE attempt SET finished=unixepoch(),outcome=?,detail=? WHERE "
            "job=? AND n=?",
            {outcome, detail, row[0], row[3]});
  db.script("COMMIT");
}
void recover(Database &db, const Path &work, unsigned limit) {
  for (auto row : db.query(
           "SELECT name,id,state,attempts,receipt FROM job ORDER BY name")) {
    auto path = work / row[0];
    if (row[2] == "done") {
      check_artifact(path, row);
      continue;
    }
    if (row[2] != "running") {
      check(!fs::exists(path), "unclaimed job output " + row[0]);
      continue;
    }
    if (fs::exists(path)) {
      finish(db, row, check_artifact(path, row));
      continue;
    }
    bool retry = unsigned(std::stoul(row[3])) < limit;
    failed(db, row, retry ? "ready" : "failed", "interrupted",
           "worker lease released before atomic artifact commit");
  }
}
J registry_config() {
  auto r = load(v5_root() / "sources.json");
  check(r["schema"] == "unllm.source-registry/1" &&
            r["queue_policy"]["version"] == "unllm.pilot-queue/1",
        "unknown queue or registry version");
  auto &l = r["queue_policy"]["limits"];
  check(l["workers"] == 1 && l["download_bytes"] == 0,
        "unsupported execution budget");
  for (const auto *k : {"wall_seconds_per_step", "cpu_seconds_per_process",
                        "address_space_mib_per_process", "disk_mib_per_run",
                        "reserve_mib", "max_attempts"})
    check(l[k].isUInt() && l[k].asUInt() > 0,
          "invalid queue limit " + std::string(k));
  return r;
}
} // namespace
void check_queue_space(const Path &work, const J &limits) {
  check_corpus(work);
  struct stat expected{}, actual{};
  check(stat(volume.c_str(), &expected) == 0 &&
            stat(work.c_str(), &actual) == 0 &&
            expected.st_dev == actual.st_dev,
        "queue must reside on the configured corpus device");
  auto budget = limits["disk_mib_per_run"].asUInt64() * 1048576,
       reserve = limits["reserve_mib"].asUInt64() * 1048576;
  auto used = file_bytes(work);
  check(used < budget && fs::space(work).available >= reserve + budget - used,
        "insufficient corpus capacity or run disk budget");
}
void initialize_queue(const Path &work) {
  check_corpus(work);
  auto registry = registry_config();
  auto jobs = pilot_jobs(registry);
  auto pins = environment_pins();
  auto identity = sha(dump(pins));
  auto limits = registry["queue_policy"]["limits"];
  check_queue_space(work, limits);
  fs::create_directory(work / "attempts");
  {
    Database db(work / ".initializing.sqlite");
    db.script(schema);
    for (auto &[key, value] : std::map<std::string, std::string>{
             {"schema", "unllm.acquisition-ledger/1"},
             {"environment", dump(pins)},
             {"config_hash", identity},
             {"limits", dump(limits)},
             {"cancelled", "0"}})
      db.insert("INSERT INTO config VALUES(?,?)", {key, value});
    for (const auto &s : registry["sources"]) {
      auto ok = s["approval"] == "local-pilot" && s.isMember("jobs");
      db.insert(
          "INSERT INTO source VALUES(?,?,?)",
          {s["id"].asString(), ok ? "approved" : "pending approval",
           s.get(ok ? "approval_reference" : "reason", "requires source review")
               .asString()});
    }
    auto weights = registry["queue_policy"]["stream_weights"];
    for (auto &name : weights.getMemberNames())
      db.insert("INSERT INTO stream(name,weight) VALUES(?,?)",
                {name, weights[name].asString()});
    for (auto &j : jobs) {
      auto id = sha(dump(array({identity, j.source, j.name, j.route})));
      db.insert(
          "INSERT INTO job(name,id,source,route,stream,score) "
          "VALUES(?,?,?,?,?,?)",
          {j.name, id, j.source, j.route, j.stream, std::to_string(j.score)});
      for (const auto &p : j.parents)
        db.insert("INSERT INTO dependency VALUES(?,?)", {j.name, p});
    }
    db.script("COMMIT");
  }
  fs::rename(work / ".initializing.sqlite", work / "queue.sqlite");
  sync_path(work);
}
void advance(const Path &work, unsigned max_steps) {
  validate_location(work);
  Lock lease(runs / ".v5-worker.lock");
  Database db(work / "queue.sqlite");
  db.script("PRAGMA busy_timeout=5000; PRAGMA synchronous=FULL;");
  check(setting(db, "schema") == "unllm.acquisition-ledger/1",
        "unknown ledger schema");
  check(setting(db, "cancelled") == "0", "queue cancelled");
  auto pins = environment_pins();
  check(sha(dump(pins)) == setting(db, "config_hash"),
        "inputs or executable environment changed; use a new pilot run");
  auto registry = registry_config(),
       limits = registry["queue_policy"]["limits"];
  auto jobs = pilot_jobs(registry);
  recover(db, work, limits["max_attempts"].asUInt());
  for (unsigned i = 0; i < max_steps; ++i) {
    check(setting(db, "cancelled") == "0", "queue cancelled at checkpoint");
    auto blocked = db.query(
        "SELECT name FROM job WHERE state='failed' ORDER BY name LIMIT 1");
    check(blocked.empty(),
          blocked.empty() ? ""
                          : "qualification requires review: " + blocked[0][0]);
    auto next = db.query(
        R"sql(SELECT j.name,j.id,j.state,j.attempts,j.receipt FROM job j JOIN stream s ON s.name=j.stream
       WHERE j.state='ready' AND NOT EXISTS (SELECT 1 FROM dependency d JOIN job p ON d.requires=p.name WHERE d.job=j.name AND p.state!='done')
       ORDER BY CAST(s.service AS REAL)/s.weight, j.score DESC, j.name LIMIT 1)sql");
    if (next.empty()) {
      check(db.query("SELECT name FROM job WHERE state!='done'").empty(),
            "blocked prerequisites");
      return;
    }
    check_queue_space(work, limits);
    auto row = next[0];
    auto selected = std::find_if(jobs.begin(), jobs.end(), [&](const Job &j) {
      return j.name == row[0];
    });
    check(selected != jobs.end(), "unrecognized queue job");
    unsigned n = 1 + std::stoul(row[3]);
    check(n <= limits["max_attempts"].asUInt(), "retry budget exhausted");
    row[3] = std::to_string(n);
    db.script("BEGIN IMMEDIATE");
    if (setting(db, "cancelled") != "0") {
      db.script("ROLLBACK");
      throw std::runtime_error("queue cancelled");
    }
    db.insert("UPDATE job SET state='running',attempts=? WHERE name=?",
              {row[3], row[0]});
    db.insert("INSERT INTO attempt(job,n,started,outcome) "
              "VALUES(?,?,unixepoch(),'running')",
              {row[0], row[3]});
    db.script("COMMIT");
    auto tmp = work / "attempts" / (row[0] + "." + row[3]);
    check(fs::create_directory(tmp), "attempt directory collision");
    Result p;
    try {
      p = launch(*selected, work, tmp, limits);
      if (p.status == 0 && selected->name == "stage")
        check(sha(dump(environment_pins())) == setting(db, "config_hash"),
              "qualification inputs changed before staged publication");
    } catch (const std::exception &error) {
      failed(db, row, "failed", "worker setup or publication check",
             error.what());
      throw;
    }
    if (p.status != 0) {
      std::string detail = (p.err + p.out).substr(0, 8192);
      bool transient = p.status == -1;
      failed(db, row,
             transient && n < limits["max_attempts"].asUInt() ? "ready"
                                                              : "failed",
             transient ? "timeout or signal" : "qualification failure", detail);
      throw std::runtime_error(row[0] + " failed (" + std::to_string(p.status) +
                               "): " + detail);
    }
    check(file_bytes(work) <= limits["disk_mib_per_run"].asUInt64() * 1048576,
          "job exceeded corpus disk budget");
    release_programs(tmp, *selected, setting(db, "config_hash"), work);
    auto hashes = tree_hashes(tmp);
    auto receipt = object({{"schema", "unllm.acquisition-checkpoint/1"},
                           {"job_id", row[1]},
                           {"attempt", n},
                           {"source", selected->source},
                           {"route", selected->route},
                           {"phase", selected->name},
                           {"config_sha256", setting(db, "config_hash")},
                           {"tree_sha256", sha(dump(hashes))},
                           {"payload_bytes", Json::UInt64(file_bytes(tmp))},
                           {"resources", load(tmp / "resource-use.json")}});
    if (row[0] == "stage")
      receipt["manifest_sha256"] = digest(tmp / "package/manifest.json");
    write(tmp / "checkpoint.json", receipt);
    sync_tree(tmp);
    fs::rename(tmp, work / row[0]);
    sync_path(work);
    sync_path(work / "attempts");
    finish(db, row, digest(work / row[0] / "checkpoint.json"));
  }
}
J queue_status(const Path &work) {
  validate_location(work);
  Database db(work / "queue.sqlite", true);
  db.script("PRAGMA busy_timeout=5000; BEGIN");
  auto status = object({{"schema", "unllm.acquisition-status/1"},
                        {"config_sha256", setting(db, "config_hash")},
                        {"cancelled", setting(db, "cancelled") == "1"},
                        {"sources", object()},
                        {"jobs", array()},
                        {"stream_service", object()}});
  for (const auto &row :
       db.query("SELECT disposition,COUNT(*) FROM source GROUP BY disposition"))
    status["sources"][row[0]] = std::stoi(row[1]);
  for (const auto &row :
       db.query("SELECT name,service FROM stream ORDER BY name"))
    status["stream_service"][row[0]] = std::stoi(row[1]);
  for (const auto &r :
       db.query("SELECT name,id,source,stream,state,attempts,receipt FROM job "
                "ORDER BY rowid")) {
    status["jobs"].append(object({{"name", r[0]},
                                  {"id", r[1]},
                                  {"source", r[2]},
                                  {"stream", r[3]},
                                  {"state", r[4]},
                                  {"attempts", std::stoi(r[5])},
                                  {"receipt_sha256", r[6]}}));
    if (r[0] == "stage" && r[4] == "done")
      status["manifest_sha256"] =
          load(work / "stage/checkpoint.json")["manifest_sha256"];
  }
  db.script("COMMIT");
  return status;
}
void cancel_queue(const Path &work) {
  validate_location(work);
  Database db(work / "queue.sqlite");
  db.script("PRAGMA busy_timeout=5000; BEGIN IMMEDIATE");
  check(!db.query("SELECT name FROM job WHERE state!='done'").empty(),
        "queue is complete");
  db.insert("UPDATE config SET value='1' WHERE key='cancelled'", {});
  db.script("UPDATE job SET state='cancelled' WHERE state='ready'");
  db.script("COMMIT");
  Lock lease(runs / ".v5-worker.lock", true);
  if (!lease.held())
    return;
  // A dead controller may have left a running attempt, including when a
  // changed executable prevents normal resume. Preserve any committed output.
  for (const auto &row : db.query("SELECT name,id,state,attempts,receipt FROM "
                                  "job WHERE state='running'")) {
    auto path = work / row[0];
    if (fs::exists(path))
      finish(db, row, check_artifact(path, row));
    else
      failed(db, row, "cancelled", "interrupted",
             "cancelled after the worker lease ended");
  }
}
} // namespace v5
