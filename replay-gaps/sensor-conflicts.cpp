#include "row-trial.hpp"
#include <algorithm>
#include <iostream>

using namespace miner;
using namespace row_replay;
namespace {
const std::string id = "H039";
const std::string tsv = "text/tab-separated-values";
const Strings splits{"ordinary", "boundary", "empty", "transfer", "sparse"};
const std::string selector = R"(["sensor", "room", "minute", "temp"])";
const std::string interleaved = R"([
  {"sensor":"B","room":"L","minute":2,"temp":5},
  {"sensor":"A","room":"L","minute":10,"temp":-10},
  {"sensor":"C","room":"R","minute":2,"temp":5},
  {"sensor":"A","room":"R","minute":2,"temp":3},
  {"sensor":"A","room":"L","minute":10,"temp":-2},
  {"sensor":"A","room":"L","minute":2,"temp":-4},
  {"sensor":"B","room":"L","minute":2,"temp":5},
  {"sensor":"A","room":"L","minute":3,"temp":29},
  {"sensor":"B","room":"L","minute":10,"temp":7},
  {"sensor":"B","room":"R","minute":10,"temp":0},
  {"sensor":"C","room":"L","minute":2,"temp":5},
  {"sensor":"D","room":"L","minute":3,"temp":1},
  {"sensor":"D","room":"R","minute":4,"temp":9}
])";
const std::string signed_zero = R"([
  {"sensor":"Z","room":"L","minute":-0,"temp":-8},
  {"sensor":"Z","room":"R","minute":0,"temp":7},
  {"sensor":"Y","room":"L","minute":-0,"temp":-3},
  {"sensor":"Y","room":"L","minute":0,"temp":-3},
  {"sensor":"Z","room":"L","minute":1,"temp":8},
  {"sensor":"C","room":"L","minute":-0,"temp":2},
  {"sensor":"C","room":"L","minute":-0,"temp":3}
])";

std::string numeric_oracle(const std::string &json) {
  using Key = std::pair<std::string, Json::Int64>;
  using Extremes = std::pair<Json::Int64, Json::Int64>;
  std::map<Key, Extremes> groups;
  const auto input = parse(json);
  for (const auto &r : input) {
    check(r["minute"].isInt64() && r["temp"].isInt64(),
          "oracle expects signed integer telemetry");
    const auto temp = r["temp"].asInt64();
    const Key key{r["sensor"].asString(), r["minute"].asInt64()};
    auto [it, inserted] = groups.try_emplace(key, temp, temp);
    if (!inserted) {
      auto &[minimum, maximum] = it->second;
      minimum = std::min(minimum, temp);
      maximum = std::max(maximum, temp);
    }
  }
  std::string out;
  for (const auto &[key, extremes] : groups)
    if (extremes.first != extremes.second)
      out += key.first + "\t" + std::to_string(key.second) + "\t" +
             std::to_string(extremes.first) + "\t" +
             std::to_string(extremes.second) + "\n";
  return out;
}

J validation_probe(const Path &bundle, const Path &dir, const J &spec) {
  const auto &role = spec["roles"][0];
  const auto emitted =
      plan(bundle,
           spec_for(spec["input_type"].asString(),
                    role["output_type"].asString(), role["requirement"]),
           dir);
  auto routes = array();
  for (const auto &r : emitted["providers"]) {
    check(!complete(r),
          "validation-only intervention completed an archived role");
    routes.append(object({{"ports", ports(r)},
                          {"status", r["status"]},
                          {"uncovered", r["uncovered"]}}));
  }
  return object({{"plan_sha256", hash(dir / "components/emission.json")},
                 {"validation", emitted["interpretation_errors"]},
                 {"gaps", emitted["gaps"]},
                 {"routes", routes}});
}

J variant_operations(J spec, const std::string &keys) {
  auto &ops = spec["roles"][0]["requirement"]["operations"];
  for (auto index : {1, 2, 4})
    ops[index]["parameters"]["keys"] = keys;
  return spec;
}

J canonical_minute(J spec) {
  auto &ops = spec["roles"][0]["requirement"]["operations"];
  ops[1]["parameters"]["keys"] = "0,4";
  ops[2]["parameters"]["keys"] = "0,4";
  ops[3]["parameters"]["column"] = 5;
  ops[3]["parameters"]["value"] = "6";
  ops[4]["parameters"]["keys"] = "0,4";
  ops[6]["parameters"]["order_by"] = "0:text:asc,4:int:asc";
  ops[7]["parameters"]["fields"] = "0,4,5,6";
  auto amended =
      array({ops[0], object({{"outcome", "derive_integer"},
                             {"parameters", object({{"column", 2},
                                                    {"operand", "int:0"},
                                                    {"operation", "add"}})}})});
  for (unsigned i = 1; i < ops.size(); ++i)
    amended.append(ops[i]);
  ops = amended;
  return spec;
}
} // namespace

int main() try {
  const auto parent = runs / "v5-queue-XXH5XMi9/stage/package";
  const auto parent_pin = hash(parent / "manifest.json");
  check(parent_pin ==
            "d15aee96917ff440f58f76270e3ca48575268dcfcf2bce951805c865aae3af63",
        "changed V5 parent");
  const auto base = parent / "bundle";
  verify_bundle(
      base, "436db92629463daee559de2cbbc1c81519e12306d5bf01cc6d2883f3140e779c");
  const auto writer_stage = runs / "v5-tsv-writer-XXAf0x0y/stage";
  const auto writer_pin = hash(writer_stage / "manifest.json");
  check(writer_pin ==
            "0047b47009df08f7ee228729eb46b4438ec9d59eadd7c44647ec6761eeabb994",
        "changed TSV evidence");
  const auto with_tsv = writer_stage / "bundle";
  verify_bundle(
      with_tsv,
      "b2f8a40d44b1589779fc82fcfa23995df5cb2b2432167e9383f6f22e9c828d54");
  const auto writer =
      exact_row(with_tsv, "write_tsv_lines", "strings", "out:" + tsv);
  check(remove_binding(read_tree(with_tsv), writer).at("SHA256SUMS") ==
            bytes(base / "SHA256SUMS"),
        "TSV control differs beyond the writer row");
  const auto reader = verify_json_records_scope(
      with_tsv, runs / "v5-json-records-XXptaI1o/stage",
      "c97ffce350f2eb6f582702d1dd6a9b803463ed0d35ae8a4668bd7d01d195b7cb",
      selector);

  const auto active = fs::canonical(volume / "unllm/boilerplate/active");
  const auto active_pin = hash(active / "SHA256SUMS");
  check(active_pin ==
            "e4564e3079f0b32b3379a411b170ce9af61ec80ab7938b6a974e724abd538328",
        "changed published context");
  const auto ranking = runs / "ranked-gaps-5x2_hyp7/ranking.json";
  const auto ranking_pin = hash(ranking);
  check(
      ranking_pin == "3d902a86f77c7159581a1721cf07c96ccd7b0147259ffdee6edce8f97"
                     "6119495" &&
          load(ranking)["validation_parameters"]["aggregate_groups.measure"] ==
              array({id}),
      "changed final archived validation case");
  const auto recipes = root / "shared/request-recipes.json";
  const auto recipe_pin = hash(recipes);
  check(recipe_pin ==
            "768676cbc34ef7db9d869520bd3d26d86123f49a3c9803901eb5653de3a6f49b",
        "changed supplied recipe");
  const auto archived = runs / "english-audit-bcsgf8pi/replay-staged" / id;
  const auto original =
      volume / "unllm/experiments/boilerplate-B42-VrtYbHSf/fresh-staged" / id;
  const Path response = "run/calls/interpret/response.json";
  const auto saved_pin = hash(archived / response);
  check(saved_pin == hash(original / response), "changed original answer");
  const auto saved = load(archived / response);
  check(saved["input_type"] == "application/json" &&
            saved["roles"].size() == 1 &&
            saved["roles"][0]["output_type"] == tsv &&
            saved["roles"][0]["requirement"]["operations"][0]["outcome"] ==
                "nested_lists",
        "changed H039 role or format");

  const auto work = fresh_run("v5-sensor-conflicts-replay");
  std::cout << work.string() << std::endl;
  auto report = object({{"request", id},
                        {"active_path", active.string()},
                        {"active_sha256", active_pin},
                        {"parent_manifest_sha256", parent_pin},
                        {"writer_manifest_sha256", writer_pin},
                        {"qualified_reader", reader},
                        {"ranking_sha256", ranking_pin},
                        {"recipes_sha256", recipe_pin},
                        {"archived_sha256", saved_pin},
                        {"tools", tools()}});

  auto probe = [&](const std::string &arm, const J &spec) {
    const auto observation =
        validation_probe(base, work / "parameters" / arm, spec);
    report["plans"][arm] = observation;
    return observation["validation"]["r"].asString();
  };
  const auto error = probe("archived", saved);
  check(error.starts_with("aggregate_groups.measure expects ") &&
            error == load(archived /
                          "run/result.json")["interpretation_errors"]["r1"]
                         .asString(),
        "changed min_max validation result");
  auto parameters = saved;
  auto &p =
      parameters["roles"][0]["requirement"]["operations"][1]["parameters"];
  p["measure"] = "distinct";
  check(probe("measure-only", parameters)
            .starts_with("aggregate_groups.result_order expects "),
        "unexpected diagnostic after changing only aggregate measure");
  p["result_order"] = "group";
  check(probe("result-order", parameters)
            .starts_with("aggregate_groups.value_column expects "),
        "unexpected diagnostic after changing aggregate result order");
  p["value_column"] = 3;
  check(probe("all-aggregate-arguments", parameters).empty(),
        "aggregate arguments did not validate");

  const auto fixtures =
      runs / "shared-review-5ui86km8/transfer" / id / "fixtures";
  auto inputs = object();
  for (const auto &split : splits)
    inputs[split] = hash(fixtures / split / "input/readings.json");
  report["input_sha256"] = inputs;
  const auto fixed_recipe = load(recipes)[id];
  auto fixed = saved;
  auto &requirement = fixed["roles"][0]["requirement"];
  requirement["operations"] = operations(fixed_recipe[0]);
  requirement["uncovered"] = array();
  const auto &ops = requirement["operations"];
  check(fixed_recipe.size() == 1 && ops.size() == 8 &&
            ops[0]["outcome"] == "read_json_records" &&
            ops[0]["parameters"]["fields"] == selector &&
            ops[1]["parameters"]["measure"] == "min" &&
            ops[2]["parameters"]["measure"] == "max" &&
            ops[6]["parameters"]["order_by"] == "0:text:asc,2:int:asc",
        "changed H039 fixed recipe shape");
  std::vector<Path> programs;
  auto converter = [&](const std::string &arm) {
    return work / arm / id / "run/project/converter";
  };
  auto replay = [&](const std::string &arm, const Path &bundle, const J &spec) {
    const auto spec_file = work / "specs" / (arm + ".json");
    write(spec_file, spec);
    auto result =
        trial(work, arm, id, bundle, spec_file, "readings.json", inputs);
    if (!result["program_sha256"].isNull())
      programs.push_back(converter(arm));
    report["runs"][arm] = result;
    write(work / "progress.json", report);
    return result;
  };
  const auto blocked = replay("without-writer", base, fixed);
  check(blocked["roles"].size() == 1 && blocked["verified"] == false &&
            ports(blocked["roles"]["0"]) ==
                array({array({"strings", "out:" + tsv})}),
        "H039 did not isolate the TSV sink");
  auto lexical = fixed;
  lexical["roles"][0]["requirement"]["operations"][6]["parameters"]
         ["order_by"] = "0:text:asc,2:text:asc";
  for (const auto &[arm, spec] : std::vector<std::pair<std::string, J>>{
           {"fixed", fixed},
           {"numeric-minute", canonical_minute(fixed)},
           {"text-order", lexical},
           {"room-key", variant_operations(fixed, "0,1,2")},
           {"sensor-key", variant_operations(fixed, "0")}}) {
    const auto result = replay(arm, with_tsv, spec);
    check(result["roles"].size() == 1 && complete(result["roles"]["0"]) &&
              selects(result["roles"]["0"], "write_tsv_lines") &&
              !result["program_sha256"].isNull(),
          "incomplete comparison construction " + arm);
    if (arm != "fixed" && arm != "numeric-minute")
      continue;
    check(result["verified"] == true, "correct construction failed fixtures");
    for (const auto &split : splits) {
      const auto fixture = fixtures / split;
      const auto expected =
          numeric_oracle(bytes(fixture / "input/readings.json"));
      check(result["outputs"][split][0] == expected &&
                expected == bytes(fixture / "output/result-1.txt"),
            "fixed recipe differs from independent oracle or archived output");
    }
  }

  auto witness = [&](const std::string &kind, const std::string &arm,
                     const std::string &input, const std::string &expected) {
    const auto tested = extra_case(work / arm / id / kind, converter(arm),
                                   with_tsv, input, array({expected}));
    check(tested["passed"] == true,
          "unexpected " + kind + " output for " + arm);
    report["witnesses"][kind][arm] = tested;
  };
  const auto correct = numeric_oracle(interleaved);
  check(correct == "A\t2\t-4\t3\nA\t10\t-10\t-2\nB\t10\t0\t7\n",
        "unexpected independent interleaved results");
  witness("interleaved", "fixed", interleaved, correct);
  witness("interleaved", "numeric-minute", interleaved, correct);
  witness("interleaved", "text-order", interleaved,
          "A\t10\t-10\t-2\nA\t2\t-4\t3\nB\t10\t0\t7\n");
  witness("interleaved", "room-key", interleaved, "A\t10\t-10\t-2\n");
  witness("interleaved", "sensor-key", interleaved,
          "A\t10\t-10\t29\nB\t2\t0\t7\nD\t3\t1\t9\n");
  const auto corrected_zero = numeric_oracle(signed_zero);
  check(corrected_zero == "C\t0\t2\t3\nZ\t0\t-8\t7\n",
        "unexpected independent signed-zero results");
  witness("signed-zero", "fixed", signed_zero, "C\t-0\t2\t3\n");
  witness("signed-zero", "numeric-minute", signed_zero, corrected_zero);

  check(tools() == report["tools"] && hash(active / "SHA256SUMS") == active_pin,
        "changed replay context");
  write(work / ".report.json", report);
  run({"sync", "-f", work / ".report.json"});
  for (const auto &exe : programs)
    fs::remove(exe);
  fs::rename(work / ".report.json", work / "report.json");
  std::cout << (work / "report.json").string() << '\n';
  return 0;
} catch (const std::exception &e) {
  std::cerr << e.what() << '\n';
  return 1;
}
