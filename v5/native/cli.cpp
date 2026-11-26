#include "queue.hpp"
#include <charconv>
#include <iostream>
#include <limits>
using namespace v5;
int main(int argc, char **argv) try {
  std::string cmd = argc > 1 ? argv[1] : "--help";
  if (cmd == "--help") {
    std::cout << "v5-miner pilot [--steps N] | resume DIR [--steps N] | status "
                 "DIR | cancel DIR | verify PACKAGE SHA256\n";
    return 0;
  }
  if (cmd == "--worker") {
    check(argc == 5 && fs::current_path() == Path(argv[4]) &&
              fs::exists("/opt/brick-worker") &&
              fs::equivalent("/proc/self/exe", "/opt/brick-worker"),
          "expected private step worker");
    auto resources = worker_step(argv[2], argv[3], argv[4]);
    write(Path(argv[4]) / "resource-use.json", resources);
    return 0;
  }
  if (cmd == "verify") {
    check(argc == 4, "expected package and manifest digest");
    std::cout << dump(verify(fs::canonical(argv[2]), argv[3]));
    return 0;
  }
  if (cmd == "status" || cmd == "cancel") {
    check(argc == 3, "expected run directory");
    auto p = fs::canonical(argv[2]);
    if (cmd == "cancel")
      cancel_queue(p);
    std::cout << dump(queue_status(p));
    return 0;
  }
  check(cmd == "pilot" || cmd == "resume", "unknown command " + cmd);
  int offset = cmd == "pilot" ? 2 : 3;
  check(argc == offset || argc == offset + 2, "invalid pilot options");
  unsigned steps = std::numeric_limits<unsigned>::max();
  if (argc > offset) {
    check(std::string(argv[offset]) == "--steps", "expected --steps");
    std::string s = argv[offset + 1];
    auto n = std::from_chars(s.data(), s.data() + s.size(), steps);
    check(n.ec == std::errc{} && n.ptr == s.data() + s.size(),
          "invalid step bound");
  }
  Path work;
  if (cmd == "pilot") {
    run({"make", "--no-print-directory", "-s", "-C", root,
         ".build/brick-scan"});
    work = fresh_run("v5-queue");
    std::cout << work.string() << std::endl;
    initialize_queue(work);
  } else
    work = fs::canonical(argv[2]);
  advance(work, steps);
  auto state = queue_status(work);
  if (state.isMember("manifest_sha256"))
    std::cout << (work / "stage/package").string() << " "
              << state["manifest_sha256"].asString() << "\n";
  else
    std::cout << dump(state);
  return 0;
} catch (const std::exception &e) {
  std::cerr << e.what() << '\n';
  return 1;
}
