#include "queue.hpp"
#include <QProcess>
#include <algorithm>
#include <chrono>
#include <sched.h>
#include <sys/resource.h>

namespace v5 {
namespace {
Path integrated() { return main_repo / "experiments/boilerplate/integrated"; }
std::vector<Path> mime_inputs() {
  std::vector<Path> files = {
      main_repo /
      "experiments/boilerplate/component-trial/expanded/mime-defaults.txt"};
  for (const auto *path :
       {"/etc/mime.types", "/etc/httpd/mime.types",
        "/etc/httpd/conf/mime.types", "/etc/apache/mime.types",
        "/etc/apache2/mime.types", "/usr/local/etc/httpd/conf/mime.types",
        "/usr/local/lib/netscape/mime.types", "/usr/local/etc/mime.types"})
    if (fs::is_regular_file(path))
      files.emplace_back(path);
  return files;
}
const Strings stage_files = {"provider-stage.sh",     "effective-contracts.sh",
                             "stage-headers.sh",      "public-contracts.jq",
                             "row-outcomes.json",     "row-sources.json",
                             "population-handoff.jq", "cli-guarantees.json"};
const Strings native_tools = {"brick-provider-plan", "brick-index-formats",
                              "unllm_logic_worker", "libunllm_native_logic.so"};
void materialize(const Path &work, const Path &out) {
  auto observations = object();
  for (const auto &job : pilot_jobs(load(v5_root() / "sources.json"))) {
    if (!job.name.starts_with("extract-"))
      continue;
    auto extracted = work / job.name;
    for (const auto &e : fs::directory_iterator(extracted)) {
      const auto name = e.path().filename();
      if (name == "notices")
        fs::copy(e.path(), out / "extract/notices",
                 fs::copy_options::recursive | fs::copy_options::skip_existing);
      else if (e.path().extension() == ".hpp" ||
               (e.path().extension() == ".json" && name != "checkpoint.json" &&
                name != "resource-use.json"))
        fs::copy_file(e.path(), out / "extract" / name);
    }
    auto tested = work / ("test-" + job.name.substr(8));
    if (fs::exists(tested / "lifecycle.json")) {
      fs::create_directories(out / "standalone");
      fs::copy_file(tested / "lifecycle.json",
                    out / "standalone/lifecycle.json");
    }
    auto values = load(tested / "standalone.json");
    for (const auto &k : values.getMemberNames()) {
      check(!observations.isMember(k), "duplicate evidence");
      observations[k] = values[k];
    }
  }
  write(out / "standalone/standalone.json", observations);
}
double secs(timeval t) {
  return double(t.tv_sec) + double(t.tv_usec) / 1000000;
}
} // namespace
std::vector<Job> pilot_jobs(const J &registry) {
  std::vector<Job> jobs = {
      {"inventory", "registry", "", "foundational", 0, {}}};
  Strings required;
  const std::map<std::string, std::pair<std::string, std::string>> reviewed = {
      {"std-reverse", {"gcc-libstdcxx", "api"}},
      {"lzip-number-prefix", {"gnu-lzip", "direct"}},
      {"lzip-index-reverse", {"gnu-lzip", "composition"}},
      {"retained-keyed-latch", {"retained-keyed-latch", "direct"}}};
  std::set<std::string> selected;
  for (const auto &source : registry["sources"]) {
    if (!source.isMember("jobs"))
      continue;
    check(source["approval"] == "local-pilot" &&
              !source["approval_reference"].asString().empty() &&
              !source["rights"]["scope"].asString().empty() &&
              !source["revision"].asString().empty() &&
              source["sha256"].asString().size() == 64,
          "unapproved or unpinned job source");
    auto id = source["id"].asString();
    for (const auto &config : source["jobs"]) {
      auto selector = config["selector"].asString(),
           route = config["route"].asString(),
           stream = config["stream"].asString();
      check(reviewed.contains(selector) &&
                reviewed.at(selector) == std::make_pair(id, route) &&
                selected.insert(selector).second,
            "unreviewed or duplicate source route");
      check(registry["queue_policy"]["stream_weights"][stream].isUInt() &&
                registry["queue_policy"]["stream_weights"][stream].asUInt() > 0,
            "unknown stream");
      check(stream != "demand-driven",
            "a reviewed missing-capability receipt is required before "
            "scheduling a demand pilot");
      auto cost = config["cost_units"].asUInt();
      check(cost > 0, "missing qualification estimate");
      auto score =
          100 * (config["coverage_gain"].asUInt() + config["reuse"].asUInt()) /
          cost;
      auto unit = selector == "retained-keyed-latch" ? "latch" : route;
      jobs.push_back(
          {"extract-" + unit, id, route, stream, score, {"inventory"}});
      jobs.push_back(
          {"test-" + unit, id, route, stream, score, {"extract-" + unit}});
      required.push_back("test-" + unit);
    }
  }
  check(selected.size() == reviewed.size(),
        "staged pilot requires all reviewed selectors");
  jobs.push_back({"materials", "registry", "", "foundational", 0, required});
  jobs.push_back(
      {"integration", "registry", "", "foundational", 0, {"materials"}});
  jobs.push_back({"stage", "registry", "", "foundational", 0, {"integration"}});
  return jobs;
}
J environment_pins() {
  auto p = object();
  const auto program = fs::canonical("/proc/self/exe");
  p["worker"] = digest(program);
  p["registry"] = digest(v5_root() / "sources.json");
  auto reviewed = sources();
  for (const auto &s : reviewed) {
    if (s["approval"] == "local-pilot") {
      Path arc = s["archive"].asString();
      check(digest(arc) == s["sha256"].asString(),
            "changed source archive " + arc.string());
      if (s["method"] == "signed-local-package")
        p["signature"] = digest(arc.string() + ".sig");
      for (const auto &f : s["files"].getMemberNames())
        check(digest(f) == s["files"][f].asString(),
              "changed pinned input " + f);
    } else if (s.isMember("local_snapshot"))
      check(digest(s["local_snapshot"].asString()) == s["sha256"].asString(),
            "changed discovery snapshot");
  }
  p["source_tree"] = sha(dump(tree_hashes(sample_application() / "source")));
  for (const auto &f : mime_inputs())
    p["mime_inputs"][f.string()] = digest(f);
  p["baseline"] = digest(verified_base() / "SHA256SUMS");
  auto dir = tantivy_reference();
  for (auto *name : {"unllm_index", "libunllm_search.a"})
    p["comparator"][name] = digest(dir / name);
  for (const auto &f : stage_files)
    p["planner"][f] = digest(integrated() / f);
  for (const auto &f : native_tools)
    p["planner"][f] = digest(main_repo / "build/native" / f);
  for (const auto &path : std::vector<Path>{
           root / ".build/brick-scan", root / "one-brick/check.cpp",
           v5_root() / "contract.schema.json", v5_root() / "vocabulary.json",
           v5_root() / "facts.dl", main_repo / ".deps/bin/souffle",
           main_repo / "docs/UNLLM_MASTER_PLAN_V5.md",
           main_repo / ".deps/native/usr/bin/souffle",
           main_repo / "reasoning/prolog/rules.pl",
           sample_application() / "build/compile_commands.json",
           sample_application() / "provenance.json", "/usr/bin/g++-15",
           "/usr/bin/c++", "/usr/bin/clang++", "/usr/bin/bwrap", "/usr/bin/ld"})
    p["files"][path.string()] = digest(path);
  p["fixtures"] = tree_hashes(v5_root() / "fixtures");
  auto backend = run({"g++-15", "-print-prog-name=cc1plus"});
  backend.erase(backend.find_last_not_of("\r\n") + 1);
  p["compiler_backend"] = digest(backend);
  // The local package database records the environment shared by the compiler
  // and its helpers.
  Strings packages = command_args(object({{"command", run({"pacman", "-Q"})}}));
  p["installed_packages"] = sha(join(packages, "\n"));
  p["architecture"] = run({"uname", "-m"});
  return p;
}
J worker_step(const std::string &name, const Path &work, const Path &out) {
  const auto start = std::chrono::steady_clock::now();
  if (name == "inventory")
    inventory(out);
  else if (name == "extract-latch")
    extract_latch(out);
  else if (name == "test-latch")
    qualify_latch(work / "extract-latch", out);
  else if (name.starts_with("extract-"))
    extract(out, name.substr(8));
  else if (name.starts_with("test-"))
    standalone(work / ("extract-" + name.substr(5)), out, name.substr(5));
  else if (name == "materials") {
    fs::create_directories(out / "extract");
    materialize(work, out);
  } else if (name == "integration")
    integrate(work / "materials/extract", out);
  else if (name == "stage")
    assemble(work, out);
  else
    throw std::runtime_error("unrecognized qualification step");
  rusage self{}, children{};
  check(getrusage(RUSAGE_SELF, &self) == 0 &&
            getrusage(RUSAGE_CHILDREN, &children) == 0,
        "missing process accounting");
  return object(
      {{"wall_seconds",
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start)
            .count()},
       {"cpu_seconds", secs(self.ru_utime) + secs(self.ru_stime) +
                           secs(children.ru_utime) + secs(children.ru_stime)},
       {"largest_process_rss_kib",
        Json::Int64(std::max(self.ru_maxrss, children.ru_maxrss))},
       {"download_bytes", 0}});
}
Result launch(const Job &job, const Path &work, const Path &out,
              const J &limits) {
  cpu_set_t mask;
  CPU_ZERO(&mask);
  check(sched_getaffinity(0, sizeof(mask), &mask) == 0,
        "cannot determine CPU allocation");
  int cpu = 0;
  while (cpu < CPU_SETSIZE && !CPU_ISSET(cpu, &mask))
    ++cpu;
  check(cpu < CPU_SETSIZE, "empty CPU allocation");
  Strings cmd = {
      "taskset",
      "-c",
      std::to_string(cpu),
      "prlimit",
      "--core=0",
      "--as=" +
          std::to_string(limits["address_space_mib_per_process"].asUInt64() *
                         1048576),
      "--cpu=" + limits["cpu_seconds_per_process"].asString(),
      "--fsize=" +
          std::to_string(limits["disk_mib_per_run"].asUInt64() * 1048576),
      "--",
      "bwrap",
      "--die-with-parent",
      "--unshare-all",
      "--new-session",
      "--clearenv",
      "--setenv",
      "PATH",
      "/usr/bin",
      "--setenv",
      "LC_ALL",
      "C",
      "--setenv",
      "HOME",
      "/tmp",
      "--setenv",
      "UNLLM_TANTIVY_REFERENCE_DIR",
      "/opt/brick-reference",
      "--ro-bind",
      "/usr",
      "/usr",
      "--symlink",
      "usr/bin",
      "/bin",
      "--symlink",
      "usr/lib",
      "/lib",
      "--symlink",
      "usr/lib",
      "/lib64",
      "--dev",
      "/dev",
      "--proc",
      "/proc",
      "--tmpfs",
      "/tmp"};
  auto ro = [&](const Path &a) { append(cmd, {"--ro-bind", a, a}); };
  for (const auto &f : std::vector<Path>{
           "/etc/ld.so.cache", root / "v5", root / "src",
           main_repo / "docs/UNLLM_MASTER_PLAN_V5.md",
           root / "one-brick/check.cpp", root / ".build/brick-scan",
           sample_application() / "source", sample_application() / "build",
           sample_application() / "provenance.json", main_repo / ".deps",
           main_repo / "reasoning", integrated()})
    ro(f);
  for (auto &f : native_tools)
    ro(main_repo / "build/native" / f);
  for (const auto &f : mime_inputs())
    ro(f);
  auto base = fs::canonical(volume / "unllm/boilerplate/active");
  ro(base);
  append(cmd, {"--symlink", base, volume / "unllm/boilerplate/active"});
  auto registry = sources();
  for (auto &s : registry) {
    if (s["approval"] == "local-pilot") {
      Path arc = s["archive"].asString();
      ro(arc);
      if (s["method"] == "signed-local-package")
        ro(arc.string() + ".sig");
    }
    if (s.isMember("local_snapshot"))
      ro(s["local_snapshot"].asString());
  }
  for (auto *name : {"unllm_index", "libunllm_search.a"})
    append(cmd, {"--ro-bind", tantivy_reference() / name,
                 Path("/opt/brick-reference") / name});
  // Only dependencies committed by the parent enter the worker namespace.
  for (const auto &stage : pilot_jobs(load(v5_root() / "sources.json")))
    if (stage.name != job.name && fs::exists(work / stage.name))
      ro(work / stage.name);
  append(cmd, {"--ro-bind", fs::canonical("/proc/self/exe"),
               "/opt/brick-worker", "--bind", out, out, "--chdir", out,
               "/opt/brick-worker", "--worker", job.name, work, out});
  QProcess child;
  QStringList args;
  for (size_t i = 1; i < cmd.size(); ++i)
    args.append(QString::fromStdString(cmd[i]));
  child.start(QString::fromStdString(cmd.front()), args);
  check(child.waitForStarted(), "cannot start isolated qualification worker");
  const auto deadline =
      std::chrono::steady_clock::now() +
      std::chrono::seconds(limits["wall_seconds_per_step"].asUInt());
  std::uintmax_t peak = 0;
  while (!child.waitForFinished(250)) {
    std::error_code ec;
    std::uintmax_t size = 0;
    for (fs::recursive_directory_iterator it(work, ec), end; !ec && it != end;
         it.increment(ec)) {
      if (it->is_regular_file(ec)) {
        auto n = it->file_size(ec);
        if (!ec)
          size += n;
      }
    }
    // A compiler may rename a transient output while it is being sampled.
    if (ec && ec != std::errc::no_such_file_or_directory) {
      child.kill();
      child.waitForFinished();
      throw std::runtime_error("cannot monitor corpus storage: " +
                               ec.message());
    }
    peak = std::max(peak, size);
    bool expired = std::chrono::steady_clock::now() >= deadline;
    if (expired || size > limits["disk_mib_per_run"].asUInt64() * 1048576 ||
        fs::space(work).available <
            limits["reserve_mib"].asUInt64() * 1048576) {
      child.kill();
      child.waitForFinished();
      return {expired ? -1 : 125, "",
              expired ? "wall-time budget exhausted"
                      : "corpus storage budget exhausted"};
    }
  }
  int code = child.exitStatus() == QProcess::NormalExit ? child.exitCode() : -1;
  if (!code) {
    auto resources = load(out / "resource-use.json");
    resources["sampled_run_disk_bytes"] = Json::UInt64(peak);
    write(out / "resource-use.json", resources);
  }
  return {code, child.readAllStandardOutput().toStdString(),
          child.readAllStandardError().toStdString()};
}
} // namespace v5
