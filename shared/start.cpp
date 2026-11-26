#include "build.hpp"
#include "duckdb.hpp"
#include <algorithm>
#include <future>
#include <iostream>

namespace shared {
void qualify_batch(const Path &base, const std::string &batch);
}
using namespace shared;
int main(int argc, char **argv) try {
  const std::string first_hash =
      "79dcd99668d0cec01d192d81e01ae1c7bb18222a4581013c937c5eea4ea07a91";
  if (argc > 1 && std::string(argv[1]) == "--worker") {
    check(argc == 7 && fs::current_path() == "/tmp/brick-trial",
          "invalid worker invocation");
    auto from = std::stoul(argv[2]), through = std::stoul(argv[3]);
    Path cache = argv[4], base = argv[5];
    auto hash = argv[6];
    check(sha(bytes(first_batch() / "SHA256SUMS")) == first_hash,
          "changed starting release");
    check(sha(bytes(base / "SHA256SUMS")) == hash,
          "changed predecessor manifest");
    for (const auto &path : std::set<Path>{first_batch(), base})
      run({"sha256sum", "--check", "--strict", "--quiet", "SHA256SUMS"},
          {.cwd = path});
    fs::copy(base / "runtime/duckdb", "runtime",
             fs::copy_options::recursive |
                 fs::copy_options::overwrite_existing);
    setenv("PKG_CONFIG_PATH", "/usr/local/lib/pkgconfig", 1);
    duckdb_miner::acquire(fs::current_path() / "sources", cache);
    for (size_t i = from; i <= through; ++i) {
      auto &batch = batches.at(i);
      qualify_batch(base, batch);
      base = fs::current_path() / batch / "bundle";
    }
    return 0;
  }
  Path cache = runs / "duckdb-input-GHUYik9e", resume;
  std::string after, through = "decisions";
  for (int i = 1; i < argc; i += 2) {
    std::string key = argv[i];
    if (key == "--help") {
      std::cout << "shared [--cache DIRECTORY] [--through BATCH] [--resume "
                   "PAIR --after BATCH]\n";
      return 0;
    }
    check(i + 1 < argc, "missing option value");
    std::string val = argv[i + 1];
    if (key == "--cache")
      cache = fs::canonical(val);
    else if (key == "--resume")
      resume = fs::canonical(val);
    else if (key == "--through")
      through = val;
    else if (key == "--after")
      after = val;
    else
      throw std::runtime_error("unknown option " + key);
  }
  auto index = [](const std::string &batch) -> size_t {
    auto it = std::find(batches.begin(), batches.end(), batch);
    check(it != batches.end(), "unknown batch " + batch);
    return it - batches.begin();
  };
  check(after.empty() == resume.empty(), "supply both --resume and --after");
  size_t first = after.empty() ? 0 : index(after) + 1, last = index(through);
  check(first <= last, "empty stage range");
  auto results = object();
  if (!resume.empty()) {
    auto prior = fs::exists(resume / "reproduction.json")
                     ? load(resume / "reproduction.json")
                     : object();
    for (size_t i = 0; i < first; ++i) {
      auto &name = batches[i];
      auto p = resume / "run-1" / name / "bundle/SHA256SUMS";
      if (i + 1 < first && !fs::exists(p)) {
        check(prior[name].isString(), "missing predecessor hash");
        results[name] = prior[name];
        continue;
      }
      auto a = bytes(p);
      check(a == bytes(resume / "run-2" / name / "bundle/SHA256SUMS"),
            "predecessor repeats differ");
      results[name] = sha(a);
      if (prior.isMember(name))
        check(results[name] == prior[name], "predecessor receipt mismatch");
    }
  }
  auto pair = fresh_run("shared-needs");
  std::cout << pair << std::endl;
  auto snapshot = pair / "miner-inputs";
  fs::copy(here(), snapshot, fs::copy_options::recursive);
  const auto worker = fs::canonical("/proc/self/exe");
  std::vector<std::future<std::string>> jobs;
  for (int i : {1, 2}) {
    auto run_name = "run-" + std::to_string(i);
    auto d = pair / run_name;
    fs::create_directories(d / "runtime/lib");
    fs::create_directories(d / "runtime/include");
    auto base = first ? resume / run_name / after / "bundle" : first_batch();
    auto hash = first ? results[after].asString() : first_hash;
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
                   "--ro-bind",
                   snapshot,
                   here(),
                   "--ro-bind",
                   d / "runtime/lib",
                   "/usr/local/lib",
                   "--ro-bind",
                   d / "runtime/include",
                   "/usr/local/include",
                   "--tmpfs",
                   "/tmp",
                   "--bind",
                   d,
                   "/tmp/brick-trial",
                   "--chdir",
                   "/tmp/brick-trial",
                   "--setenv",
                   "LC_ALL",
                   "C",
                   worker,
                   "--worker",
                   std::to_string(first),
                   std::to_string(last),
                   cache,
                   base,
                   hash};
    jobs.push_back(std::async(std::launch::async,
                              [cmd] { return run(cmd, {.seconds = 7200}); }));
  }
  for (auto &job : jobs)
    job.get();
  for (size_t i = first; i <= last; ++i) {
    auto name = batches[i];
    auto a = bytes(pair / "run-1" / name / "bundle/SHA256SUMS");
    check(a == bytes(pair / "run-2" / name / "bundle/SHA256SUMS"),
          "clean repeats differ for " + name);
    results[name] = sha(a);
  }
  if (first)
    write(pair / "predecessor.json",
          object({{"pair", resume.string()}, {"last", after}}));
  write(pair / "reproduction.json", results);
  if (through == batches.back())
    run({root / ".build/shared-review", pair, "--build-snapshot", snapshot},
        {.seconds = 7200});
  std::cout << (pair / "run-1" / through / "bundle") << std::endl;
  return 0;
} catch (const std::exception &e) {
  std::cerr << e.what() << '\n';
  return 1;
}
