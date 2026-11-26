#include "pilot.hpp"
#include <iostream>
using namespace v5;
int main(int argc, char **argv) try {
  if (argc == 4 && std::string(argv[1]) == "verify") {
    std::cout << dump(verify(fs::canonical(argv[2]), argv[3]));
    return 0;
  }
  check(argc == 2 && std::string(argv[1]) == "pilot",
        "usage: v5-miner pilot | verify PACKAGE MANIFEST_SHA256");
  run({"make", "--no-print-directory", "-s", "-C", root, ".build/brick-scan"});
  auto work = fresh_run("v5-native");
  std::cout << work << std::endl;
  for (const auto *s :
       {"inventory", "extract", "standalone", "integration", "stage"})
    fs::create_directory(work / s);
  inventory(work / "inventory");
  J checked = object();
  for (const auto *route : {"api", "direct", "composition"}) {
    auto partial = work / route;
    fs::create_directories(partial);
    extract(partial, route);
    standalone(partial, partial, route);
    for (const auto &entry : fs::directory_iterator(partial)) {
      auto p = entry.path();
      auto name = p.filename();
      if (name == "notices")
        fs::copy(p, work / "extract/notices",
                 fs::copy_options::recursive | fs::copy_options::skip_existing);
      else if (p.extension() == ".hpp" ||
               (p.extension() == ".json" && name != "standalone.json"))
        fs::copy_file(p, work / "extract" / name);
    }
    auto results = load(partial / "standalone.json");
    for (const auto &key : results.getMemberNames())
      checked[key] = results[key];
  }
  write(work / "standalone/standalone.json", checked);
  integrate(work / "extract", work / "integration");
  assemble(work, work / "stage");
  std::cout << (work / "stage/package") << " "
            << digest(work / "stage/package/manifest.json") << "\n";
  return 0;
} catch (const std::exception &e) {
  std::cerr << e.what() << "\n";
  return 1;
}
