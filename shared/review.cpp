#include "qualification.hpp"
#include <future>
#include <iostream>
using namespace shared;
int main(int argc, char **argv) try {
  if (argc > 1 && std::string(argv[1]) == "--help") {
    std::cout << "shared review PAIR [--through BATCH] [--arm "
                 "all|baseline|transfer|compare]\n";
    return 0;
  }
  check(argc >= 2, "expected verified run pair");
  std::string first = argv[1];
  if (first == "--worker") {
    check(argc == 5 || argc == 6, "invalid internal review invocation");
    std::string arm = argv[2];
    Path bundle = argv[4];
    if (arm == "compare")
      compare(argv[3], bundle, fs::current_path());
    else {
      check(arm == "transfer" || arm == "baseline", "unknown review arm");
      bool baseline = arm == "baseline";
      auto selected = baseline ? first_batch() : bundle;
      setenv("PKG_CONFIG_PATH",
             (selected / "runtime/duckdb/lib/pkgconfig").c_str(), 1);
      run_requests(selected, fs::current_path(), baseline,
                   argc == 6 ? argv[5] : "");
    }
    return 0;
  }
  auto pair = fs::canonical(first);
  std::string through = "decisions", arm = "all", id;
  Path snapshot_source = here();
  bool record = false;
  for (int i = 2; i < argc; i += 2) {
    check(i + 1 < argc, "missing review option");
    std::string key = argv[i], val = argv[i + 1];
    if (key == "--through")
      through = val;
    else if (key == "--arm")
      arm = val;
    else if (key == "--request")
      id = val;
    else if (key == "--build-snapshot") {
      snapshot_source = fs::canonical(val);
      record = true;
    } else
      throw std::runtime_error("unknown review option " + key);
  }
  check(std::find(batches.begin(), batches.end(), through) != batches.end(),
        "unknown batch");
  auto bundle = pair / "run-1" / through / "bundle";
  auto hash = sha(bytes(bundle / "SHA256SUMS"));
  check(hash == load(pair / "reproduction.json")[through].asString(),
        "reproduction manifest differs");
  run({"sha256sum", "--strict", "--quiet", "--check", "SHA256SUMS"},
      {.cwd = bundle});
  auto dest = fresh_run("shared-review");
  std::cout << dest << std::endl;
  write(dest / "review.json",
        object({{"bundle", bundle.string()}, {"manifest_sha256", hash}}));
  auto snapshot = dest / "miner-inputs";
  fs::copy(snapshot_source, snapshot, fs::copy_options::recursive);
  auto worker = fs::canonical("/proc/self/exe");
  Strings arms =
      arm == "all" ? Strings{"transfer", "compare", "baseline"} : Strings{arm};
  std::vector<std::future<std::string>> jobs;
  for (auto name : arms) {
    check(name == "transfer" || name == "baseline" || name == "compare",
          "unknown arm");
    jobs.push_back(std::async(std::launch::async, [&, name] {
      auto d = dest / name;
      fs::create_directory(d);
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
                     bundle / "runtime/duckdb/lib",
                     "/usr/local/lib",
                     "--tmpfs",
                     "/tmp",
                     "--bind",
                     d,
                     "/tmp/brick-trial",
                     "--chdir",
                     "/tmp/brick-trial",
                     worker,
                     "--worker",
                     name,
                     pair,
                     bundle};
      if (!id.empty())
        cmd.push_back(id);
      return run(cmd, {.seconds = 7200});
    }));
  }
  for (auto &job : jobs)
    job.get();
  if (record)
    write(pair / "review-location.json",
          object({{"directory", dest.string()}, {"manifest_sha256", hash}}));
  return 0;
} catch (const std::exception &e) {
  std::cerr << e.what() << "\n";
  return 1;
}
