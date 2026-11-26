#include "row-trial.hpp"
#include <algorithm>
#include <iostream>

using namespace miner;
using namespace row_replay;
namespace {
struct Case {
  std::string id, input, order;
};

const std::vector<Case> selector_cases{
    {"H006", "stock.tsv", ""},
    {"H021", "scans.db", "4:num:asc"},
    {"H024", "scans.db", "1:text:asc"},
    {"H027", "scans.db", "4:num:asc"},
    {"H032", "readings.json", "3:num:desc,2:num:asc"},
    {"H053", "backup.zip", "1:num:desc,0:text:asc"},
    {"H066", "project", "2:num:desc,1:text:asc"},
    {"H084", "build.tsv", "0:text:asc"},
    {"H092", "bookings.json", "2:num:asc"},
    {"H095", "bookings.json", "5:num:asc,0:text:asc"},
};
const std::vector<Case> grouping_cases{
    {"H004", "stock.tsv", "asc"},       {"H001", "stock.tsv", "asc"},
    {"H003", "stock.tsv", "encounter"}, {"H005", "stock.tsv", "asc"},
    {"H008", "stock.tsv", "asc"},       {"H056", "backup.zip", "asc"},
    {"H083", "build.tsv", "encounter"}, {"H090", "build.tsv", "encounter"},
};
const Strings splits{"ordinary", "boundary", "empty", "transfer", "sparse"};
const std::string tsv = "text/tab-separated-values";
const Path response = "run/calls/interpret/response.json";

J plans(const Path &bundle, const Path &dest, const J &spec) {
  auto result = array();
  for (unsigned i = 0; i < spec["roles"].size(); ++i) {
    const auto &role = spec["roles"][i];
    const auto request =
        spec_for(spec["input_type"].asString(), role["output_type"].asString(),
                 role["requirement"]);
    const auto dir = dest / std::to_string(i);
    const auto p = plan(bundle, request, dir);
    unsigned complete_roles = 0;
    auto routes = array();
    for (const auto &provider : p["providers"]) {
      complete_roles += complete(provider);
      routes.append(object({{"status", provider["status"]},
                            {"ports", ports(provider)},
                            {"uncovered", provider["uncovered"]}}));
    }
    result.append(
        object({{"plan_sha256", hash(dir / "components/emission.json")},
                {"validation", p["interpretation_errors"]},
                {"gaps", p["gaps"]},
                {"complete_roles", complete_roles},
                {"routes", routes}}));
  }
  return result;
}

std::pair<J, J> selector_controls(J order_only, const std::string &order) {
  auto all_selector_args = order_only;
  for (unsigned i = 0; i < order_only["roles"].size(); ++i) {
    auto &ops = order_only["roles"][i]["requirement"]["operations"];
    unsigned count = 0;
    for (unsigned j = 0; j < ops.size(); ++j) {
      auto &op = ops[j];
      if (op["outcome"] != "select_group_row")
        continue;
      ++count;
      check(op["parameters"]["order_by"].isString(), "missing saved row order");
      op["parameters"]["order_by"] = order;
      auto &same =
          all_selector_args["roles"][i]["requirement"]["operations"][j];
      same["parameters"] = object({{"order_by", order},
                                   {"result_order", "group"},
                                   {"which", op["parameters"]["which"]}});
    }
    check(count == 1, "expected one rejected selector per role");
  }
  return {order_only, all_selector_args};
}

J group_order_control(J saved, const std::string &order) {
  for (auto &role : saved["roles"]) {
    unsigned count = 0;
    for (auto &op : role["requirement"]["operations"]) {
      if (op["outcome"] != "group_rows")
        continue;
      ++count;
      check(op["parameters"]["group_order"].isString(),
            "missing saved group order");
      op["parameters"]["group_order"] = order;
    }
    check(count == 1, "expected one rejected grouping per role");
  }
  return saved;
}

J fingerprint(const Path &path) {
  if (fs::is_regular_file(path))
    return J(hash(path));
  auto result = object();
  for (const auto &entry : fs::recursive_directory_iterator(path)) {
    const auto name = entry.path().lexically_relative(path).generic_string();
    auto status = entry.symlink_status();
    if (fs::is_symlink(status))
      result[name] =
          object({{"symlink", fs::read_symlink(entry.path()).string()}});
    else if (fs::is_directory(status))
      result[name] = object({{"directory", true}});
    else {
      check(fs::is_regular_file(status), "unexpected fixture entry");
      result[name] = object({{"sha256", hash(entry.path())}});
    }
  }
  return result;
}

J fixture_context(const Path &dir, const Path &historical, const Path &exe,
                  const Path &bundle, const Case &c, const J &result) {
  auto evidence = object();
  for (const auto &split : splits) {
    const bool held = split == "transfer" || split == "sparse";
    const auto previous = historical / split / "input" / c.input;
    const auto current =
        dir / (held ? "held" : "run/check-initial") / split / "input" / c.input;
    const auto before = fingerprint(previous);
    const auto now = fingerprint(current);
    auto observation = object({{"historical", before}, {"generated", now}});
    const bool sqlite = previous.extension() == ".db";
    const bool zip = previous.extension() == ".zip";
    if (sqlite) {
      const auto dump =
          run({"/usr/bin/sqlite3", "-readonly", previous, ".dump"});
      check(dump == run({"/usr/bin/sqlite3", "-readonly", current, ".dump"}),
            "SQLite schema or rows changed in " + c.id + " " + split);
      observation["sqlite_dump_sha256"] = sha(dump);
    } else if (!zip) {
      check(before == now, "fixture drift in " + c.id + " " + split);
    }
    if ((sqlite || zip) && result["verified"] == true) {
      const auto exact =
          extra_case(dir / "exact-historical-input" / split, exe, bundle,
                     bytes(previous), result["outputs"][split]);
      check(exact["passed"] == true, "output changed on exact saved fixture");
      observation["exact_bytes_replay"] = exact;
    }
    evidence[split] = observation;
  }
  return evidence;
}
} // namespace

int main(int argc, char **argv) try {
  const bool grouping = argc == 2 && std::string(argv[1]) == "--group-order";
  check(argc == 1 || grouping, "usage: row-order-replay [--group-order]");
  const auto &cases = grouping ? grouping_cases : selector_cases;
  const std::string parameter =
      grouping ? "group_rows.group_order" : "select_group_row.order_by";
  const auto parent = runs / "v5-queue-XXH5XMi9/stage/package";
  const auto parent_pin = hash(parent / "manifest.json");
  check(parent_pin ==
            "d15aee96917ff440f58f76270e3ca48575268dcfcf2bce951805c865aae3af63",
        "changed V5 parent");
  const auto base = parent / "bundle";
  verify_bundle(
      base, "436db92629463daee559de2cbbc1c81519e12306d5bf01cc6d2883f3140e779c");
  const auto tsv_stage = runs / "v5-tsv-writer-XXAf0x0y/stage";
  const auto tsv_pin = hash(tsv_stage / "manifest.json");
  check(tsv_pin ==
            "0047b47009df08f7ee228729eb46b4438ec9d59eadd7c44647ec6761eeabb994",
        "changed TSV stage");
  const auto with_tsv = tsv_stage / "bundle";
  verify_bundle(
      with_tsv,
      "b2f8a40d44b1589779fc82fcfa23995df5cb2b2432167e9383f6f22e9c828d54");
  const auto writer = exact_row(with_tsv, "write_tsv_lines", "strings",
                                "out:text/tab-separated-values");
  check(remove_binding(read_tree(with_tsv), writer).at("SHA256SUMS") ==
            bytes(base / "SHA256SUMS"),
        "TSV comparison differs by more than the exact writer");

  const auto active = fs::canonical(volume / "unllm/boilerplate/active");
  const auto live = hash(active / "SHA256SUMS");
  check(live ==
            "e4564e3079f0b32b3379a411b170ce9af61ec80ab7938b6a974e724abd538328",
        "changed published context");
  const auto ranking_file = runs / "ranked-gaps-5x2_hyp7/ranking.json";
  const auto ranking_pin = hash(ranking_file);
  check(ranking_pin ==
            "3d902a86f77c7159581a1721cf07c96ccd7b0147259ffdee6edce8f976119495",
        "changed archived ranking");
  Strings ids;
  for (const auto &c : cases)
    ids.push_back(c.id);
  std::sort(ids.begin(), ids.end());
  check(load(ranking_file)["validation_parameters"][parameter] ==
            string_array(ids),
        "incomplete archived validation cluster");
  const auto recipes_file = root / "shared/request-recipes.json";
  const auto recipe_pin = hash(recipes_file);
  check(recipe_pin ==
            "768676cbc34ef7db9d869520bd3d26d86123f49a3c9803901eb5653de3a6f49b",
        "changed supplied reference recipes");
  const auto recipes = load(recipes_file);
  const auto work =
      fresh_run(grouping ? "v5-group-order-replay" : "v5-row-order-replay");
  std::cout << work.string() << std::endl;
  const auto archive = runs / "english-audit-bcsgf8pi/replay-staged";
  const auto original =
      volume / "unllm/experiments/boilerplate-B42-VrtYbHSf/fresh-staged";
  const auto history = runs / "shared-review-5ui86km8/transfer";
  const auto sqlite = hash("/usr/bin/sqlite3");
  auto report = object({{"ranking_sha256", ranking_pin},
                        {"recipes_sha256", recipe_pin},
                        {"parent_manifest_sha256", parent_pin},
                        {"tsv_manifest_sha256", tsv_pin},
                        {"active_path", active.string()},
                        {"active_sha256", live},
                        {"sqlite_cli_sha256", sqlite},
                        {"tools", tools()}});
  std::vector<Path> programs;
  for (const auto &c : cases) {
    std::cout << c.id << " " << parameter << std::endl;
    const auto saved_file = archive / c.id / response;
    const auto saved_pin = hash(saved_file);
    check(saved_pin == hash(original / c.id / response),
          "changed original answer for " + c.id);
    const auto old_result = archive / c.id / "run/result.json";
    const auto previous = load(old_result)["interpretation_errors"];
    const auto saved = load(saved_file);
    const auto dir = work / "parameters" / c.id;
    auto entry = object({{"archived_sha256", saved_pin},
                         {"archived_result_sha256", hash(old_result)},
                         {"historical_errors", previous}});
    entry["archived"] = plans(base, dir / "archived", saved);
    if (grouping) {
      entry["order_only"] =
          plans(base, dir / "order-only", group_order_control(saved, c.order));
    } else {
      const auto [order_only, params_only] = selector_controls(saved, c.order);
      entry["order_only"] = plans(base, dir / "order-only", order_only);
      entry["selector_only"] = plans(base, dir / "selector-only", params_only);
    }
    const auto &measurements = entry;
    for (unsigned i = 0; i < saved["roles"].size(); ++i) {
      const auto id = saved["roles"][i]["id"].asString();
      const auto first = previous[id].asString();
      check(first.starts_with(parameter + " expects ") &&
                measurements["archived"][i]["validation"]["r"].asString() ==
                    first,
            "archived validation changed for " + c.id + " " + id);
      const auto &order = measurements["order_only"][i];
      const auto next = order["validation"]["r"].asString();
      if (grouping) {
        check(!next.starts_with(parameter + " expects ") &&
                  order["complete_roles"].asUInt() == 0,
              "unexpected group-order repair result for " + c.id + " " + id);
        continue;
      }
      check(next.starts_with("select_group_row.result_order expects ") &&
                !measurements["selector_only"][i]["validation"]["r"]
                     .asString()
                     .starts_with("select_group_row.") &&
                measurements["selector_only"][i]["complete_roles"].asUInt() ==
                    0,
            "parameter-only route changed unexpectedly for " + c.id + " " + id);
    }
    report["requests"][c.id] = entry;
  }
  write(work / "progress.json", report);

  auto replay = [&](const Case &c, const std::string &arm, const Path &catalog,
                    const J &spec) {
    const auto path = work / "specs" / c.id / (arm + ".json");
    write(path, spec);
    auto result = trial(work, arm, c.id, catalog, path);
    const auto dir = work / arm / c.id;
    const auto exe = dir / "run/project/converter";
    if (!result["program_sha256"].isNull())
      programs.push_back(exe);
    result["fixture_context"] = fixture_context(
        dir, history / c.id / "fixtures", exe, catalog, c, result);
    report["requests"][c.id]["runs"][arm] = result;
    write(work / "progress.json", report);
    return result;
  };

  for (const auto &c : cases) {
    auto fixed = load(archive / c.id / response);
    const auto &recipe = recipes[c.id];
    bool needs_tsv = false;
    check(recipe.size() == fixed["roles"].size(), "changed recipe role count");
    for (unsigned i = 0; i < fixed["roles"].size(); ++i) {
      auto &role = fixed["roles"][i];
      needs_tsv |= role["output_type"] == tsv;
      role["requirement"]["operations"] = operations(recipe[i]);
      role["requirement"]["uncovered"] = array();
    }
    const auto accepted =
        replay(c, "fixed", needs_tsv ? with_tsv : base, fixed);
    check(accepted["verified"] == true &&
              accepted["roles"].size() == recipe.size(),
          "fixed construction did not pass for " + c.id);
    for (const auto &role : accepted["roles"])
      check(complete(role) && !role["steps"].empty(), "fixed role diagnostic");
    for (const auto &split : splits) {
      const auto prior = history / c.id / "fixtures" / split / "output";
      for (unsigned i = 0; i < recipe.size(); ++i)
        check(accepted["outputs"][split][i] ==
                  bytes(prior / ("result-" + std::to_string(i + 1) + ".txt")),
              "fixed output differs from archived bytes: " + c.id + " " +
                  split);
    }
    if (needs_tsv) {
      const auto blocked = replay(c, "without-writer", base, fixed);
      check(blocked["verified"] == false,
            "fixed TSV route unexpectedly verified");
      for (const auto &role : blocked["roles"])
        check(ports(role) ==
                  array({array({"strings", "out:text/tab-separated-values"})}),
              "fixed TSV route stopped before the sink");
      check(blocked["roles"].size() == recipe.size(), "missing TSV role");
    }
    if (c.id == "H004") {
      auto single_aggregate = fixed;
      single_aggregate["roles"][0]["requirement"]["operations"] =
          operations(array(
              {array({"group_rows",
                      object({{"keys", "1"}, {"group_order", "asc"}})}),
               array({"aggregate_groups", object({{"measure", "min"},
                                                  {"value_column", 2},
                                                  {"result_order", "group"}})}),
               array({"report_fields", object({{"fields", "0,1"}})})}));
      const auto rejected =
          plans(base, work / "parameters/H004/aggregate-min", single_aggregate);
      check(rejected[0]["validation"]["r"].asString().starts_with(
                "aggregate_groups.measure expects "),
            "the single aggregate unexpectedly admitted minimum");
      report["requests"][c.id]["aggregate_min"] = rejected;

      auto row_count = fixed;
      auto &aggregate = row_count["roles"][0]["requirement"]["operations"][2];
      check(aggregate["outcome"] == "aggregate_groups" &&
                aggregate["parameters"]["measure"] == "distinct",
            "changed H004 distinct-count recipe");
      aggregate["parameters"]["measure"] = "count";
      auto early_dedup = fixed;
      auto &dedup_ops = early_dedup["roles"][0]["requirement"]["operations"];
      auto prefix = operations(array(
          {array({"group_rows",
                  object({{"keys", "1,0"}, {"group_order", "encounter"}})}),
           array({"select_group_row", object({{"which", "first"},
                                              {"order_by", ""},
                                              {"result_order", "group"}})})}));
      for (const auto &op : dedup_ops)
        prefix.append(op);
      dedup_ops = prefix;
      for (const auto &[arm, spec] :
           {std::pair{"count-rows", row_count},
            std::pair{"dedup-before-min", early_dedup}}) {
        const auto variant = replay(c, arm, base, spec);
        check(variant["roles"].size() == 1 && complete(variant["roles"]["0"]),
              "H004 mutation did not construct");
      }
      const std::string stock = "Q\tZ\t10\nQ\tA\t11\nP\tZ\t2\nQ\tA\t-"
                                "3\nR\tA\t4\nQ\tZ\t-7\nQ\tZ\t-7\n";
      for (const auto &[arm, expected] :
           {std::pair{"fixed", "A\t2\t-3\nZ\t2\t-7\n"},
            std::pair{"count-rows", "A\t3\t-3\nZ\t4\t-7\n"},
            std::pair{"dedup-before-min", "A\t2\t4\nZ\t2\t2\n"}}) {
        const auto dir = work / arm / c.id;
        const auto tested =
            extra_case(dir / "distinct-and-min", dir / "run/project/converter",
                       base, stock, array({expected}));
        check(tested["passed"] == true,
              "unexpected H004 distinct/minimum behavior");
        report["requests"][c.id]["distinct_and_min"][arm] = tested;
      }
    }
    if (c.id != "H032")
      continue;
    auto wrong_tie = fixed;
    auto &op = wrong_tie["roles"][0]["requirement"]["operations"][2];
    check(op["outcome"] == "select_group_row" &&
              op["parameters"]["order_by"] == "3:num:desc",
          "changed H032 reference selector");
    op["parameters"]["order_by"] = c.order;
    const auto variant = replay(c, "minute-tie", with_tsv, wrong_tie);
    check(variant["roles"].size() == 1 && complete(variant["roles"]["0"]),
          "minute-tie mutation did not construct");
    const std::string readings = R"([
      {"sensor":"S","room":"A","minute":30,"temp":9},
      {"sensor":"S","room":"A","minute":10,"temp":9},
      {"sensor":"S","room":"A","minute":20,"temp":3}
    ])";
    for (const auto *arm : {"fixed", "minute-tie"}) {
      const auto dir = work / arm / c.id;
      const auto expected =
          arm == std::string("fixed") ? "S\t30\t9\n" : "S\t10\t9\n";
      auto tested =
          extra_case(dir / "out-of-order-peaks", dir / "run/project/converter",
                     with_tsv, readings, array({expected}));
      check(tested["passed"] == true, "unexpected H032 tie behavior");
      report["requests"][c.id]["out_of_order_peaks"][arm] = tested;
    }
  }
  check(hash(active / "SHA256SUMS") == live && tools() == report["tools"] &&
            hash("/usr/bin/sqlite3") == sqlite,
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
