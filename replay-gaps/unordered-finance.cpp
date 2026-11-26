#include "row-trial.hpp"
#include <algorithm>
#include <iostream>
#include <set>

using namespace miner;
using namespace row_replay;
namespace {
const std::string csv = "text/csv";
const std::string plain = "text/plain";
const std::string tsv = "text/tab-separated-values";
const Path response = "run/calls/interpret/response.json";
const Path emission = "run/components/emission.json";
const std::string empty_stop = "Expected between one and 32 ordered outcomes";
const Strings splits{"ordinary", "boundary", "empty", "transfer", "sparse"};
const std::string signed_amounts = R"(side,ref,cents
bank,z,-0
book,z,0
bank,z,00
bank,t,-7
book,t,-7
bank,t,-7
bank,a,2
book,a,2
book,a,2
)";

using Occurrences = std::map<std::string, std::set<std::string>>;

std::string family(const std::string &id) {
  const Strings families{"inventory",    "finance",    "parcel dispatch",
                         "telemetry",    "publishing", "archives",
                         "directories",  "URI lists",  "build scheduling",
                         "room bookings"};
  return families.at((std::stoi(id.substr(1)) - 1) / 10);
}

J ranked(const Occurrences &occurrences) {
  std::vector<J> entries;
  for (const auto &[key, ids] : occurrences) {
    auto requests = array();
    for (const auto &id : ids)
      requests.append(id);
    entries.push_back(object(
        {{"name", key}, {"count", requests.size()}, {"requests", requests}}));
  }
  std::sort(entries.begin(), entries.end(), [](const J &a, const J &b) {
    if (a["count"] != b["count"])
      return a["count"].asUInt() > b["count"].asUInt();
    return a["name"].asString() < b["name"].asString();
  });
  auto result = array();
  for (const auto &entry : entries)
    result.append(entry);
  return result;
}

J with_operations(J spec, const J &steps) {
  spec["roles"][0]["requirement"]["operations"] = steps;
  spec["roles"][0]["requirement"]["uncovered"] = array();
  return spec;
}

void expect_empty(const J &probe, const std::string &id) {
  check(probe["gaps"]["r"] == empty_stop && probe["routes"].empty(),
        "changed empty-operation diagnosis for " + id + ": " + dump(probe));
}

bool finished(const J &probe) {
  return probe["gaps"].empty() && probe["routes"].size() == 1 &&
         probe["routes"][0]["status"] == "complete";
}

J normalize_amount(const std::string &id, const J &steps, const J &derive) {
  auto result = array({steps[0], derive});
  for (unsigned i = 1; i < steps.size(); ++i) {
    auto step = steps[i];
    auto &p = step["parameters"];
    const auto effect = step["outcome"].asString();
    if (effect == "rank_records" || effect == "annotate_group_measure" ||
        effect == "group_rows")
      p["keys"] = "1,3";
    if (effect == "filter_records" && p["column"] != 0)
      p["column"] = p["column"].asUInt() + 1;
    if (effect == "order_records")
      p["order_by"] = "1:text:asc,3:int:asc";
    if (id == "H020" && effect == "report_fields")
      p["fields"] = "1,3";
    result.append(step);
  }
  return result;
}
} // namespace

int main() try {
  const auto stage = runs / "v5-queue-XXH5XMi9/stage/package";
  const auto writer_stage = runs / "v5-tsv-writer-XXAf0x0y/stage";
  const auto base = stage / "bundle", with_tsv = writer_stage / "bundle";
  const auto parent_pin = hash(stage / "manifest.json");
  const auto writer_pin = hash(writer_stage / "manifest.json");
  check(parent_pin == "d15aee96917ff440f58f76270e3ca48575268dcfcf2bce951805c865"
                      "aae3af63" &&
            writer_pin == "0047b47009df08f7ee228729eb46b4438ec9d59eadd7c44647ec"
                          "6761eeabb994",
        "changed comparison stages");
  verify_bundle(
      base, "436db92629463daee559de2cbbc1c81519e12306d5bf01cc6d2883f3140e779c");
  verify_bundle(
      with_tsv,
      "b2f8a40d44b1589779fc82fcfa23995df5cb2b2432167e9383f6f22e9c828d54");
  const auto writer =
      exact_row(with_tsv, "write_tsv_lines", "strings", "out:" + tsv);
  check(remove_binding(read_tree(with_tsv), writer).at("SHA256SUMS") ==
            bytes(base / "SHA256SUMS"),
        "comparison differs outside the TSV writer");
  const auto active = fs::canonical(volume / "unllm/boilerplate/active");
  const auto active_pin = hash(active / "SHA256SUMS");
  check(active_pin ==
            "e4564e3079f0b32b3379a411b170ce9af61ec80ab7938b6a974e724abd538328",
        "changed published context");

  const auto ranking = runs / "ranked-gaps-5x2_hyp7/ranking.json";
  const auto recipe = root / "shared/request-recipes.json";
  const auto ranking_pin = hash(ranking), recipe_pin = hash(recipe);
  check(ranking_pin == "3d902a86f77c7159581a1721cf07c96ccd7b0147259ffdee6edce8f"
                       "976119495" &&
            recipe_pin == "768676cbc34ef7db9d869520bd3d26d86123f49a3c9803901eb5"
                          "653de3a6f49b",
        "changed cohort or reference recipes");
  const auto cohort = load(ranking)["stops"]["no ordered outcomes"];
  const auto recipes = load(recipe);
  const auto archive = runs / "english-audit-bcsgf8pi/replay-staged";
  const auto original =
      volume / "unllm/experiments/boilerplate-B42-VrtYbHSf/fresh-staged";
  const auto older = volume / "unllm/experiments/end-user-100-20261123";
  const auto provider_rows = load(base / "providers/contracts.json");
  std::set<std::string> retained;
  for (const auto &row : provider_rows["providers"])
    if (row["effect"].isString())
      retained.insert(row["effect"].asString());

  const auto work = fresh_run("v5-unordered-finance-replay");
  std::cout << work.string() << std::endl;
  auto report = object(
      {{"active_path", active.string()},
       {"active_sha256", active_pin},
       {"parent_manifest_sha256", parent_pin},
       {"writer_manifest_sha256", writer_pin},
       {"ranking_sha256", ranking_pin},
       {"recipes_sha256", recipe_pin},
       {"reader", exact_row(base, "read_csv_records", "in:" + csv, "rows")},
       {"writer", writer},
       {"tools", tools()}});
  report["tools"]["unllm_logic_worker"] =
      hash(main_repo / "build/native/unllm_logic_worker");
  Occurrences families, media, reference, absent_from_original_menu,
      absent_from_replay_menu, absent_from_base;
  for (const auto &r : cohort) {
    const auto id = r.asString();
    const auto dir = archive / id;
    const auto saved = load(dir / response);
    const auto &role = saved["roles"][0];
    const auto replay_vocabulary = dir / "run/vocabulary.json";
    const auto replay_menu = load(replay_vocabulary)["outcomes"];
    const auto plan = load(dir / emission);
    const auto saved_pin = hash(dir / response);
    const auto primary = original / id / response;
    const auto source_dir = (fs::exists(primary) ? original : older) / id;
    const auto source = source_dir / response;
    const auto original_vocabulary = source_dir / "run/vocabulary.json";
    const auto original_menu = load(original_vocabulary)["outcomes"];
    check(saved["roles"].size() == 1 &&
              role["requirement"]["operations"] == array() &&
              plan["gaps"][role["id"].asString()] == empty_stop &&
              saved_pin == hash(source) && original_menu.isObject() &&
              replay_menu.isObject(),
          "changed archived context for " + id);
    families[family(id)].insert(id);
    media[saved["input_type"].asString()].insert(id);
    std::set<std::string> effects;
    for (const auto &slot : recipes[id])
      for (const auto &step : slot)
        effects.insert(step[0].asString());
    auto original_omissions = array(), replay_omissions = array();
    for (const auto &effect : effects) {
      reference[effect].insert(id);
      if (!original_menu.isMember(effect)) {
        absent_from_original_menu[effect].insert(id);
        original_omissions.append(effect);
      }
      if (!replay_menu.isMember(effect)) {
        absent_from_replay_menu[effect].insert(id);
        replay_omissions.append(effect);
      }
      if (!retained.contains(effect))
        absent_from_base[effect].insert(id);
    }
    report["archive"][id] = object(
        {{"reply_sha256", saved_pin},
         {"reply_source", source.string()},
         {"request_sha256", hash(dir / "request.txt")},
         {"plan_sha256", hash(dir / emission)},
         {"receipt_sha256", hash(dir / "receipt.json")},
         {"original_menu_sha256", hash(original_vocabulary)},
         {"replay_menu_sha256", hash(replay_vocabulary)},
         {"input_type", saved["input_type"]},
         {"output_type", role["output_type"]},
         {"reference_effects_missing_in_original_menu", original_omissions},
         {"reference_effects_missing_in_replay_menu", replay_omissions}});
  }
  check(cohort.size() == 32 && families["finance"].size() == 9 &&
            families["room bookings"].size() == 7,
        "changed cohort frequency");
  report["family_frequency"] = ranked(families);
  report["input_mime_frequency"] = ranked(media);
  report["reference_effect_frequency"] = ranked(reference);
  report["reference_effect_absent_from_original_menu"] =
      ranked(absent_from_original_menu);
  report["reference_effect_absent_from_replay_menu"] =
      ranked(absent_from_replay_menu);
  report["reference_effect_absent_from_parent"] = ranked(absent_from_base);
  write(work / "progress.json", report);

  const auto derive = operations(recipes["H011"][0])[1];
  const auto historical = runs / "shared-review-5ui86km8/transfer";
  std::vector<Path> programs;
  for (const auto &id : families.at("finance")) {
    const auto dir = archive / id;
    const auto saved = load(dir / response);
    const auto output = saved["roles"][0]["output_type"].asString();
    const auto steps = operations(recipes[id][0]);
    check(saved["input_type"] == plain &&
              output == (id == "H011" ? plain : tsv) &&
              steps[0]["outcome"] == "read_csv_records" &&
              steps[0]["parameters"] == object({{"header", "present"}}),
          "changed finance formats for " + id);
    auto &entry = report["finance"][id];
    auto probe = [&](const std::string &arm, const Path &bundle,
                     const J &spec) {
      auto result = plan_probe(bundle, work / "plans" / id / arm, spec);
      entry["plans"][arm] = result;
      return result;
    };
    expect_empty(probe("archived", base, saved), id);
    auto csv_only = saved;
    csv_only["input_type"] = csv;
    expect_empty(probe("csv-only", base, csv_only), id);
    const auto &bundle = output == tsv ? with_tsv : base;
    const auto wrong_mime =
        probe("operations-only", bundle, with_operations(saved, steps));
    check(!finished(wrong_mime),
          "plain input completed CSV operations for " + id);
    const auto typed = with_operations(csv_only, steps);
    if (output == tsv) {
      const auto missing_sink = probe("without-writer", base, typed);
      check(missing_sink["validation"].empty() &&
                missing_sink["routes"].size() == 1 &&
                missing_sink["routes"][0]["ports"] ==
                    array({array({"strings", "out:" + tsv})}),
            "writer ablation changed another port for " + id + ": " +
                dump(missing_sink));
    }
    if (id == "H011") {
      const auto fifo = exact_row(base, "reconcile", "rows", "integers");
      const auto direct = with_operations(
          csv_only,
          array({steps[0],
                 object({{"outcome", fifo["effect"]},
                         {"parameters", object({{"reference_column", 1},
                                                {"amount_column", 2},
                                                {"side_column", 0},
                                                {"first_side", "book"}})}})}));
      const auto gap = probe("direct-fifo", base, direct);
      check(gap["validation"].empty() && gap["routes"].size() == 1 &&
                gap["routes"][0]["ports"] ==
                    array({array({"integers", "out:" + plain})}),
            "direct FIFO did not reach the integer writer boundary: " +
                dump(gap));
      entry["direct_fifo_binding"] = fifo;
    }

    auto inputs = object();
    const auto fixtures = historical / id / "fixtures";
    for (const auto &split : splits)
      inputs[split] = hash(fixtures / split / "input/ledger.csv");
    entry["fixture_input_sha256"] = inputs;
    auto replay = [&](const std::string &arm, const J &spec) {
      const auto file = work / "specs" / arm / (id + ".json");
      write(file, spec);
      auto result = trial(work, arm, id, bundle, file, "ledger.csv", inputs);
      const auto exe = work / arm / id / "run/project/converter";
      programs.push_back(exe);
      entry["runs"][arm] = result;
      write(work / "progress.json", report);
      check(result["verified"] == true && complete(result["roles"]["0"]) &&
                selects(result["roles"]["0"], "read_csv_records") &&
                selects(result["roles"]["0"],
                        output == tsv ? "write_tsv_lines" : "write_lines"),
            "incomplete finance replay for " + id);
      for (const auto &split : splits)
        check(result["scores"][split] == true &&
                  result["outputs"][split][0] ==
                      bytes(fixtures / split / "output/result-1.txt"),
              "saved fixture mismatch for " + id + " " + split);
      return exe;
    };
    const auto exe = replay("reference", typed);
    if (id == "H011" || id == "H013" || id == "H020") {
      const auto raw = id == "H011"   ? "3\n6\n9\n"
                       : id == "H013" ? "6\n"
                                      : "a\t2\nt\t-7\n";
      const auto expected = id == "H011"   ? raw
                            : id == "H013" ? "3\n6\n"
                                           : "a\t2\nt\t-7\nz\t0\n";
      entry["signed_amount_expected"] = expected;
      entry["reference_witness"] =
          extra_case(work / "witness" / id / "reference", exe, bundle,
                     signed_amounts, array({raw}));
      check(entry["reference_witness"]["passed"] == true,
            "unexpected reference result on signed amounts for " + id + ": " +
                dump(entry["reference_witness"]));
      if (id != "H011") {
        const auto fixed =
            with_operations(typed, normalize_amount(id, steps, derive));
        const auto corrected = replay("numeric-keys", fixed);
        entry["numeric_witness"] =
            extra_case(work / "witness" / id / "numeric-keys", corrected,
                       bundle, signed_amounts, array({expected}));
        check(entry["numeric_witness"]["passed"] == true,
              "incorrect normalized result on signed amounts for " + id + ": " +
                  dump(entry["numeric_witness"]));
      }
    }
    write(work / "progress.json", report);
  }
  report["signed_amount_fixture_sha256"] = sha(signed_amounts);
  check(hash(active / "SHA256SUMS") == active_pin, "published context changed");
  write(work / ".report.json", report);
  run({"sync", "-f", work});
  for (const auto &exe : programs)
    check(fs::remove(exe), "could not retire " + exe.string());
  fs::rename(work / ".report.json", work / "report.json");
  std::cout << hash(work / "report.json") << std::endl;
  return 0;
} catch (const std::exception &e) {
  std::cerr << e.what() << '\n';
  return 1;
}
