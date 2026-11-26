#include "row-trial.hpp"
#include <algorithm>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <tuple>

using namespace miner;
using namespace row_replay;
namespace {
const Strings splits{"ordinary", "boundary", "empty", "transfer", "sparse"};
const Path response = "run/calls/interpret/response.json";
const Path emission = "run/components/emission.json";
const std::string no_outcomes = "Expected between one and 32 ordered outcomes";

struct TrialSource {
  std::string directory, pin, section, arm;
  std::string id{};
};

// Only the reviewed supplied arms are eligible for the join.
const std::vector<TrialSource> sources{
    {"v5-row-order-replay-XXxsGfyg",
     "d72e461d2412510f8038b66da5b415716e7e5dae32cc9da5926498f8529a3172",
     "requests", "fixed"},
    {"v5-group-order-replay-XX3MOjcy",
     "98f56631d8f5d01d08626de37185a7ddec55104a04b25e7d1dd36f22ec976858",
     "requests", "fixed"},
    {"v5-stock-state-replay-XXXUqNGd",
     "fff7b885199baab41c40f99930a5309b7c15a6b288b1168ecfc6aed636abdf18",
     "requests", "fixed"},
    {"v5-sensor-conflicts-replay-XXTajrlo",
     "b84c134e71f4648edb3a216ae1913aca8b1a3925bf640834b48a77ef3f4b06da", "",
     "fixed", "H039"},
    {"v5-xml-chain-replay-XXOSWDDX",
     "34cbcd6b6d6468552f329fa07be9ad425ce678a4d3b4fab4661705a5003b02ca",
     "requests", "fixed"},
    {"v5-zip-chain-replay-XXRbxzNk",
     "5aca58c1978024d66108845b5878dacc1e61a82fbdf73b61af90a697c8ae0f59",
     "requests", "fixed"},
    {"v5-directory-count-replay-XXsh0ql9",
     "d44f81b63da9b8a63ffe2f8cf0fafb423dc3b7a456bfe4ed804f9d1c7233b1a2", "",
     "fixed", "H063"},
    {"v5-depot-count-replay-XXH1aBvL",
     "43ecbda98897f4c062a007c85c3f571ad5658f4200937442ed2f0791bbd9a3fa", "",
     "fixed", "H030"},
    {"v5-uri-fragment-replay-XXYIY5y0",
     "5fafe828f67a03950e21531bb213efdc7ac73de7be29dd76caec55226c053d85", "",
     "fixed", "H080"},
    {"v5-unordered-finance-replay-XXvu2pML",
     "20ab431f685db586b3fd07fcfb1d1a7e5bc0dba8727a14642aa7340ba5d3b252",
     "finance", "reference"},
    {"v5-unordered-bookings-replay-XXYYEdHA",
     "572080fb776ebe8efe03ef9ecf7d11ce6098d95ffe7c77009c2376fa6144faf3",
     "bookings", "reference"},
    {"v5-unordered-dispatch-replay-XX8yrLPh",
     "7a64c0e8c5ede30e57d6b46bd49bfa4d4f6bdbb5dcaeac494b696aa223e7a2e4",
     "dispatch", "seq"},
    {"v5-unordered-scheduling-replay-XXZQnfDy",
     "77ebec07db24b39e8ff66a90604579be52cc0de87124ef495023078f4129c555",
     "scheduling", "reference"},
    {"v5-reachability-replay-XXxyKVfL",
     "45a5750d826899e2edaf056637d096418cde2ff389d8d9690aebd0800a511064",
     "requests", "fixed", "H086"},
    {"v5-unordered-telemetry-replay-XXj6C6EH",
     "13e1d9667144ded4a2526a9fe78e1b12599c66ff41344914b14607e68c4ef1d6",
     "telemetry", "reference"},
    {"v5-unordered-singletons-replay-XXwTruXp",
     "216acec42166d7aaa5c7aa15268600add9da1bd8b0cb518129221ec4cd0b0fea",
     "singletons", "reference"},
    {"v5-residual-directory-replay-XXHTdjz7",
     "bace72893bc599004a2d0564c058213083257d1f396760bf565d21cc72282b68",
     "directories", "reference"},
    {"v5-residual-zip-replay-XXRDOTGr",
     "bc1d40444fd5e431806629b915754899ee9f8a172dc7526d582e26ab502eda6f",
     "archives", "reference"},
    {"v5-residual-uri-replay-XXNy7n6G",
     "15dda75f1b9ec6f40dead0e5c304ce6693f92bb14d423a1574323ecab5b6ec70", "uri",
     "reference"},
    {"v5-residual-closure-replay-XXPB76Hi",
     "0f4005ba7e35108d88e20c6c67809d038454dc8546cd36813d6f62213da2edf0",
     "requests", "reference"},
    {"v5-path-replay-XXSSPzLT",
     "897d493da10f1982ec058fbc8bc2f4587049fc8cd81658234cb9f757aeda2db8",
     "requests", "correlated"},
    {"v5-typed-closure-replay-XXnzoeMp",
     "06893b54b4d0d56431ea5c40cba6bdd5a516721313b3f09ad62748f4288476ca",
     "cohort", ""}};

class Inputs {
  J pins_ = object();

public:
  std::string pin(const Path &file, std::string_view expected = {}) {
    const auto actual = hash(file);
    check(expected.empty() || actual == expected,
          "changed input " + file.string());
    pins_[file.string()] = actual;
    return actual;
  }

  J json(const Path &file, std::string_view expected = {}) {
    pin(file, expected);
    return load(file);
  }

  const J &pins() const { return pins_; }
};

struct Trial {
  Path dir, report;
  J proof;
};

std::map<std::string, Trial> selected_trials(Inputs &inputs) {
  std::map<std::string, Trial> trials;
  for (const auto &source : sources) {
    const auto dir = runs / source.directory;
    const auto file = dir / "report.json";
    const auto report = inputs.json(file, source.pin);
    const auto ids = source.id.empty() ? report[source.section].getMemberNames()
                                       : Strings{source.id};
    for (const auto &id : ids) {
      if (id == "H086" && source.section == "scheduling")
        continue; // Use the original receipt referenced by the scheduling
                  // cohort.
      const auto &entry =
          source.section.empty() ? report : report[source.section][id];
      Path path = dir / source.arm / id;
      auto proof = entry["runs"][source.arm];
      if (source.arm.empty()) {
        proof = entry;
        path = dir / entry["path"].asString();
      } else if (source.directory == "v5-reachability-replay-XXxyKVfL") {
        proof = entry["arms"][source.arm];
      } else if (source.arm == "correlated") {
        proof = entry[source.arm];
      }
      check(proof["receipt_sha256"].isString() &&
                trials.emplace(id, Trial{path, file, proof}).second,
            "missing or duplicate supplied receipt " + id + " in " +
                file.string());
    }
  }
  return trials;
}

using Binding = std::tuple<std::string, std::string, std::string>;
using Usage = std::map<Binding, std::set<std::string>>;
using Registry = std::map<Binding, J>;

Binding binding(const J &step) {
  auto name = step["provider"].asString();
  const auto variant_end = name.find('_', 3);
  if (name.starts_with("op_") && variant_end != std::string::npos &&
      variant_end > 3 && name.find_first_not_of("0123456789", 3) == variant_end)
    name.erase(0, variant_end + 1);
  check(!name.empty(), "emission has no selected binding");
  return {name, step["from"].asString(), step["to"].asString()};
}

std::string binding_digest(const J &row) {
  Json::StreamWriterBuilder canonical;
  canonical["indentation"] = "";
  canonical["emitUTF8"] = true;
  canonical["commentStyle"] = "None";
  return sha(Json::writeString(canonical, row));
}

J rank(const Usage &usage, const Registry &registry = {}) {
  std::vector<J> rows;
  for (const auto &[key, ids] : usage) {
    const auto &[id, from, to] = key;
    auto row = object({{"request_count", ids.size()},
                       {"requests", string_array({ids.begin(), ids.end()})}});
    if (registry.empty()) {
      row["outcome"] = id;
    } else {
      const auto &registered = registry.at(key);
      row["binding_id"] = id;
      row["from"] = from;
      row["to"] = to;
      row["component_id"] = registered["brick"];
      row["binding_sha256"] = binding_digest(registered);
    }
    rows.push_back(std::move(row));
  }
  std::stable_sort(rows.begin(), rows.end(), [](const auto &a, const auto &b) {
    return a["request_count"].asUInt() > b["request_count"].asUInt();
  });
  auto result = array();
  for (const auto &row : rows)
    result.append(row);
  return result;
}

std::string stop(const J &plan, const J &result) {
  if (!result["interpretation_errors"].empty())
    return "invalid interpretation";
  bool prose = false;
  for (const auto &role : plan["providers"]) {
    if (!role["typed_gaps"].empty())
      return "typed conversion gap";
    prose |= !role["uncovered"].empty();
  }
  if (!plan["gaps"].empty())
    return std::all_of(plan["gaps"].begin(), plan["gaps"].end(),
                       [](const auto &g) { return g == no_outcomes.c_str(); })
               ? "no ordered outcomes"
               : "no complete chain for the requested operations";
  check(prose, "unexplained archived stop");
  return "uncovered obligations without a typed gap";
}

std::map<std::string, Strings> tsv(Inputs &inputs, const Path &file,
                                   std::string_view pin, size_t width) {
  inputs.pin(file, pin);
  std::istringstream rows(bytes(file));
  std::string line;
  std::getline(rows, line);
  std::map<std::string, Strings> entries;
  while (std::getline(rows, line)) {
    std::istringstream row(line);
    Strings fields;
    for (std::string value; std::getline(row, value, '\t');)
      fields.push_back(value);
    check(fields.size() == width &&
              entries.emplace(fields.front(), fields).second,
          "bad or repeated TSV row in " + file.string());
  }
  return entries;
}

void verified_scores(const J &values) {
  check(values.size() == splits.size() &&
            std::all_of(values.begin(), values.end(),
                        [](const auto &v) { return v == true; }),
        "supplied request has a failing fixture");
}
} // namespace

int main() try {
  Inputs inputs;
  const auto ranking = inputs.json(
      runs / "ranked-gaps-5x2_hyp7/ranking.json",
      "3d902a86f77c7159581a1721cf07c96ccd7b0147259ffdee6edce8f976119495");
  const auto recipes = inputs.json(
      root / "shared/request-recipes.json",
      "768676cbc34ef7db9d869520bd3d26d86123f49a3c9803901eb5653de3a6f49b");
  const auto summary = tsv(
      inputs, root / "row-ops/english-construction.tsv",
      "e5a279aa3726cb40822ddd0aa2c39ce669abd8f7f247989569f795bd5390a7e8", 26);
  const auto mapping = tsv(
      inputs, root / "row-ops/request-map.tsv",
      "727915058868a654ad744c4ebfbfe47f04becc65f1f21ad16512585e5faf5f80", 3);
  const auto original = runs / "shared-review-5ui86km8/transfer";
  const auto reference = inputs.json(
      original / "transfer.json",
      "65ffe2964958ee9002ebc7bf7fbbd20291300ec9d19d02d4e5e1ffe28df6d536");
  const auto earlier_typed = inputs.json(
      runs / "v5-typed-reconciliation-XXVKXZxL/report.json",
      "3f6359bb7836418e0f3d6356e12275cfd600d57078869db528741cadb6cb2f8e");
  const auto active = fs::canonical(volume / "unllm/boilerplate/active");
  const auto active_pin = inputs.pin(
      active / "SHA256SUMS",
      "e4564e3079f0b32b3379a411b170ce9af61ec80ab7938b6a974e724abd538328");
  const auto staged = runs / "v5-tsv-writer-XXAf0x0y/stage/bundle";
  verify_bundle(
      staged,
      "b2f8a40d44b1589779fc82fcfa23995df5cb2b2432167e9383f6f22e9c828d54");
  inputs.pin(staged / "SHA256SUMS");
  Registry retained;
  const auto catalog = inputs.json(staged / "providers/contracts.json");
  for (const auto &row : catalog["providers"])
    check(retained
              .emplace(Binding{row["id"].asString(), row["from"].asString(),
                               row["to"].asString()},
                       row)
              .second,
          "ambiguous retained binding identity");
  const auto trials = selected_trials(inputs);
  std::set<std::string> seen;
  auto requests = object(), counts = object(), format_changes = array(),
       input_corrections = array(), observed_variants = object();
  Usage used, omitted;
  unsigned mapped = 0, fresh = 0, saved_roles = 0, archive_pass = 0,
           archive_checked = 0;
  const auto archive = runs / "english-audit-bcsgf8pi/replay-staged";
  for (const auto &reason : ranking["stops"].getMemberNames()) {
    for (const auto &id_value : ranking["stops"][reason]) {
      const auto id = id_value.asString();
      check(seen.insert(id).second, "blocker cohorts overlap " + id);
      const auto historical = archive / id;
      const auto old = inputs.json(historical / "receipt.json");
      const auto old_spec = inputs.json(historical / response);
      const auto old_plan = inputs.json(historical / emission);
      const auto old_status = inputs.json(historical / "run/result.json");
      const auto menu =
          inputs.json(historical / "run/vocabulary.json")["outcomes"];
      inputs.pin(historical / "request.txt");
      check(old["request"] == id_value && old["verified"] == false &&
                old["editor"] == false && stop(old_plan, old_status) == reason,
            "saved receipt disagrees with the ranked blocker " + id);
      auto historical_verdicts = object();
      for (const auto &split : splits) {
        const auto &score =
            old[split == "transfer" || split == "sparse" ? "held" : "initial"]
               [split];
        check(score.isNull() || score.isBool(), "invalid archived score " + id);
        historical_verdicts[split] = score;
        archive_checked += score.isBool();
        archive_pass += score.isBool() && score.asBool();
      }

      const auto &trial = trials.at(id);
      const auto receipt = inputs.json(
          trial.dir / "receipt.json", trial.proof["receipt_sha256"].asString());
      const auto &old_writer =
          earlier_typed["requests"]["H029"]["retained_execution"];
      const auto spec_pin =
          (id == "H029" ? old_writer : receipt)["spec_sha256"].asString();
      const auto bundle_pin =
          (id == "H029" ? old_writer["catalog_sha256"] : receipt["manifest"])
              .asString();
      check(!spec_pin.empty() && !bundle_pin.empty(),
            "missing supplied specification or bundle pin " + id);
      const auto supplied = inputs.json(trial.dir / response, spec_pin);
      const auto plan = inputs.json(trial.dir / emission,
                                    trial.proof["plan_sha256"].asString());
      const auto local =
          inputs.json(trial.dir / "run/components/contracts.json");
      check(receipt["request"] == id_value && receipt["verified"] == true &&
                receipt["accepted"] == true && receipt["editor"] == false &&
                receipt["interpret_model"].isNull(),
            "supplied execution or isolation failed " + id);
      verified_scores(scores(receipt));
      check(reference[id]["passed"] == true &&
                reference[id]["checks"].size() == splits.size(),
            "missing original supplied reference " + id);
      for (const auto &split : splits)
        check(reference[id]["checks"][split]["pass_"] == true,
              "original recipe failed a fixture " + id + "/" + split);
      const auto roles = old_spec["roles"].size();
      const bool csv_correction = id >= "H011" && id <= "H020" &&
                                  old_spec["input_type"] == "text/plain" &&
                                  supplied["input_type"] == "text/csv";
      check(roles == plan["providers"].size() &&
                roles == supplied["roles"].size() &&
                roles == recipes[id].size() &&
                roles == reference[id]["roles"].size() &&
                (old_spec["input_type"] == supplied["input_type"] ||
                 csv_correction),
            "supplied input or role count differs " + id);
      if (csv_correction)
        input_corrections.append(id);
      auto &request = requests[id];
      request = object(
          {{"archived_request_file", (historical / "request.txt").string()},
           {"request", bytes(historical / "request.txt")},
           {"blocker", reason},
           {"archived_spec", old_spec},
           {"archived_verdicts", historical_verdicts},
           {"archived_validation", old_plan["interpretation_errors"]},
           {"archived_planner_gaps", old_plan["gaps"]},
           {"row_map", mapping.at(id)[1]},
           {"row_map_residual", mapping.at(id)[2]},
           {"supplied_intent_notes", summary.at(id)[3]},
           {"supplied_run", trial.dir.string()},
           {"evidence_report", trial.report.string()},
           {"supplied_bundle_sha256", bundle_pin},
           {"supplied_scores", scores(receipt)},
           {"supplied_input_type", supplied["input_type"]}});
      mapped += mapping.at(id)[1] != "-";
      counts[reason] = counts[reason].asUInt() + 1;
      saved_roles += roles;
      auto seen_slots = std::set<unsigned>{};
      for (const auto &emitted : plan["providers"]) {
        const auto slot = emitted["slot"].asUInt();
        check(slot < roles && seen_slots.insert(slot).second &&
                  complete(emitted),
              "invalid supplied role emission " + id);
        const auto &old_role = old_spec["roles"][slot];
        const auto &role = supplied["roles"][slot];
        const auto &req = role["requirement"];
        check(role["id"] == old_role["id"] &&
                  role["output_type"] == old_role["output_type"] &&
                  req["operations"] == operations(recipes[id][slot]) &&
                  req["uncovered"].empty(),
              "supplied recipe or output changed " + id + " slot " +
                  std::to_string(slot));
        const auto historical_plan = inputs.json(
            original / id / std::to_string(slot) / "components/emission.json");
        const auto &historic_steps =
            historical_plan["providers"][0]["plan"]["steps"];
        check(!historic_steps.empty(),
              "missing original emitted reference route " + id);
        const auto &historic_sink =
            historic_steps[historic_steps.size() - 1]["to"];
        const auto &steps = emitted["plan"]["steps"];
        check(!steps.empty() && steps[steps.size() - 1]["to"].asString() ==
                                    "out:" + role["output_type"].asString(),
              "supplied emitter produced a different output port " + id);
        if (trial.proof["plan_sha256"].isNull())
          check((id == "H058" || id == "H070") &&
                    trial.proof["providers"][slot]["steps"] == steps,
                "retained path evidence lacks the emitted steps " + id);
        if (historic_sink != steps[steps.size() - 1]["to"])
          format_changes.append(
              object({{"request", id},
                      {"role", role["id"]},
                      {"original_reference_port", historic_sink},
                      {"replay_port", steps[steps.size() - 1]["to"]}}));
        for (const auto &step : steps) {
          const auto key = binding(step);
          const auto &registered = retained.at(key);
          const auto &providers = local["providers"];
          const auto actual = std::find_if(
              providers.begin(), providers.end(), [&](const auto &row) {
                return row["id"] == registered["id"] &&
                       row["from"] == registered["from"] &&
                       row["to"] == registered["to"];
              });
          check(actual != providers.end(), "missing local binding " + id);
          for (const auto *field :
               {"brick", "call", "header", "effect", "args"})
            check((*actual)[field] == registered[field],
                  "local binding changes " + std::string(field) + " for " + id);
          if (*actual != registered) {
            auto &variant = observed_variants[binding_digest(*actual)];
            variant["row"] = *actual;
            variant["staged_binding_sha256"] = binding_digest(registered);
            variant["requests"][id] = true;
          }
          used[key].insert(id);
        }
        auto absent = array();
        for (const auto &op : req["operations"]) {
          const auto outcome = op["outcome"].asString();
          if (!menu.isMember(outcome)) {
            absent.append(outcome);
            omitted[{outcome, "", ""}].insert(id);
          }
        }
        auto archived_ports = array();
        for (const auto &entry : old_plan["providers"])
          if (entry["slot"].asUInt() == slot)
            archived_ports = ports(entry);
        request["roles"][role["id"].asString()] =
            object({{"output_type", role["output_type"]},
                    {"archived_typed_ports", archived_ports},
                    {"original_reference_output_port", historic_sink},
                    {"supplied_operations", req["operations"]},
                    {"supplied_steps", steps},
                    {"supplied_operations_absent_from_archived_menu", absent}});
      }
      if (summary.at(id)[4] == "unmeasured") {
        request["fresh_english"] = object({{"status", "unmeasured"}});
      } else {
        const Path file = summary.at(id)[24];
        const auto observation = inputs.json(file);
        check((id == "H004" || id == "H010") &&
                  observation["request"] == id_value &&
                  observation["interpret_model"].isString() &&
                  observation["editor"] == false &&
                  observation["verified"] == false &&
                  summary.at(id)[4] == "0/5",
              "unexpected fresh English receipt " + id);
        const auto verdicts = scores(observation);
        for (const auto &score : verdicts)
          check(score == false, "fresh English scored differently " + id);
        request["fresh_english"] =
            object({{"status", observation["status"]},
                    {"receipt", file.string()},
                    {"verdicts", verdicts},
                    {"spec", inputs.json(file.parent_path() / response)}});
        ++fresh;
      }
    }
  }
  check(seen.size() == 100 && trials.size() == seen.size() &&
            mapping.size() == seen.size() && summary.size() == seen.size() &&
            recipes.size() == seen.size() && mapped == 71 && fresh == 2 &&
            input_corrections.size() == 10 && archive_checked == 395 &&
            archive_pass == 27 && hash(active / "SHA256SUMS") == active_pin,
        "incomplete request partition or changed active catalog");
  const auto fifo_probe = inputs.json(
      runs / "v5-unordered-finance-replay-XXvu2pML/plans/H011/direct-fifo/"
             "components/emission.json",
      "9221d5fbc13677e5f14eaaaf3928780e4db3532f562115557a63763ef2459ba5");
  const auto fifo_port = array({"integers", "out:text/plain"});
  check(fifo_probe["providers"].size() == 1 &&
            fifo_probe["providers"][0]["status"] == "partial" &&
            ports(fifo_probe["providers"][0]) == array({fifo_port}),
        "changed H011 direct FIFO control");
  const auto optional_gaps =
      array({object({{"request", "H011"},
                     {"request_count", 1},
                     {"missing_port", fifo_port},
                     {"scope", "alternative direct FIFO route"},
                     {"supplied_route_verified", true}})});
  const auto report = object(
      {{"schema", "unllm.archived-construction-audit/1"},
       {"archived_first_blockers", counts},
       {"archived_typed_ranking", ranking["typed"]},
       {"archived_validation_ranking", ranking["validation_parameters"]},
       {"archived_fixture_verdicts", archive_checked},
       {"archived_fixture_passes", archive_pass},
       {"row_map_mapped", mapped},
       {"row_map_unmapped", seen.size() - mapped},
       {"fresh_english_requests_tested", fresh},
       {"fresh_english_requests_passing", 0},
       {"verified_supplied_requests", seen.size()},
       {"verified_supplied_roles", saved_roles},
       {"verified_supplied_fixture_passes", seen.size() * splits.size()},
       {"known_remaining_implementation_gaps_in_supplied_scope", array()},
       {"optional_alternative_port_gaps", optional_gaps},
       {"input_type_corrected_to_csv", input_corrections},
       {"output_ports_changed_from_original_reference", format_changes},
       {"supplied_binding_usage", rank(used, retained)},
       {"observed_binding_metadata_variants", observed_variants},
       {"absent_archived_menu_outcomes", rank(omitted)},
       {"requests", requests},
       {"file_sha256", inputs.pins()}});
  const auto work = fresh_run("v5-coverage-audit");
  write(work / ".report.json", report);
  run({"sync", "-f", work});
  fs::rename(work / ".report.json", work / "report.json");
  std::cout << work.string() << '\n' << hash(work / "report.json") << '\n';
  return 0;
} catch (const std::exception &e) {
  std::cerr << e.what() << '\n';
  return 1;
}
