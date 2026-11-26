#include "pilot.hpp"
#include <iostream>
#include <sstream>
using namespace v5;
namespace {
const std::string successor =
    "4f87953e1e215409febd74393e3f319c7732b78cbd3f628d2412c05d2e2d3bd4";
J receipt(const Strings &args, const std::string &disposition) {
  auto raw = run(args);
  auto split = raw.rfind(' ');
  check(split != raw.npos, "missing receipt pin");
  Path file = raw.substr(0, split);
  check_corpus(file);
  std::string pin;
  std::istringstream(raw.substr(split + 1)) >> pin;
  check(digest(file) == pin, "published receipt pin mismatch");
  auto result = load(file);
  check(result["disposition"] == disposition &&
            result["scheduled_jobs"].empty() &&
            result["mining_authorized"] == false &&
            result["activated"] == false,
        "incorrect disposition or authority");
  return object(
      {{"file", file.string()}, {"sha256", pin}, {"receipt", result}});
}
void rejected(const Strings &args, const std::string &reason) {
  auto p = execute(args);
  check(p.status != 0 && p.err.find(reason) != p.err.npos,
        "expected rejection " + reason + ", observed " + p.err);
}
} // namespace
int main() try {
  const Path handoff =
                 volume / "unllm/experiments/v5-h1-semantic-fVfGorYZ/handoff",
             before = runs / "v5-queue-XX9hlImk/stage/package",
             after = runs / "v5-queue-XXkyvO7O/stage/package",
             cli = root / ".build/v5-gap-review";
  auto ev = fresh_run("v5-gap-controls");
  J results = object();
  Strings
      catalog =
          {cli,
           handoff / "catalog/gap.json",
           "a05906986c76883439b95c0f0826b59879f5d5b92eba8bf352230f034b9fc143",
           after,
           after,
           successor},
      qualification =
          {cli,
           handoff / "old/gap.json",
           "a564bc87e7a64dbc4277027841e6fa43f888f99e692c5082a99c366101d4f9ba",
           before,
           after,
           successor,
           handoff / "closed/decision.json",
           "05eec7346c58c2657a1dda2237fa9158bbbf931e84916da169641f988fde56ef"},
      constructor = {
          cli,
          handoff / "reader/gap.json",
          "a5b99c813b74991501f20f31be3ecec135dcd30c25b2b27091f769368f5503fe",
          after,
          after,
          successor};
  auto cat = receipt(catalog, "no_direct_binding_in_reviewed_catalog"),
       pos = receipt(qualification, "qualified_provider_proposed"),
       builder = receipt(constructor, "returned_to_construction");
  check(cat["receipt"]["inventory"]["direct"].empty() &&
            cat["receipt"]["inventory"]["related_outputs"][0]["provider_id"] ==
                "x20" &&
            pos["receipt"]["proposed_providers"][0]["provider_id"] == "x19" &&
            builder["receipt"]["proposed_providers"].empty(),
        "wrong candidate identities");
  check(receipt(catalog, "no_direct_binding_in_reviewed_catalog")["sha256"] ==
            cat["sha256"],
        "receipt is not deterministic");
  for (const auto &[name, v] : std::map<std::string, J>{
           {"catalog", cat}, {"qualification", pos}, {"construction", builder}})
    results[name] = object({{"path", v["file"]},
                            {"sha256", v["sha256"]},
                            {"gap_id", v["receipt"]["gap_id"]}});
  auto mutation = [&](const J &j, const std::string &name) {
    auto f = ev / (name + ".json");
    write(f, j);
    return f;
  };
  auto bad = catalog;
  bad[2] = successor;
  rejected(bad, "handoff file pin mismatch");
  auto g = load(catalog[1]);
  const auto original = g;
  auto invalid = [&](const std::string &label, const std::string &why) {
    auto file = mutation(g, label);
    auto args = catalog;
    args[1] = file;
    args[2] = digest(file);
    rejected(args, why);
    g = original;
  };
  g["goal_id"] = "changed";
  invalid("changed-id", "gap identity differs");
  g["mining_authorized"] = true;
  invalid("grant", "executable gap export");
  g["typed_task"]["expected_outputs"] = array();
  invalid("unexpected-field", "unexpected handoff field");
  g["diagnosis"]["complete_within_scope"] = false;
  invalid("incomplete", "requires a complete bounded catalog diagnosis");
  bad = catalog;
  bad[3] = before;
  rejected(bad, "manifest digest mismatch");
  bad = qualification;
  auto d = load(qualification[6]);
  const auto selected = d;
  d["task_sha256"] = std::string(64, '0');
  auto file = mutation(d, "another-goal");
  bad[6] = file;
  bad[7] = digest(file);
  rejected(bad, "does not select this admitted task");
  d = selected;
  auto registry = load(after / "bundle/providers/contracts.json");
  for (const auto &row : registry["providers"])
    if (row["id"] == "item_count")
      d["eligible"] = array({row});
  file = mutation(d, "wrong-callable");
  bad[6] = file;
  bad[7] = digest(file);
  rejected(bad, "contradicts requested semantics");
  results["negative_controls"] =
      array({"untrusted file pin", "changed goal identity", "mining grant",
             "unexpected expected-output field", "incomplete catalog review",
             "wrong origin catalog", "selection for another obligation",
             "same-signature substitution"});
  write(ev / "controls.json", results);
  std::cout << (ev / "controls.json").string() << " "
            << digest(ev / "controls.json") << '\n';
  return 0;
} catch (const std::exception &e) {
  std::cerr << e.what() << '\n';
  return 1;
}
