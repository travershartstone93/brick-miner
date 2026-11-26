#include "row-trial.hpp"
#include <iostream>
#include <map>
#include <set>
#include <tuple>

using namespace miner;
using namespace row_replay;
namespace {
const std::string tsv = "text/tab-separated-values";
const std::string stop = "typed conversion gap";
const Path response = "run/calls/interpret/response.json";
const Path emission = "run/components/emission.json";
const Path vocabulary = "run/vocabulary.json";
const Strings splits{"ordinary", "boundary", "empty", "transfer", "sparse"};
const std::set<std::string> pending{"H016", "H065", "H073",
                                    "H075", "H077", "H079"};

struct Evidence {
  std::string directory, arm;
  Strings proof_path;
};

const std::map<std::string, Evidence> earlier{
    {"H033",
     {"v5-telemetry-replay-XX5OHDSR",
      "reference-with-tsv-writer",
      {"requests", "H033", "reference-with-tsv-writer"}}},
    {"H035",
     {"v5-telemetry-replay-XX5OHDSR",
      "reference",
      {"requests", "H035", "reference"}}},
    {"H037", {"v5-sensor-history-replay-XXAGXfAx", "fixed", {"arms", "fixed"}}},
    {"H038",
     {"v5-telemetry-replay-XX5OHDSR",
      "reference-with-tsv-writer",
      {"requests", "H038", "reference-with-tsv-writer"}}},
    {"H076",
     {"v5-row-boundary-replay-XXyWAX5Y",
      "fixed",
      {"requests", "H076", "arms", "fixed"}}},
    {"H085",
     {"v5-reachability-replay-XXxyKVfL",
      "fixed",
      {"requests", "H085", "arms", "fixed"}}},
    {"H088",
     {"v5-row-boundary-replay-XXyWAX5Y",
      "fixed",
      {"requests", "H088", "arms", "fixed"}}},
    {"H096", {"v5-booking-state-replay-XXSn4lGn", "fixed", {"arms", "fixed"}}}};
const std::map<std::string, std::string> evidence_pins{
    {"v5-telemetry-replay-XX5OHDSR",
     "31a35180daf8cf52b050e5ca2eb6200147124f57a55f7f6f5aadb7ef228893c8"},
    {"v5-row-boundary-replay-XXyWAX5Y",
     "028580028f66b07a45370c97b7297e380e56b97d6ca44afe74870bfe49d7a64c"},
    {"v5-reachability-replay-XXxyKVfL",
     "45a5750d826899e2edaf056637d096418cde2ff389d8d9690aebd0800a511064"},
    {"v5-sensor-history-replay-XXAGXfAx",
     "6df035658af1a276ab9feff95063fde93544da0c36ab3ce1101a496bca479b3c"},
    {"v5-booking-state-replay-XXSn4lGn",
     "2a954d81d08f485e1cb8ded584a836907fad82026e0ead01dd3f370e43083048"}};
const std::map<std::string, std::string> families{
    {"text/plain", "finance"},
    {"inode/directory", "directories"},
    {"text/uri-list", "URI lists"}};

using Gap =
    std::tuple<std::string, unsigned, std::string, std::string, std::string>;
using Gaps = std::set<Gap>;

bool has_route(const J &probe, const J &uncovered, const J &expected_ports) {
  return probe["validation"].empty() && probe["gaps"].empty() &&
         probe["routes"].size() == 1 &&
         probe["routes"][0]["ports"] == expected_ports &&
         probe["routes"][0]["uncovered"] == uncovered &&
         probe["routes"][0]["status"] ==
             (uncovered.empty() && expected_ports.empty() ? "complete"
                                                          : "partial");
}

bool same_spec(const J &actual, const J &wanted) {
  if (actual["input_type"] != wanted["input_type"] ||
      actual["roles"].size() != wanted["roles"].size())
    return false;
  for (unsigned slot = 0; slot < wanted["roles"].size(); ++slot) {
    const auto &a = actual["roles"][slot], &b = wanted["roles"][slot];
    if (a["id"] != b["id"] || a["output_type"] != b["output_type"] ||
        a["requirement"]["operations"] != b["requirement"]["operations"] ||
        a["requirement"]["uncovered"] != b["requirement"]["uncovered"])
      return false;
  }
  return true;
}

void compare_saved_outputs(const Path &dir, const std::string &id,
                           const J &spec) {
  const auto fixtures =
      runs / "shared-review-5ui86km8/transfer" / id / "fixtures";
  for (const auto &split : splits) {
    const auto branch =
        split == "transfer" || split == "sparse" ? "held" : "run/check-initial";
    for (unsigned slot = 0; slot < spec["roles"].size(); ++slot) {
      const auto suffix =
          spec["roles"][slot]["output_type"] == "application/json" ? ".json"
                                                                   : ".txt";
      const auto name = "result-" + std::to_string(slot + 1) + suffix;
      check(bytes(dir / branch / split / "output" / name) ==
                bytes(fixtures / split / "output" / name),
            "retained fixture output differs for " + id + " " + split + " " +
                name);
    }
  }
}

J credit(const std::string &id, const J &wanted, const J &wanted_chains,
         const Path &writer_history, const J &legacy,
         const std::string &legacy_pin) {
  const auto legacy_h029 = id == "H029";
  Path dir, prior;
  std::string pin, catalog, arm;
  J proof;
  if (legacy_h029) {
    arm = "control-with-writer-staged";
    dir = writer_history / arm / id;
    prior = writer_history / "report.json";
    pin = legacy_pin;
    catalog = legacy["staged_sha256"].asString();
    check(catalog == hash(writer_history / "run-1/bundle/SHA256SUMS") &&
              hash(dir / "receipt.json") == "1c79636201bfe8be35fac8422ca8b7f7e6"
                                            "1092daebc4d9790e8064cb6510497b" &&
              hash(dir / emission) == "3edd00336b44e07329921e3a6a3a18cd700eca0c"
                                      "a2336e9e7e7141298d31d9f3" &&
              hash(dir / response) ==
                  hash(writer_history / "correct-operations.json"),
          "changed legacy H029 control files");
  } else {
    const auto &e = earlier.at(id);
    arm = e.arm;
    prior = runs / e.directory / "report.json";
    pin = hash(prior);
    check(pin == evidence_pins.at(e.directory),
          "changed retained execution report " + id);
    dir = prior.parent_path() / arm / id;
    const auto evidence = load(prior);
    const J *node = &evidence;
    for (const auto &key : e.proof_path)
      node = &(*node)[key];
    proof = *node;
    catalog = proof["catalog_sha256"].asString();
    check(proof["verified"] == true && proof["program_sha256"].isString() &&
              hash(dir / "receipt.json") ==
                  proof["receipt_sha256"].asString() &&
              hash(dir / response) == proof["spec_sha256"].asString() &&
              (!proof.isMember("plan_sha256") ||
               hash(dir / emission) == proof["plan_sha256"].asString()),
          "changed retained receipt or specification " + id);
  }

  const auto receipt = load(dir / "receipt.json");
  const auto plan = load(dir / emission);
  const auto spec = load(dir / response);
  const auto verdicts = scores(receipt);
  check(receipt["request"] == id && receipt["verified"] == true &&
            receipt["interpreted"] == true && receipt["editor"] == false &&
            same_spec(spec, wanted) &&
            plan["providers"].size() == wanted["roles"].size() &&
            plan["gaps"].empty() && plan["interpretation_errors"].empty(),
        "retained execution did not verify the supplied recipe " + id);
  if (legacy_h029) {
    check(verdicts == legacy["control"]["after"],
          "changed H029 control verdicts");
  } else {
    const auto &recorded =
        proof.isMember("scores") ? proof["scores"] : proof["verdicts"];
    check(receipt["manifest"] == catalog &&
              receipt["spec_sha256"] == proof["spec_sha256"] &&
              verdicts == recorded,
          "changed retained catalog or verdicts " + id);
  }
  for (const auto &split : splits)
    check(verdicts[split] == true,
          "saved fixture did not pass " + id + " " + split);
  for (unsigned slot = 0; slot < plan["providers"].size(); ++slot) {
    const auto &role = plan["providers"][slot];
    check(role["slot"].asUInt() == slot && complete(role) &&
              role["plan"]["steps"] == wanted_chains[slot],
          "retained provider chain differs for " + id + " slot " +
              std::to_string(slot));
  }
  compare_saved_outputs(dir, id, spec);
  return object(
      {{"path", dir.string()},
       {"arm", arm},
       {"report_sha256", pin},
       {"catalog_sha256", catalog},
       {"receipt_sha256", hash(dir / "receipt.json")},
       {"plan_sha256", hash(dir / emission)},
       {"spec_sha256", hash(dir / response)},
       {"program_sha256", legacy_h029 ? J(hash(dir / "run/project/converter"))
                                      : proof["program_sha256"]},
       {"scores", verdicts},
       {"role_count", wanted["roles"].size()}});
}

J writer_credit(const Path &history, const J &legacy, const std::string &id,
                const std::string &reply_pin) {
  auto proof = object();
  for (const auto &[label, folder] :
       {std::pair{"before", "without-writer-staged"},
        std::pair{"after", "with-writer-staged"}}) {
    const auto dir = history / folder / id;
    const auto rec = load(dir / "receipt.json");
    const auto verdicts = scores(rec);
    check(hash(dir / response) == reply_pin && rec["request"] == id &&
              rec["verified"] == false && rec["interpreted"] == true &&
              rec["editor"] == false &&
              verdicts == legacy["requests"][id][label],
          "changed writer-only evidence " + id + " " + label);
    proof[label] = object({{"path", dir.string()},
                           {"scores", verdicts},
                           {"receipt_sha256", hash(dir / "receipt.json")}});
  }
  return proof;
}
} // namespace

int main() try {
  const auto writer_history = runs / "ranked-gaps-5x2_hyp7";
  const auto rank_file = writer_history / "ranking.json";
  const auto rank_pin = hash(rank_file);
  check(rank_pin ==
            "3d902a86f77c7159581a1721cf07c96ccd7b0147259ffdee6edce8f976119495",
        "changed archived typed ranking");
  const auto ranking = load(rank_file);
  const auto cohort = ranking["stops"][stop];
  check(cohort.size() == 15 &&
            cohort.size() == 1 + earlier.size() + pending.size(),
        "changed typed-conversion cohort");
  Gaps ranked_gaps;
  for (const auto &pair : ranking["typed"])
    for (const auto &hit : pair["occurrences"])
      check(ranked_gaps
                .emplace(hit[0].asString(), hit[1].asUInt(), hit[2].asString(),
                         pair["source"].asString(),
                         pair["destination"].asString())
                .second,
            "duplicate ranked typed gap");

  const auto legacy_pin = hash(writer_history / "report.json");
  check(legacy_pin ==
            "28c412fe208b20e6bab66365217ee9726c1e13e1ad0345d234a7607152168171",
        "changed earlier writer comparison");
  const auto legacy = load(writer_history / "report.json");
  const auto previous =
      runs / "v5-residual-closure-replay-XXPB76Hi/report.json";
  const auto previous_pin = hash(previous);
  check(previous_pin == "0f4005ba7e35108d88e20c6c67809d038454dc8546cd36813d6f62"
                        "213da2edf0" &&
            load(previous)["cohort"]["verified_requests"] == 19,
        "changed preceding checkpoint");
  const auto recipes_file = root / "shared/request-recipes.json";
  const auto recipe_pin = hash(recipes_file);
  check(recipe_pin ==
            "768676cbc34ef7db9d869520bd3d26d86123f49a3c9803901eb5653de3a6f49b",
        "changed supplied recipes");
  const auto recipes = load(recipes_file);

  const auto parent = runs / "v5-queue-XXH5XMi9/stage/package";
  const auto writer_stage = runs / "v5-tsv-writer-XXAf0x0y/stage";
  const auto base = parent / "bundle", with_writer = writer_stage / "bundle";
  const auto parent_pin = hash(parent / "manifest.json");
  const auto writer_pin = hash(writer_stage / "manifest.json");
  check(parent_pin == "d15aee96917ff440f58f76270e3ca48575268dcfcf2bce951805c865"
                      "aae3af63" &&
            writer_pin == "0047b47009df08f7ee228729eb46b4438ec9d59eadd7c44647ec"
                          "6761eeabb994",
        "changed catalog stages");
  verify_bundle(
      base, "436db92629463daee559de2cbbc1c81519e12306d5bf01cc6d2883f3140e779c");
  verify_bundle(
      with_writer,
      "b2f8a40d44b1589779fc82fcfa23995df5cb2b2432167e9383f6f22e9c828d54");
  const auto writer =
      exact_row(with_writer, "write_tsv_lines", "strings", "out:" + tsv);
  check(remove_binding(read_tree(with_writer), writer).at("SHA256SUMS") ==
            bytes(base / "SHA256SUMS"),
        "catalog differs outside the TSV writer");
  const auto active = fs::canonical(volume / "unllm/boilerplate/active");
  const auto active_pin = hash(active / "SHA256SUMS");
  check(active_pin == legacy["base_sha256"].asString(),
        "changed published catalog");

  const auto work = fresh_run("v5-typed-reconciliation");
  std::cout << work.string() << std::endl;
  auto report = object({{"active_path", active.string()},
                        {"active_sha256", active_pin},
                        {"ranking_sha256", rank_pin},
                        {"writer_history_sha256", legacy_pin},
                        {"previous_sha256", previous_pin},
                        {"recipes_sha256", recipe_pin},
                        {"parent_manifest_sha256", parent_pin},
                        {"writer_manifest_sha256", writer_pin},
                        {"writer", writer},
                        {"tools", tools()}});
  report["tools"]["unllm_logic_worker"] =
      hash(main_repo / "build/native/unllm_logic_worker");

  const auto archive = runs / "english-audit-bcsgf8pi/replay-staged";
  const auto original =
      volume / "unllm/experiments/boilerplate-B42-VrtYbHSf/fresh-staged";
  Gaps observed_gaps;
  const auto writer_port = array({"strings", "out:" + tsv});
  std::map<std::string, Strings> open_families;
  unsigned role_count = 0, writer_uses = 0, credited_roles = 0, probe_count = 0;
  for (const auto &request : cohort) {
    const auto id = request.asString();
    const auto old_dir = archive / id;
    const auto reply_pin = hash(old_dir / response);
    const auto saved = load(old_dir / response);
    const auto old = load(old_dir / emission);
    const auto &roles = saved["roles"];
    check(!report["requests"].isMember(id) &&
              reply_pin == hash(original / id / response) &&
              old["providers"].size() == roles.size() &&
              roles.size() == recipes[id].size(),
          "changed archived context " + id);
    auto &entry = report["requests"][id];
    entry =
        object({{"reply_sha256", reply_pin},
                {"request_sha256", hash(old_dir / "request.txt")},
                {"archived_plan_sha256", hash(old_dir / emission)},
                {"archived_receipt_sha256", hash(old_dir / "receipt.json")},
                {"replay_menu_sha256", hash(old_dir / vocabulary)},
                {"archived_input_type", saved["input_type"]},
                {"archived_scores", scores(load(old_dir / "receipt.json"))}});
    const auto menu = load(old_dir / vocabulary)["outcomes"];
    auto supplied = saved;
    if (id == "H016") {
      const auto input = runs / "shared-review-5ui86km8/transfer/H016/fixtures/"
                                "ordinary/input/ledger.csv";
      check(saved["input_type"] == "text/plain" &&
                bytes(old_dir / "request.txt").find("CSV") !=
                    std::string::npos &&
                bytes(input).starts_with("side,ref,cents"),
            "changed CSV format evidence for H016");
      supplied["input_type"] = "text/csv";
      entry["csv_fixture_sha256"] = hash(input);
    }
    entry["supplied_input_type"] = supplied["input_type"];
    auto chains = array();
    for (unsigned slot = 0; slot < roles.size(); ++slot) {
      ++role_count;
      const auto &role = roles[slot];
      const auto name = role["id"].asString();
      const auto &req = role["requirement"];
      const auto &old_role = old["providers"][slot];
      check(old_role["slot"].asUInt() == slot &&
                old_role["uncovered"] == req["uncovered"],
            "changed saved role " + id + "/" + name);
      auto old_ports = array(), remaining = array();
      for (const auto &gap : old_role["typed_gaps"]) {
        const auto pair = array({gap["from"], gap["to"]});
        old_ports.append(pair);
        check(observed_gaps
                  .emplace(id, slot, gap["name"].asString(),
                           gap["from"].asString(), gap["to"].asString())
                  .second,
              "duplicate archived gap " + id);
        if (pair == writer_port)
          ++writer_uses;
        else
          remaining.append(pair);
      }
      auto spec = saved;
      spec["roles"] = array({role});
      auto probe = [&](const std::string &arm, const Path &bundle,
                       const J &input) {
        ++probe_count;
        return plan_probe(bundle, work / "plans" / id / name / arm, input);
      };
      const auto archived = probe("archived", base, spec);
      const auto writer_only = probe("writer-only", with_writer, spec);
      check(has_route(archived, req["uncovered"], old_ports) &&
                has_route(writer_only, req["uncovered"], remaining),
            "archived typed route differs for " + id + "/" + name);

      auto &fixed = supplied["roles"][slot]["requirement"];
      fixed["operations"] = operations(recipes[id][slot]);
      fixed["uncovered"] = array();
      spec["roles"][0]["requirement"] = fixed;
      auto &out = entry["roles"][name];
      out = object({{"output_type", role["output_type"]},
                    {"archived_operations", req["operations"]},
                    {"archived_uncovered", req["uncovered"]},
                    {"ranked_ports", old_ports},
                    {"archived", archived},
                    {"writer_only", writer_only},
                    {"supplied_operations", fixed["operations"]}});
      if (id == "H016") {
        const auto wrong = probe("supplied-as-plain-text", with_writer, spec);
        check(
            wrong["validation"].empty() && wrong["routes"].empty() &&
                wrong["gaps"]["r"].asString().starts_with("No provider chain"),
            "H016 supplied CSV operations accepted a plain-text source");
        out["supplied_as_plain_text"] = wrong;
      }
      spec["input_type"] = supplied["input_type"];
      const auto without = probe("supplied-without-writer", base, spec);
      const auto complete_with =
          probe("supplied-with-writer", with_writer, spec);
      const auto needs_writer = role["output_type"] == tsv;
      check(has_route(without, array(),
                      needs_writer ? array({writer_port}) : array()) &&
                has_route(complete_with, array(), array()),
            "retained supplied construction differs for " + id + "/" + name);
      out["supplied_without_writer"] = without;
      out["supplied_with_writer"] = complete_with;
      chains.append(complete_with["routes"][0]["steps"]);
      std::set<std::string> omitted;
      for (const auto &op : fixed["operations"])
        if (!menu.isMember(op["outcome"].asString()))
          omitted.insert(op["outcome"].asString());
      out["supplied_operations_absent_from_replay_menu"] =
          string_array(Strings(omitted.begin(), omitted.end()));
    }
    if (legacy["requests"].isMember(id))
      entry["prior_writer_only"] =
          writer_credit(writer_history, legacy, id, reply_pin);
    if (pending.contains(id)) {
      open_families[families.at(saved["input_type"].asString())].push_back(id);
      entry["execution_status"] = "pending_scoped_replay";
    } else {
      entry["retained_execution"] =
          credit(id, supplied, chains, writer_history, legacy, legacy_pin);
      entry["execution_status"] = "verified_prior_replay";
      credited_roles += roles.size();
    }
  }
  check(observed_gaps == ranked_gaps && observed_gaps.size() == 22 &&
            role_count == 20 && writer_uses == 10 && probe_count == 82 &&
            credited_roles == 12,
        "typed cohort totals differ");
  auto open = array();
  for (const auto &family : {"URI lists", "directories", "finance"})
    open.append(object({{"name", family},
                        {"requests", string_array(open_families.at(family))},
                        {"count", open_families.at(family).size()}}));
  report["open_families"] = open;
  report["request_count"] = cohort.size();
  report["role_count"] = role_count;
  report["typed_occurrences"] = observed_gaps.size();
  report["writer_occurrences"] = writer_uses;
  report["supplied_complete_roles"] = role_count;
  report["probe_count"] = probe_count;
  report["previously_verified_requests"] = cohort.size() - pending.size();
  report["previously_verified_roles"] = credited_roles;
  report["credited_fixture_passes"] =
      (cohort.size() - pending.size()) * splits.size();
  report["open_requests"] = pending.size();
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
