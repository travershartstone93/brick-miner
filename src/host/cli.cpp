#include "bundle.hpp"
#include "project.hpp"
#include <chrono>
#include <iostream>
#include <regex>
#include <set>

using namespace miner;
int main(int argc, char **argv) try {
  std::map<std::string, std::string> args;
  for (int i = 1; i < argc; i += 2) {
    std::string option = argv[i];
    if (option == "--help") {
      std::cout << "mine-project --source ROOT --database BUILD --output FRESH "
                   "[--provenance FILE --contracts FILE] [--baseline DIR "
                   "--manifest-sha256 HASH]\n";
      return 0;
    }
    const std::set<std::string> known = {
        "--source",    "--database", "--output",         "--provenance",
        "--contracts", "--baseline", "--manifest-sha256"};
    check(i + 1 < argc && known.contains(option) && !args.contains(option),
          "invalid option " + option);
    args[option] = argv[i + 1];
  }
  auto base = fs::canonical(args.at("--source")),
       db = fs::canonical(args.at("--database"));
  auto dest = fs::weakly_canonical(fs::absolute(args.at("--output")));
  Strings skipped;
  auto units = compilation_units(base, db, skipped);
  auto provenance = source_pins(base, args["--provenance"]);
  if (!provenance.isNull()) {
    auto expected = strings(provenance["translation_units"]);
    std::sort(expected.begin(), expected.end());
    check(expected == units.getMemberNames(),
          "source units differ from provenance");
  }
  J contracts = array(), baseline;
  Path generation;
  if (args.contains("--contracts")) {
    check(!provenance.isNull(), "package and source evidence required");
    run({"mountpoint", "-q", volume});
    auto relative = dest.lexically_relative(runs);
    check(!relative.empty() && *relative.begin() != ".." && relative != ".",
          "corpus staging directory required");
    contracts = load(args.at("--contracts"))["contracts"];
    std::set<std::string> names;
    for (const auto &c : contracts) {
      auto key = c["id"].asString();
      check(std::regex_match(key, std::regex("[a-z][a-z0-9_]*")) &&
                names.insert(key).second && !c["cases"].empty() &&
                !c["parameters"].empty(),
            "invalid or duplicate contract " + key);
      for (const auto &example : c["cases"])
        check(example.size() == 2 &&
                  example[0].size() == c["parameters"].size() &&
                  (example[1].isString() || example[1].isIntegral() ||
                   example[1].isBool()),
              "invalid example for " + key);
    }
    auto pins = load(root / "contracts/inputs.json");
    if (args.contains("--baseline") || args.contains("--manifest-sha256")) {
      generation = fs::canonical(args.at("--baseline"));
      check(sha(bytes(generation / "SHA256SUMS")) ==
                args.at("--manifest-sha256"),
            "generation digest mismatch");
      run({"sha256sum", "--check", "--strict", "--quiet", "SHA256SUMS"},
          {.cwd = generation});
    } else {
      generation = fs::canonical(volume / "unllm/boilerplate/active");
      check(generation.string().ends_with("/" + pins["base"].asString()) &&
                sha(manifest(read_tree(generation))) ==
                    pins["base_tree_sha256"].asString(),
            "active base differs from historical pin");
      for (const auto &f : pins["planner_files"].getMemberNames())
        check(sha(bytes(main_repo / f)) == pins["planner_files"][f].asString(),
              "historical planner pin differs: " + f);
    }
    baseline = object(
        {{"generation", generation.filename().string()},
         {"manifest_sha256", sha(bytes(generation / "verification.json"))},
         {"cards", load(generation / "catalog.json").size()}});
  }
  check(fs::create_directories(dest),
        "output already exists: " + dest.string());
  auto start = std::chrono::steady_clock::now();
  auto mined = mine_native(base, db, dest, units, provenance, contracts);
  std::pair<J, J> admission{object(), J()};
  if (!contracts.empty())
    admission = publish_project(base, dest, provenance, mined.inventory,
                                mined.checks, contracts, units, generation);
  J stats = object();
  for (const auto &r : mined.inventory) {
    auto status = r["status"].asString();
    stats[status] = stats.get(status, 0).asInt() + 1;
  }
  auto seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - start)
          .count();
  J report =
      object({{"units", string_array(units.getMemberNames())},
              {"skipped_non_cxx", string_array(skipped)},
              {"definitions", mined.inventory.size()},
              {"ast_status", stats},
              {"checks", mined.checks},
              {"qualifications", admission.first},
              {"bundle_manifest_sha256", admission.second},
              {"baseline", baseline},
              {"contract_sha256", args.contains("--contracts")
                                      ? J(sha(bytes(args.at("--contracts"))))
                                      : J()},
              {"inventory_sha256", sha(bytes(dest / "inventory.json"))},
              {"seconds", object({{"scan_and_build", mined.scan_seconds},
                                  {"client_checks", mined.client_seconds},
                                  {"total", seconds}})}});
  write(dest / "report.json", report);
  std::cout << units.size() << " project units, " << mined.inventory.size()
            << " definitions, " << mined.checks.size() << " extracted. Report "
            << (dest / "report.json") << "\n";
  return 0;
} catch (const std::exception &e) {
  std::cerr << e.what() << "\n";
  return 1;
}
