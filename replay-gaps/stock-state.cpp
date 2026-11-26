#include "row-trial.hpp"
#include <iostream>

using namespace miner;
using namespace row_replay;
namespace {
const Strings ids{"H002", "H007"};
const Strings splits{"ordinary", "boundary", "empty", "transfer", "sparse"};
const Path response{"run/calls/interpret/response.json"};
const std::string stock = "A\tX\t2\nB\tX\t2\nA\tX\t4\nB\tZ\t2\nA\tX\t2\n"
                          "B\tZ\t5\nA\tX\t2\nB\tZ\t2\nA\tY\t5\nB\tZ\t2\n"
                          "A\tY\t2\nB\tX\t2\nA\tX\t2\nB\tX\t2\nA\tX\t2\n";

J plan_probe(const Path &bundle, const Path &dest, const J &saved) {
  check(saved["roles"].size() == 1, "expected one stock report");
  const auto &role = saved["roles"][0];
  const auto spec =
      spec_for(saved["input_type"].asString(), role["output_type"].asString(),
               role["requirement"]);
  const auto p = plan(bundle, spec, dest);
  auto routes = array();
  for (const auto &r : p["providers"]) {
    routes.append(object({{"status", r["status"]},
                          {"ports", ports(r)},
                          {"steps", r["plan"]["steps"]},
                          {"uncovered", r["uncovered"]}}));
  }
  return object({{"plan_sha256", hash(dest / "components/emission.json")},
                 {"validation", p["interpretation_errors"]},
                 {"gaps", p["gaps"]},
                 {"routes", routes}});
}

J emit_only(J saved, const std::string &emit) {
  auto &op = saved["roles"][0]["requirement"]["operations"][0];
  check(op["outcome"] == "threshold_entries" &&
            op["parameters"]["emit"] == array({"key", "row_number"}),
        "saved threshold choice changed");
  op["parameters"]["emit"] = emit;
  return saved;
}

J fixture_hashes(const Path &history) {
  auto result = object();
  for (const auto &split : splits)
    result[split] = hash(history / split / "input/stock.tsv");
  return result;
}

J without_comparison(J spec, unsigned index, const std::string &type) {
  auto &ops = spec["roles"][0]["requirement"]["operations"];
  J removed;
  check(ops.removeIndex(index, &removed) &&
            removed["outcome"] == "filter_records" &&
            removed["parameters"]["comparison"] == "eq" &&
            removed["parameters"]["type"] == type,
        "saved repeat predicate changed");
  return spec;
}
} // namespace

int main() try {
  const auto parent = runs / "v5-queue-XXH5XMi9/stage/package";
  const auto parent_pin = hash(parent / "manifest.json");
  check(parent_pin ==
            "d15aee96917ff440f58f76270e3ca48575268dcfcf2bce951805c865aae3af63",
        "changed V5 parent");
  const auto bundle = parent / "bundle";
  verify_bundle(
      bundle,
      "436db92629463daee559de2cbbc1c81519e12306d5bf01cc6d2883f3140e779c");
  const auto active = fs::canonical(volume / "unllm/boilerplate/active");
  const auto active_pin = hash(active / "SHA256SUMS");
  check(active_pin ==
            "e4564e3079f0b32b3379a411b170ce9af61ec80ab7938b6a974e724abd538328",
        "changed published context");
  const auto ranking = runs / "ranked-gaps-5x2_hyp7/ranking.json";
  const auto ranking_pin = hash(ranking);
  check(ranking_pin == "3d902a86f77c7159581a1721cf07c96ccd7b0147259ffdee6edce8f"
                       "976119495" &&
            load(ranking)["validation_parameters"]["threshold_entries.emit"] ==
                string_array(ids),
        "changed archived validation cluster");
  const auto recipe_file = root / "shared/request-recipes.json";
  const auto recipe_pin = hash(recipe_file);
  check(recipe_pin ==
            "768676cbc34ef7db9d869520bd3d26d86123f49a3c9803901eb5653de3a6f49b",
        "changed fixed recipe source");
  const auto recipes = load(recipe_file);
  const auto history = runs / "shared-review-5ui86km8/transfer";
  const auto archive = runs / "english-audit-bcsgf8pi/replay-staged";
  const auto original =
      volume / "unllm/experiments/boilerplate-B42-VrtYbHSf/fresh-staged";
  const auto work = fresh_run("v5-stock-state-replay");
  std::cout << work.string() << std::endl;
  auto report = object({
      {"active_path", active.string()},
      {"active_sha256", active_pin},
      {"parent_manifest_sha256", parent_pin},
      {"ranking_sha256", ranking_pin},
      {"recipes_sha256", recipe_pin},
      {"tools", tools()},
      {"bindings",
       array({exact_row(bundle, "threshold_keys", "rows", "strings"),
              exact_row(bundle, "threshold_rows", "rows", "integers"),
              exact_row(bundle, "previous_field", "rows", "rows")})},
      {"threshold_provider_sha256",
       hash(bundle / "providers/threshold_provider.hpp")},
      {"previous_field_header_sha256",
       hash(bundle / "headers/miner_previous_field.hpp")},
  });
  std::vector<Path> programs;
  for (const auto &id : ids) {
    const auto saved_path = archive / id / response;
    const auto saved_pin = hash(saved_path);
    check(saved_pin == hash(original / id / response),
          "changed original answer for " + id);
    const auto prior_result = archive / id / "run/result.json";
    const auto previous = load(prior_result)["interpretation_errors"];
    const auto saved = load(saved_path);
    const auto &saved_role = saved["roles"][0];
    check(saved["input_type"] == "text/tab-separated-values" &&
              saved_role["output_type"] == "text/plain" &&
              saved["roles"].size() == 1 && recipes[id].size() == 1,
          "changed stock report shape");
    auto &item = report["requests"][id];
    item["archived_sha256"] = saved_pin;
    item["archived_result_sha256"] = hash(prior_result);
    const auto root = work / "parameters" / id;
    const auto v = plan_probe(bundle, root / "archived", saved);
    const auto err = previous[saved_role["id"].asString()].asString();
    check(err.starts_with("threshold_entries.emit expects ") &&
              v["validation"]["r"].asString() == err && v["routes"].empty(),
          "archived scalar error changed for " + id);
    item["plans"]["archived"] = v;
    for (const auto *emit : {"key", "row_number"}) {
      const auto p = plan_probe(bundle, root / emit, emit_only(saved, emit));
      check(p["validation"].empty(),
            "scalar emit still failed validation for " + id);
      for (const auto &route : p["routes"]) {
        check(route["uncovered"] == saved_role["requirement"]["uncovered"] &&
                  route["status"] != "complete",
              "emit-only probe bypassed an obligation for " + id);
      }
      item["plans"][emit] = p;
    }

    const auto fixtures = history / id / "fixtures";
    const auto inputs = fixture_hashes(fixtures);
    item["input_hashes"] = inputs;
    auto fixed = saved;
    fixed["roles"][0]["requirement"]["operations"] = operations(recipes[id][0]);
    fixed["roles"][0]["requirement"]["uncovered"] = array();
    auto replay = [&](const std::string &arm, const J &spec) {
      const auto path = work / "specs" / id / (arm + ".json");
      write(path, spec);
      const auto result =
          trial(work, arm, id, bundle, path, "stock.tsv", inputs);
      check(result["roles"].size() == 1 && complete(result["roles"]["0"]) &&
                !result["program_sha256"].isNull(),
            "incomplete stock replay " + id + " " + arm);
      programs.push_back(work / arm / id / "run/project/converter");
      item["runs"][arm] = result;
      write(work / "progress.json", report);
      return result;
    };
    const auto accepted = replay("fixed", fixed);
    check(accepted["verified"] == true &&
              selects(accepted["roles"]["0"],
                      id == "H002" ? "threshold_keys" : "previous_field"),
          "fixed stock recipe did not bind and verify for " + id);
    for (const auto &split : splits)
      check(accepted["outputs"][split][0] ==
                bytes(fixtures / split / "output/result-1.txt"),
            "fixed output changed from archived bytes");
    auto witness = [&](const std::string &arm, const std::string &expected) {
      const auto dir = work / arm / id;
      const auto result =
          extra_case(dir / "interleaved", dir / "run/project/converter", bundle,
                     stock, array({expected}));
      check(result["passed"] == true,
            "fresh stock witness differed for " + id + " " + arm);
      item["interleaved"][arm] = result;
    };

    if (id == "H002") {
      auto early_rearm = fixed;
      auto &p =
          early_rearm["roles"][0]["requirement"]["operations"][0]["parameters"];
      check(p["reset_threshold"] == 5, "changed H002 rearm boundary");
      p["reset_threshold"] = 4;
      replay("early-rearm", early_rearm);
      witness("fixed", "A\nB\nB\nA\n");
      witness("early-rearm", "A\nB\nA\nB\nA\n");
    } else {
      replay("quantity-only", without_comparison(fixed, 3, "text_field"));
      replay("aisle-only", without_comparison(fixed, 4, "int_field"));
      auto global = fixed;
      for (unsigned i : {0, 1}) {
        auto &op = global["roles"][0]["requirement"]["operations"][i];
        check(op["outcome"] == "previous_field" &&
                  op["parameters"]["keys"] == "0",
              "changed per-SKU repeat recipe");
        op["parameters"]["keys"] = "all";
      }
      replay("global-previous", global);
      witness("fixed", "A\t7\nB\t10\nB\t14\nA\t15\n");
      witness("quantity-only",
              "B\t4\nA\t7\nB\t10\nB\t12\nA\t13\nB\t14\nA\t15\n");
      witness("aisle-only",
              "A\t3\nA\t5\nB\t6\nA\t7\nB\t8\nB\t10\nA\t11\nB\t14\nA\t15\n");
      witness("global-previous", "B\t2\nA\t13\nB\t14\nA\t15\n");
    }
  }
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
