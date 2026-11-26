#include "duckdb.hpp"
#include "qualification.hpp"
#include <iostream>

namespace rows {
void qualify(bool mined);
}
using namespace miner;
static void saved_client(const Path &work) {
  auto bundle = work / "bundle", runtime = bundle / "runtime/duckdb",
       dest = work / "saved-bundle-client";
  fs::create_directory(dest);
  setenv("PKG_CONFIG_PATH", (runtime / "lib/pkgconfig").c_str(), 1);
  unsetenv("LD_LIBRARY_PATH");
  Strings cmd = {"g++-15",
                 "-std=c++17",
                 "-O1",
                 "-I",
                 bundle / "headers",
                 "-I",
                 bundle / "providers",
                 bundle / "qualifications/row-operations/tests/client.cpp",
                 "-o",
                 dest / "client"};
  append(cmd, flags({"brick-row-kernels"}));
  run(cmd);
  auto deps = run({"ldd", dest / "client"});
  for (std::string name : {"libduckdb.so", "libbrick_row_kernels.so"}) {
    auto marker = name + " => ";
    auto pos = deps.find(marker);
    check(pos != deps.npos, "missing saved runtime " + name);
    pos += marker.size();
    check(fs::equivalent(deps.substr(pos, deps.find(" (", pos) - pos),
                         runtime / "lib" / name),
          "wrong saved library " + name);
  }
  run({dest / "client"}, {.cwd = dest, .seconds = 15});
}
int main(int argc, char **argv) try {
  check(argc >= 2, "expected authored or mined mode");
  std::string mode = argv[1];
  if (mode == "--worker") {
    check(argc == 3 && fs::current_path() == "/tmp/brick-trial",
          "private work directory required");
    rows::qualify(std::string(argv[2]) == "mined");
    return 0;
  }
  bool mined = mode == "mined";
  check(mined || mode == "authored", "invalid row qualification mode");
  if (argc == 3 && std::string(argv[2]) == "--help") {
    std::cout << "row qualification [--cache SIGNED_INPUTS]\n";
    return 0;
  }
  Path cache;
  if (argc > 2) {
    check(mined && argc == 4 && std::string(argv[2]) == "--cache",
          "invalid options");
    cache = fs::canonical(argv[3]);
  }
  auto pair = fresh_run(mined ? "mined-rows" : "row-ops");
  std::cout << pair << std::endl;
  if (mined && cache.empty())
    cache = duckdb_miner::fetch(pair / "downloads");
  auto snapshot = pair / "miner-inputs";
  fs::create_directories(snapshot / "src");
  fs::copy(root / "row-ops", snapshot / "row-ops", fs::copy_options::recursive);
  fs::copy(root / "src/experiments", snapshot / "src/experiments",
           fs::copy_options::recursive);
  auto worker = fs::canonical("/proc/self/exe");
  duckdb_miner::copy(worker, snapshot / "row-pilot");
  if (mined)
    duckdb_miner::copy(root / ".build/extract-row-kernels",
                       snapshot / "extract-row-kernels");
  J first;
  for (int i : {1, 2}) {
    auto work = pair / ("run-" + std::to_string(i));
    fs::create_directory(work);
    Strings sandbox = {"bwrap",
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
                       "C"};
    append(sandbox,
           {"--ro-bind", snapshot / "row-ops", root / "row-ops", "--ro-bind",
            snapshot / "src/experiments", root / "src/experiments", "--ro-bind",
            snapshot / "row-pilot", worker});
    if (mined)
      append(sandbox, {"--ro-bind", snapshot / "extract-row-kernels",
                       root / ".build/extract-row-kernels"});
    auto cmd = sandbox;
    double elapsed = 0;
    if (mined) {
      auto started = qualification::seconds();
      append(cmd, {"--ro-bind", cache, "/tmp/inputs", "--bind", work, runs,
                   root / ".build/extract-row-kernels", "--cache",
                   "/tmp/inputs", "--output", runs / "native"});
      run(cmd, {.seconds = 1800});
      elapsed = qualification::seconds() - started;
      for (auto &[from, to] : std::map<Path, Path>{
               {work / "native/libbrick_row_kernels.so",
                "lib/libbrick_row_kernels.so"},
               {work / "native/libduckdb.so", "lib/libduckdb.so"},
               {work / "native/duckdb.h", "include/duckdb.h"},
               {root / "row-ops/mined/brick-row-kernels.pc",
                "lib/pkgconfig/brick-row-kernels.pc"}})
        duckdb_miner::copy(from, work / "runtime" / to);
      cmd = sandbox;
      append(cmd, {"--ro-bind", work / "runtime", "/usr/local", "--setenv",
                   "PKG_CONFIG_PATH", "/usr/local/lib/pkgconfig"});
    }
    append(cmd, {worker, "--worker", mode});
    run(cmd, {.seconds = 3600});
    auto report = load(work / "report.json");
    if (mined) {
      report["seconds"]["native_build_and_probe"] = elapsed;
      write(work / "report.json", report);
    }
    report.removeMember("seconds");
    if (i == 1)
      first = report;
    else
      check(first == report, "clean repeats differ");
  }
  if (mined)
    saved_client(pair / "run-1");
  std::cout << first["manifest_sha256"].asString() << "  "
            << (pair / "run-1/bundle") << std::endl;
  return 0;
} catch (const std::exception &e) {
  std::cerr << e.what() << '\n';
  return 1;
}
