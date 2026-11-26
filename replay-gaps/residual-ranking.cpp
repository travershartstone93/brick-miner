#include "row-trial.hpp"
#include <algorithm>
#include <iostream>
#include <map>
#include <set>

using namespace miner;
using namespace row_replay;
namespace {
const std::string stop = "uncovered obligations without a typed gap";
const Path reply = "run/calls/interpret/response.json";
const Path emission = "run/components/emission.json";
const Path vocabulary = "run/vocabulary.json";
const Strings splits{"ordinary", "boundary", "empty", "transfer", "sparse"};

// Manual tags for the archived prose, in output role order.
const std::map<std::string, std::vector<Strings>> obligations{
    {"H010", {{"deduplication", "output formatting", "sequence order"}}},
    {"H041", {{"deduplication", "sequence order"}}},
    {"H047",
     {{"grouping and aggregation", "output formatting", "sequence order",
       "state membership"},
      {"grouping and aggregation", "output formatting", "sequence order",
       "state membership"}}},
    {"H051",
     {{"deduplication", "file kind", "path names and representation",
       "sequence order"}}},
    {"H052",
     {{"grouping and aggregation", "output formatting",
       "path names and representation", "sequence order"}}},
    {"H055", {{"file kind", "file size", "sequence order"}}},
    {"H057", {{}, {"path depth and root"}}},
    {"H058",
     {{"case-insensitive comparison", "deduplication", "file extension",
       "path depth and root", "sequence order"}}},
    {"H061",
     {{"deduplication", "grouping and aggregation", "output formatting",
       "path names and representation", "sequence order"}}},
    {"H062",
     {{"file size", "hidden path", "path depth and root",
       "path names and representation", "sequence order"}}},
    {"H064",
     {{"file content identity", "grouping and aggregation", "output formatting",
       "sequence order"}}},
    {"H067",
     {{"hidden path", "output formatting", "sequence order"},
      {"hidden path", "output formatting", "sequence order"}}},
    {"H068",
     {{"case-insensitive comparison", "grouping and aggregation",
       "output formatting", "path names and representation",
       "sequence order"}}},
    {"H069",
     {{"grouping and aggregation", "output formatting", "path depth and root",
       "sequence order"}}},
    {"H070",
     {{"case-insensitive comparison", "file extension",
       "grouping and aggregation", "output formatting",
       "path depth and root"}}},
    {"H071", {{"output formatting", "URI comments and blanks"}}},
    {"H072",
     {{"case-insensitive comparison", "line endings", "output formatting",
       "URI comments and blanks", "URI scheme"}}},
    {"H074",
     {{"line endings", "output formatting", "URI comments and blanks"}}},
    {"H078",
     {{"line endings", "output formatting", "URI comments and blanks",
       "URI spelling"}}}};

const std::map<std::string, std::string> families{
    {"text/tab-separated-values", "inventory"},
    {"application/xml", "publishing"},
    {"application/zip", "archives"},
    {"inode/directory", "directories"},
    {"text/uri-list", "URI lists"}};
const std::set<std::string> verified{"H058", "H070"};

struct Occurrences {
  std::set<std::string> requests, roles;
  void add(const std::string &id, const std::string &role) {
    requests.insert(id);
    roles.insert(id + "/" + role);
  }
};

using Groups = std::map<std::string, Occurrences>;

J rank(const Groups &groups) {
  std::vector<J> entries;
  for (const auto &[name, occurrences] : groups)
    entries.push_back(
        object({{"name", name},
                {"request_count", occurrences.requests.size()},
                {"role_count", occurrences.roles.size()},
                {"requests", string_array(Strings(occurrences.requests.begin(),
                                                  occurrences.requests.end()))},
                {"roles", string_array(Strings(occurrences.roles.begin(),
                                               occurrences.roles.end()))}}));
  std::sort(entries.begin(), entries.end(), [](const J &a, const J &b) {
    if (a["request_count"] != b["request_count"])
      return a["request_count"].asUInt() > b["request_count"].asUInt();
    return a["name"].asString() < b["name"].asString();
  });
  auto result = array();
  for (const auto &entry : entries)
    result.append(entry);
  return result;
}

bool has_route(const J &probe, const J &uncovered) {
  return probe["validation"].empty() && probe["gaps"].empty() &&
         probe["routes"].size() == 1 && probe["routes"][0]["ports"].empty() &&
         probe["routes"][0]["uncovered"] == uncovered &&
         probe["routes"][0]["status"] ==
             (uncovered.empty() ? "complete" : "partial");
}

J credit_paths(const Path &prior_dir, const J &prior, const std::string &id,
               const std::string &reply_pin, const J &spec, const J &routes) {
  const auto &proof = prior["requests"][id];
  const auto &arm = proof["correlated"];
  const auto dir = prior_dir / "correlated" / id;
  const auto spec_file = prior_dir / "specs" / id / "reference.json";
  const auto historical = load(spec_file);
  const auto receipt = load(dir / "receipt.json");
  const auto emitted = load(dir / emission);
  check(proof["interpretation_sha256"] == reply_pin &&
            hash(spec_file) == arm["spec_sha256"].asString() &&
            historical["input_type"] == spec["input_type"] &&
            historical["roles"][0]["output_type"] ==
                spec["roles"][0]["output_type"] &&
            historical["roles"][0]["requirement"] ==
                spec["roles"][0]["requirement"] &&
            hash(dir / "receipt.json") == arm["receipt_sha256"].asString() &&
            receipt["spec_sha256"] == arm["spec_sha256"] &&
            receipt["verified"] == true &&
            arm["catalog_sha256"] == prior["baseline_sha256"] &&
            receipt["manifest"] == prior["baseline_sha256"] &&
            arm["verified"] == true && emitted["providers"].size() == 1 &&
            complete(emitted["providers"][0]) &&
            emitted["providers"][0]["plan"]["steps"] == routes[0]["steps"] &&
            arm["providers"][0]["steps"] == routes[0]["steps"] &&
            arm["root_check"]["passed"] == true &&
            proof["reader-removed"]["verified"] == false,
        "retained path evidence differs for " + id);
  auto scores = object();
  for (const auto &split : splits) {
    const auto &case_result = arm["cases"][split];
    const auto split_dir =
        split == "transfer" || split == "sparse" ? "held" : "run/check-initial";
    const auto output = dir / split_dir / split / "output/result-1.txt";
    check(case_result["passed"] == true &&
              bytes(output) == case_result["output"].asString(),
          "retained path result differs for " + id + " " + split);
    scores[split] = true;
  }
  return object({{"catalog_sha256", arm["catalog_sha256"]},
                 {"receipt_sha256", arm["receipt_sha256"]},
                 {"spec_sha256", arm["spec_sha256"]},
                 {"program_sha256", arm["program_sha256"]},
                 {"scores", scores},
                 {"root_check", arm["root_check"]}});
}
} // namespace

int main() try {
  const auto rank_file = runs / "ranked-gaps-5x2_hyp7/ranking.json";
  const auto ranking_pin = hash(rank_file);
  check(ranking_pin ==
            "3d902a86f77c7159581a1721cf07c96ccd7b0147259ffdee6edce8f976119495",
        "changed archived ranking");
  const auto ranking = load(rank_file);
  const auto cohort = ranking["stops"][stop];
  check(cohort.size() == obligations.size() && cohort.size() == 19,
        "changed residual cohort");
  const auto previous =
      runs / "v5-unordered-singletons-replay-XXwTruXp/report.json";
  const auto previous_pin = hash(previous);
  check(previous_pin == "216acec42166d7aaa5c7aa15268600add9da1bd8b0cb518129221e"
                        "c4cd0b0fea" &&
            load(previous)["cohort"]["verified_requests"] == 32,
        "changed preceding checkpoint");

  const auto recipe = root / "shared/request-recipes.json";
  const auto recipes_pin = hash(recipe);
  check(recipes_pin ==
            "768676cbc34ef7db9d869520bd3d26d86123f49a3c9803901eb5653de3a6f49b",
        "changed supplied recipes");
  const auto recipes = load(recipe);
  const auto parent = runs / "v5-queue-XXH5XMi9/stage/package";
  const auto base = parent / "bundle";
  const auto parent_pin = hash(parent / "manifest.json");
  check(parent_pin ==
            "d15aee96917ff440f58f76270e3ca48575268dcfcf2bce951805c865aae3af63",
        "changed V5 parent");
  verify_bundle(
      base, "436db92629463daee559de2cbbc1c81519e12306d5bf01cc6d2883f3140e779c");
  const auto active = fs::canonical(volume / "unllm/boilerplate/active");
  const auto active_pin = hash(active / "SHA256SUMS");
  check(active_pin ==
            "e4564e3079f0b32b3379a411b170ce9af61ec80ab7938b6a974e724abd538328",
        "changed published catalog");
  const auto path_dir = runs / "v5-path-replay-XXSSPzLT";
  const auto path_pin = hash(path_dir / "report.json");
  const auto path_report = load(path_dir / "report.json");
  check(path_pin == "897d493da10f1982ec058fbc8bc2f4587049fc8cd81658234cb9f757ae"
                    "da2db8" &&
            path_report["baseline_sha256"] == active_pin &&
            path_report["recipes_sha256"] == recipes_pin,
        "changed retained path evidence");

  std::set<std::string> retained;
  const auto contracts = load(base / "providers/contracts.json");
  for (const auto &binding : contracts["providers"])
    if (binding["effect"].isString())
      retained.insert(binding["effect"].asString());

  const auto work = fresh_run("v5-residual-ranking");
  std::cout << work.string() << std::endl;
  auto report = object({{"active_path", active.string()},
                        {"active_sha256", active_pin},
                        {"ranking_sha256", ranking_pin},
                        {"previous_checkpoint_sha256", previous_pin},
                        {"recipes_sha256", recipes_pin},
                        {"parent_manifest_sha256", parent_pin},
                        {"path_evidence_sha256", path_pin},
                        {"tools", tools()}});
  report["tools"]["unllm_logic_worker"] =
      hash(main_repo / "build/native/unllm_logic_worker");
  Groups by_family, by_open_family, tags, open_tags, missing_from_reply,
      missing_from_original_menu, missing_from_replay_menu, missing_from_parent;
  unsigned role_count = 0, partial_count = 0;
  const auto archive = runs / "english-audit-bcsgf8pi/replay-staged";
  const auto original =
      volume / "unllm/experiments/boilerplate-B42-VrtYbHSf/fresh-staged";
  for (const auto &request : cohort) {
    const auto id = request.asString();
    const auto dir = archive / id;
    const auto old_dir = original / id;
    const auto reply_pin = hash(dir / reply);
    const auto saved = load(dir / reply);
    const auto &roles = saved["roles"];
    const auto old = load(dir / emission);
    const auto receipt = load(dir / "receipt.json");
    const auto original_menu = load(old_dir / vocabulary)["outcomes"];
    const auto replay_menu = load(dir / vocabulary)["outcomes"];
    const auto &labels = obligations.at(id);
    const auto &family = families.at(saved["input_type"].asString());
    const bool open = !verified.contains(id);
    check(!report["requests"].isMember(id) &&
              reply_pin == hash(old_dir / reply) && receipt["request"] == id &&
              receipt["interpreted"] == true && receipt["editor"] == false &&
              old["providers"].size() == roles.size() && old["gaps"].empty() &&
              original_menu.isObject() && replay_menu.isObject() &&
              labels.size() == roles.size() &&
              recipes[id].size() == roles.size(),
          "changed archived context for " + id);
    auto &entry = report["requests"][id];
    entry = object({{"family", family},
                    {"input_type", saved["input_type"]},
                    {"request_sha256", hash(dir / "request.txt")},
                    {"reply_sha256", reply_pin},
                    {"original_menu_sha256", hash(old_dir / vocabulary)},
                    {"replay_menu_sha256", hash(dir / vocabulary)},
                    {"archived_plan_sha256", hash(dir / emission)},
                    {"archived_receipt_sha256", hash(dir / "receipt.json")},
                    {"previously_verified", !open}});
    for (unsigned slot = 0; slot < roles.size(); ++slot) {
      ++role_count;
      const auto &role = roles[slot];
      const auto name = role["id"].asString();
      const auto &requirement = role["requirement"];
      const auto &residual = requirement["uncovered"];
      const auto &old_role = old["providers"][slot];
      check(old_role["slot"].asUInt() == slot &&
                old_role["uncovered"] == residual &&
                old_role["typed_gaps"].empty() &&
                old_role["status"] ==
                    (residual.empty() ? "complete" : "partial") &&
                !requirement["operations"].empty() &&
                labels[slot].empty() == residual.empty(),
            "changed residual role " + id + "/" + name);
      if (!residual.empty())
        ++partial_count;
      by_family[family].add(id, name);
      if (open)
        by_open_family[family].add(id, name);
      for (const auto &tag : labels[slot]) {
        tags[tag].add(id, name);
        if (open)
          open_tags[tag].add(id, name);
      }

      auto spec = saved;
      spec["roles"] = array({role});
      const auto dest = work / "plans" / id / name;
      const auto archived = plan_probe(base, dest / "archived", spec);
      check(has_route(archived, residual),
            "residual stop did not replay for " + id + "/" + name);

      const auto steps = operations(recipes[id][slot]);
      auto transformed = requirement;
      transformed["operations"] = steps;
      transformed["uncovered"] = array();
      spec["roles"][0]["requirement"] = transformed;
      const auto supplied = plan_probe(base, dest / "supplied", spec);
      check(has_route(supplied, array()),
            "supplied construction did not plan for " + id + "/" + name + ": " +
                dump(supplied));

      std::set<std::string> selected, added, removed;
      for (const auto &step : requirement["operations"])
        selected.insert(step["outcome"].asString());
      removed = selected;
      for (const auto &step : steps) {
        const auto effect = step["outcome"].asString();
        removed.erase(effect);
        if (!selected.contains(effect)) {
          added.insert(effect);
          missing_from_reply[effect].add(id, name);
        }
        if (!original_menu.isMember(effect))
          missing_from_original_menu[effect].add(id, name);
        if (!replay_menu.isMember(effect))
          missing_from_replay_menu[effect].add(id, name);
        if (!retained.contains(effect))
          missing_from_parent[effect].add(id, name);
      }
      entry["roles"][name] =
          object({{"output_type", role["output_type"]},
                  {"uncovered", residual},
                  {"manual_tags", string_array(labels[slot])},
                  {"archived_operations", requirement["operations"]},
                  {"supplied_operations", steps},
                  {"outcomes_added",
                   string_array(Strings(added.begin(), added.end()))},
                  {"outcomes_removed",
                   string_array(Strings(removed.begin(), removed.end()))},
                  {"archived", archived},
                  {"supplied", supplied}});
      if (!open) {
        check(roles.size() == 1, "unexpected multi-role path evidence");
        entry["retained_fixture_evidence"] = credit_paths(
            path_dir, path_report, id, reply_pin, spec, supplied["routes"]);
      }
    }
  }
  check(role_count == 22 && partial_count == 21 && missing_from_parent.empty(),
        "unexpected role count or absent supplied operation");
  report["request_count"] = cohort.size();
  report["role_count"] = role_count;
  report["partial_role_count"] = partial_count;
  report["supplied_complete_roles"] = role_count;
  report["previously_verified_requests"] = verified.size();
  report["open_requests"] = cohort.size() - verified.size();
  report["families"] = rank(by_family);
  report["open_families"] = rank(by_open_family);
  report["archived_prose_tags"] = rank(tags);
  report["open_archived_prose_tags"] = rank(open_tags);
  report["supplied_outcomes_missing_from_operations"] =
      rank(missing_from_reply);
  report["supplied_outcomes_missing_from_original_menu"] =
      rank(missing_from_original_menu);
  report["supplied_outcomes_missing_from_replay_menu"] =
      rank(missing_from_replay_menu);
  report["supplied_outcomes_absent_from_parent"] = rank(missing_from_parent);
  check(hash(active / "SHA256SUMS") == active_pin, "published catalog changed");
  write(work / ".report.json", report);
  run({"sync", "-f", work});
  fs::rename(work / ".report.json", work / "report.json");
  std::cout << hash(work / "report.json") << std::endl;
  return 0;
} catch (const std::exception &e) {
  std::cerr << e.what() << '\n';
  return 1;
}
