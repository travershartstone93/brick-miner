#include "pilots.hpp"
#include "qualification.hpp"
#include <iomanip>
#include <iostream>
#include <sstream>
using namespace shared;
namespace {
void check_fixtures(const J &checks) {
  check(checks.size() == 5, "incomplete fixture set");
  for (const auto &c : checks)
    check(c["pass_"].asBool(), "failed fixture");
}
std::string ids(const Strings &list) {
  return list.empty() ? "—" : join(list, ", ");
}
std::string link(const std::string &label, const Path &path) {
  return "[" + label + "](" + path.string() + ")";
}
} // namespace
int main(int argc, char **argv) try {
  check(argc >= 4 && argc <= 6,
        "shared/ledger PAIR REVIEW OUTPUT [COMPARISON_REVIEW [CLEAN_REPLAY]]");
  auto pair = fs::canonical(argv[1]), review = fs::canonical(argv[2]),
       cmp = argc > 4 ? fs::canonical(argv[4]) : review;
  auto hashes = load(pair / "reproduction.json");
  check(hashes.size() == batches.size(),
        "full shared-needs ledger requires all batches");
  auto bundle = pair / "run-1" / batches.back() / "bundle",
       stages = bundle / "qualifications/shared";
  auto digest = sha(bytes(bundle / "SHA256SUMS"));
  check(digest == hashes[batches.back()].asString(),
        "incorrect release digest");
  for (auto &dir : {review, cmp})
    check(load(dir / "review.json")["manifest_sha256"] == digest,
          "review measured a different release");
  auto baseline = load(review / "baseline/baseline.json"),
       receipt = load(review / "transfer/transfer.json"),
       direct = load(cmp / "compare/comparison.json");
  auto tasks = suite();
  check(tasks.getMemberNames() == receipt.getMemberNames() &&
            tasks.getMemberNames() == baseline["fixed"].getMemberNames(),
        "all 100 requests are required");
  std::map<std::string, Strings> users;
  Strings fixed, alternates;
  for (const auto &id : receipt.getMemberNames()) {
    const auto &r = receipt[id];
    check(r["passed"].asBool(), "failed transfer " + id);
    check_fixtures(r["checks"]);
    std::set<std::string> seen;
    for (const auto &arm : r["roles"])
      for (const auto &op : arm)
        seen.insert(op[0].asString());
    for (const auto &name : seen)
      users[name].push_back(id);
    if (baseline["fixed"][id]["pass"].asBool())
      fixed.push_back(id);
  }
  for (const auto &id : baseline["alternatives"].getMemberNames())
    if (baseline["alternatives"][id]["pass"].asBool())
      alternates.push_back(id);
  auto labels = load(here() / "ledger-labels.json"),
       extra = load(here() / "tests/additional-cases.json");
  Strings bricks, legacy, batch_rows, times;
  std::set<std::string> seen;
  unsigned checked = 0, clients = 0, bindings = 0;
  for (const auto &name : batches) {
    auto d = stages / name;
    auto requests = load(d / "requests.json"),
         contracts = load(d / "contracts.json"), cases = load(d / "cases.json"),
         oracles = load(d / "receipts.json");
    auto &measure = direct[name];
    auto source = link(name, d / "receipts.json");
    batch_rows.push_back(
        "| " + source + " | " + std::to_string(contracts.size()) + " | " +
        std::to_string(oracles.size()) + " | " +
        link(std::to_string(requests.size()), d / "requests.json") + " | `" +
        hashes[name].asString() + "` |");
    for (const auto &id : requests.getMemberNames()) {
      check(receipt[id]["roles"] == array({requests[id]["operations"]}),
            "recipe differs for " + id);
      check_fixtures(requests[id]["checks"]);
    }
    checked += requests.size();
    clients += oracles.size() + measure["additional_cases"].size();
    bindings += measure["checks"].asUInt();
    for (auto &key : pilots::original_keys(d / "contracts.json")) {
      check(labels["descriptions"].isMember(key) && seen.insert(key).second,
            "missing or duplicate capability in ledger");
      Strings data;
      for (const auto &set : {cases, extra})
        for (const auto &item : set)
          if (item["brick"] == key)
            data.push_back(item["id"].asString());
      bricks.push_back(
          "| **" + key + "**. " + labels["descriptions"][key].asString() +
          " | " + link(labels["sources"][key].asString(), d / "source.json") +
          " | " + join(data, ", ") + " | " + ids(users[key]) + " |");
    }
    auto timing = measure["kernel_route"];
    std::ostringstream out;
    out << "| " << name << std::fixed << std::setprecision(1);
    for (const auto *k : {"acquire_and_build_seconds", "brick_checks_seconds",
                          "end_user_checks_and_staging_seconds"})
      out << " | " << timing[k].asDouble();
    out << " | " << measure["binding_build_seconds"].asDouble() << " |";
    times.push_back(out.str());
  }
  check(seen.size() == labels["descriptions"].size(),
        "unaccounted ledger capability");
  for (const auto &entry : labels["legacy"])
    legacy.push_back("| " + entry[1].asString() + " | " +
                     ids(users[entry[0].asString()]) + " |");
  std::string replay;
  if (argc == 6) {
    auto full = fs::canonical(argv[5]);
    check(!fs::exists(full / "predecessor.json") &&
              load(full / "reproduction.json") == hashes,
          "invalid clean repeat");
    replay =
        "Two additional clean runs rebuilt all eight stages directly from the "
        "original 104-card input and matched every intermediate manifest. " +
        link("From-scratch reproduction", full / "reproduction.json") + "\n\n";
  }
  auto report = bytes(here() / "ledger.md.in");
  for (auto &[key, val] : std::map<std::string, std::string>{
           {"CARDS", std::to_string(load(bundle / "catalog.json").size())},
           {"COUNT", std::to_string(seen.size())},
           {"BUNDLE", bundle},
           {"PAIR", pair},
           {"REVIEW", review},
           {"COMPARISON", cmp},
           {"HASH", digest},
           {"REPLAY", replay},
           {"BRICKS", join(bricks, "\n")},
           {"LEGACY", join(legacy, "\n")},
           {"BATCHES", join(batch_rows, "\n")},
           {"TIMES", join(times, "\n")},
           {"CHECKED", std::to_string(checked)},
           {"ADDITIONAL", std::to_string(tasks.size() - checked)},
           {"FIXED_COUNT", std::to_string(fixed.size())},
           {"FIXED", ids(fixed)},
           {"GAIN", std::to_string(tasks.size() - fixed.size())},
           {"ALTERNATES", ids(alternates)},
           {"CLIENTS", std::to_string(clients)},
           {"BINDINGS", std::to_string(bindings)}})
    report = replace(report, "@" + key + "@", val);
  save(argv[3], report);
  return 0;
} catch (const std::exception &e) {
  std::cerr << e.what() << '\n';
  return 1;
}
