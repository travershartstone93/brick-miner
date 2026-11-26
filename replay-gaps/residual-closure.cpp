#include "row-trial.hpp"
#include "xml-fixture.hpp"
#include <algorithm>
#include <iostream>
#include <map>
#include <set>
#include <sstream>

using namespace miner;
using namespace row_replay;
namespace {
const Strings requests{"H010", "H041", "H047"};
const Strings splits{"ordinary", "boundary", "empty", "transfer", "sparse"};
const Path response = "run/calls/interpret/response.json";
const std::map<std::string, Strings> variants{
    {"H010", {"archived", "deduplicate-first"}},
    {"H041", {"archived", "xpath-then-unique", "deduplicate-before-live"}},
    {"H047", {"draft-present"}}};
const std::string stock = "K\tX\t1\nK\tA\t0\nS\tC\t3\nP\tX\t5\nS\tB\t4\nR\tB\t9"
                          "\nR\tA\t0\nH\tC\t0\nR\tX\t1\n";
const std::string feed = R"(<feed>
  <article editor="Ann" section="beta" state="draft"><title>Shared &amp; Name</title></article>
  <article editor="Bo" section="Zulu" state="draft"><title>Hidden</title></article>
  <article editor="Bo" section="Alpha" state="live"><title>Earlier</title></article>
  <article editor="Cal" section="beta" state="live"><title>Shared &#38; Name</title></article>
  <article editor="Ann" section="beta" state="draft"><title>Draft Only</title></article>
  <article editor="Cal" section="A&#108;pha" state="draft"><title>Hidden Alpha</title></article>
  <article editor="Bo" section="Alpha" state="live"><title>Shared &amp; Name</title></article>
  <article editor="Bo" section="Dora" state="draft"><title>Twin</title></article>
  <article editor="Cal" section="beta" state="live"><title>Twin</title></article>
  <article editor="Ann" section="Delta" state="live"><title>live only</title></article>
  <article editor="Bo" section="beta" state="draft"><title>Another draft</title></article>
  <article editor="Cal" section="Alpha" state="live"><title>twin</title></article>
  <article editor="Ann" section="Cold" state="draft"><title>Cold draft</title></article>
</feed>)";
const std::string draft_only =
    R"(<feed><article editor="Bo" section="draft-A" state="draft"><title>Only</title></article></feed>)";

std::string lines(const Strings &values) {
  return values.empty() ? std::string{} : join(values, "\n") + "\n";
}

Strings unique(const Strings &values) {
  Strings result;
  std::set<std::string> seen;
  for (const auto &v : values)
    if (seen.insert(v).second)
      result.push_back(v);
  return result;
}

std::string json_array(const Strings &values) {
  Json::FastWriter writer;
  writer.omitEndingLineFeed();
  return writer.write(string_array(values));
}

bool matches(const std::string &id, const J &actual, const J &expected) {
  if (id != "H041")
    return actual == expected;
  return actual.size() == 1 && expected.size() == 1 &&
         parse(actual[0].asString()) == parse(expected[0].asString());
}

J oracle(const std::string &id, const std::string &data,
         std::string_view arm = "reference") {
  if (id == "H010") {
    Strings aisles;
    std::istringstream input(data);
    for (std::string row; std::getline(input, row);) {
      const auto first = row.find('\t');
      const auto last = row.rfind('\t');
      check(first != std::string::npos && first < last,
            "expected inventory row");
      aisles.push_back(row.substr(first + 1, last - first - 1));
    }
    if (arm == "archived")
      return array({lines(aisles)});
    if (arm == "deduplicate-first")
      aisles = unique(aisles);
    std::reverse(aisles.begin(), aisles.end());
    return array({lines(unique(aisles))});
  }

  const auto articles = read_feed(data);
  if (id == "H041") {
    Strings titles;
    std::set<std::string> seen;
    for (const auto &article : articles) {
      if (arm == "deduplicate-before-live" &&
          !seen.insert(article.title).second)
        continue;
      if (article.state == "live")
        titles.push_back(article.title);
    }
    return array({json_array(arm == "archived" ? titles : unique(titles))});
  }

  check(id == "H047", "unknown prose-obligation oracle");
  struct States {
    bool draft = false;
    bool live = false;
  };
  std::map<std::string, States> by_section;
  for (const auto &article : articles) {
    auto &seen = by_section[article.section];
    if (article.state == "draft")
      seen.draft = true;
    else {
      check(article.state == "live", "expected draft or live");
      seen.live = true;
    }
  }
  Strings mixed, drafts;
  for (const auto &[section, states] : by_section) {
    if (states.draft && states.live)
      mixed.push_back(section);
    if (states.draft && (!states.live || arm == "draft-present"))
      drafts.push_back(section);
  }
  return array({lines(mixed), lines(drafts)});
}

J op(const std::string &effect, const J &parameters = object()) {
  return object({{"outcome", effect}, {"parameters", parameters}});
}

J change(J spec, const J &saved, const std::string &arm) {
  auto &steps = spec["roles"][0]["requirement"]["operations"];
  if (arm == "deduplicate-first") {
    check(steps[1]["outcome"] == "reverse_order" &&
              steps[2]["outcome"] == "unique_lines",
          "changed aisle scan");
    std::swap(steps[1], steps[2]);
  } else if (arm == "xpath-then-unique") {
    const auto &old = saved["roles"][0]["requirement"]["operations"];
    check(old.size() == 1 && old[0]["outcome"] == "xml_text",
          "changed archived XPath");
    steps = array({old[0], op("unique_lines")});
  } else if (arm == "deduplicate-before-live") {
    check(steps[0]["outcome"] == "xml_records" &&
              steps[1]["outcome"] == "filter_records",
          "changed live-title recipe");
    steps =
        array({steps[0],
               op("group_rows",
                  object({{"keys", "3"}, {"group_order", "encounter"}})),
               op("select_group_row", object({{"which", "first"},
                                              {"order_by", ""},
                                              {"result_order", "source_asc"}})),
               steps[1], steps[2]});
  } else {
    check(arm == "draft-present", "unknown closure control");
    auto &draft = spec["roles"][1]["requirement"]["operations"];
    J removed;
    check(draft.removeIndex(4, &removed) &&
              removed["outcome"] == "filter_records" &&
              removed["parameters"] == object({{"column", 5},
                                               {"comparison", "eq"},
                                               {"type", "int"},
                                               {"value", "0"}}),
          "changed draft-only exclusion");
  }
  return spec;
}

struct Evidence {
  std::string directory, sha256, section;
};
const std::vector<Evidence> earlier{
    {"v5-residual-directory-replay-XXHTdjz7",
     "bace72893bc599004a2d0564c058213083257d1f396760bf565d21cc72282b68",
     "directories"},
    {"v5-residual-zip-replay-XXRDOTGr",
     "bc1d40444fd5e431806629b915754899ee9f8a172dc7526d582e26ab502eda6f",
     "archives"},
    {"v5-residual-uri-replay-XXNy7n6G",
     "15dda75f1b9ec6f40dead0e5c304ce6693f92bb14d423a1574323ecab5b6ec70",
     "uri"}};

J cohort(const J &ranking, const std::string &rank_pin, const J &current) {
  auto results = object();
  unsigned role_count = 0;
  auto add = [&](const std::string &id, const J &scores, const J &program,
                 unsigned roles, const J &pin) {
    check(!results.isMember(id) && program.isString() &&
              scores.size() == splits.size() &&
              ranking["requests"][id]["roles"].size() == roles,
          "missing or duplicate verified prose-obligation request " + id);
    for (const auto &split : splits)
      check(scores[split] == true, "failed fixture " + id + " " + split);
    role_count += roles;
    results[id] = object({{"report_sha256", pin},
                          {"role_count", roles},
                          {"program_sha256", program},
                          {"scores", scores}});
  };
  auto read = [&](const J &cases, const J &pin) {
    for (const auto &id : cases.getMemberNames()) {
      const auto &run = cases[id]["runs"]["reference"];
      check(run["verified"] == true, "missing passed reference program " + id);
      for (const auto &role : run["roles"])
        check(complete(role), "incomplete verified role " + id);
      for (const auto &split : splits)
        check(run["outputs"][split].size() == run["roles"].size(),
              "missing verified role output " + id + " " + split);
      add(id, run["scores"], run["program_sha256"], run["roles"].size(), pin);
    }
  };
  for (const auto &e : earlier) {
    const auto file = runs / e.directory / "report.json";
    check(hash(file) == e.sha256,
          "changed retained cohort evidence " + e.section);
    read(load(file)[e.section], e.sha256);
  }
  for (const auto &id : {"H058", "H070"}) {
    const auto &entry = ranking["requests"][id];
    const auto &proof = entry["retained_fixture_evidence"];
    check(entry["previously_verified"] == true &&
              proof["root_check"]["passed"] == true,
          "changed previously verified path evidence");
    add(id, proof["scores"], proof["program_sha256"], 1, rank_pin);
  }
  read(current, J());
  check(
      results.size() == 19 && role_count == 22 &&
          results.getMemberNames() == ranking["requests"].getMemberNames(),
      "verified cohort differs from the 19 archived prose-obligation requests");
  return object({{"verified_requests", results.size()},
                 {"verified_roles", role_count},
                 {"saved_fixture_passes", results.size() * splits.size()},
                 {"saved_role_outputs", role_count * splits.size()},
                 {"requests", results}});
}
} // namespace

int main() try {
  const auto rank_file = runs / "v5-residual-ranking-XXXSWRmG/report.json";
  const auto rank_pin = hash(rank_file);
  check(rank_pin ==
            "c83b0ad78c9d2054f17580b1047a5ec2ab30f833810e415391265be4341328c9",
        "changed residual ranking");
  const auto ranking = load(rank_file);
  check(ranking["open_families"][3]["requests"] ==
                string_array({"H041", "H047"}) &&
            ranking["open_families"][4]["requests"] == string_array({"H010"}),
        "changed open inventory and publishing cohort");
  const auto recipes_file = root / "shared/request-recipes.json";
  const auto recipe_pin = hash(recipes_file);
  check(recipe_pin == ranking["recipes_sha256"].asString(),
        "changed supplied recipes");
  const auto recipes = load(recipes_file);
  const auto parent = runs / "v5-queue-XXH5XMi9/stage/package";
  const auto bundle = parent / "bundle";
  const auto parent_pin = hash(parent / "manifest.json");
  check(parent_pin == ranking["parent_manifest_sha256"].asString(),
        "changed V5 parent");
  verify_bundle(
      bundle,
      "436db92629463daee559de2cbbc1c81519e12306d5bf01cc6d2883f3140e779c");
  const auto source = bundle / "qualifications/shared/inputs/source.json";
  const auto source_pin = hash(source);
  check(source_pin == "cfbd58d26ebaab781ff61c4859b5fa80b62a8dae1c113715b498ba5c"
                      "4af9bdb1" &&
            load(source)["kernel_sha256"] ==
                hash(bundle / "runtime/duckdb/lib/libbrick_shared_inputs.so"),
        "changed retained XML source or kernel");
  const auto active = fs::canonical(volume / "unllm/boilerplate/active");
  const auto active_pin = hash(active / "SHA256SUMS");
  check(active_pin == ranking["active_sha256"].asString(),
        "changed published catalog");

  const auto work = fresh_run("v5-residual-closure-replay");
  std::cout << work.string() << std::endl;
  auto report = object(
      {{"active_path", active.string()},
       {"active_sha256", active_pin},
       {"ranking_sha256", rank_pin},
       {"recipes_sha256", recipe_pin},
       {"parent_manifest_sha256", parent_pin},
       {"xml_source_sha256", source_pin},
       {"xml_oracle_version", xmlParserVersion},
       {"tools", tools()},
       {"xml_reader",
        exact_row(bundle, "xml_records", "in:application/xml", "rows")},
       {"tab_reader", exact_row(bundle, "tab_records",
                                "in:text/tab-separated-values", "rows")}});
  report["tools"]["unllm_logic_worker"] =
      hash(main_repo / "build/native/unllm_logic_worker");

  check(
      oracle("H010", stock) == array({"X\nC\nA\nB\n"}) &&
          oracle("H010", stock, "deduplicate-first") ==
              array({"B\nC\nA\nX\n"}) &&
          oracle("H041", feed) ==
              array(
                  {R"(["Earlier","Shared & Name","Twin","live only","twin"])"}) &&
          oracle("H041", feed, "deduplicate-before-live") ==
              array({R"(["Earlier","live only","twin"])"}) &&
          oracle("H047", feed) ==
              array({"Alpha\nbeta\n", "Cold\nDora\nZulu\n"}),
      "lost a frozen prose-obligation distinction");

  const auto archive = runs / "english-audit-bcsgf8pi/replay-staged";
  const auto historical = runs / "shared-review-5ui86km8/transfer";
  std::vector<Path> programs;
  for (const auto &id : requests) {
    const auto &record = ranking["requests"][id];
    const auto dir = archive / id;
    const auto saved_pin = hash(dir / response);
    const auto saved = load(dir / response);
    auto supplied = saved;
    auto &roles = supplied["roles"];
    check(roles.size() == recipes[id].size() &&
              saved_pin == record["reply_sha256"].asString() &&
              hash(dir / "request.txt") ==
                  record["request_sha256"].asString() &&
              record["previously_verified"] == false,
          "changed ranked prose-obligation case " + id);
    auto &entry = report["requests"][id];
    entry["reply_sha256"] = saved_pin;
    Strings names;
    for (unsigned slot = 0; slot < roles.size(); ++slot) {
      auto &role = roles[slot];
      const auto &ranked = record["roles"][role["id"].asString()];
      const auto steps = operations(recipes[id][slot]);
      check(role["output_type"] == ranked["output_type"] &&
                role["requirement"]["operations"] ==
                    ranked["archived_operations"] &&
                role["requirement"]["uncovered"] == ranked["uncovered"] &&
                steps == ranked["supplied_operations"],
            "changed ranked role " + id);
      role["requirement"]["operations"] = steps;
      role["requirement"]["uncovered"] = array();
      names.push_back(
          "result-" + std::to_string(slot + 1) +
          (role["output_type"] == "application/json" ? ".json" : ".txt"));
    }
    const auto fixtures = historical / id / "fixtures";
    const std::string input = id == "H010" ? "stock.tsv" : "feed.xml";
    auto inputs = object();
    for (const auto &split : splits)
      inputs[split] = hash(fixtures / split / "input" / input);
    entry["fixture_input_sha256"] = inputs;
    const std::map<std::string, std::string> fresh{
        {"main", id == "H010" ? stock : feed},
        {"boundary", id == "H010" ? "" : draft_only}};
    for (const auto &[name, data] : fresh)
      save(work / "inputs" / id / name / input, data);
    Strings arms{"reference"};
    append(arms, variants.at(id));
    for (const auto &arm : arms) {
      const auto file = work / "specs" / arm / (id + ".json");
      write(file, arm == "reference"  ? supplied
                  : arm == "archived" ? saved
                                      : change(supplied, saved, arm));
      const auto result = trial(work, arm, id, bundle, file, input, inputs);
      const auto trial_dir = work / arm / id;
      const auto exe = trial_dir / "run/project/converter";
      entry["runs"][arm] = result;
      write(work / "progress.json", report);
      check(fs::is_regular_file(exe) && result["roles"].size() == roles.size(),
            "incomplete prose-obligation execution for " + id);
      programs.push_back(exe);
      for (unsigned slot = 0; slot < roles.size(); ++slot) {
        const auto &role = result["roles"][std::to_string(slot)];
        const auto &chain = role["steps"];
        const auto &ranked = record["roles"][roles[slot]["id"].asString()];
        if (arm == "archived") {
          check(role["status"] == "partial" && ports(role).empty() &&
                    chain == ranked["archived"]["routes"][0]["steps"] &&
                    result["scores"] == scores(load(dir / "receipt.json")),
                "archived partial route changed for " + id);
        } else {
          check(complete(role), "supplied role did not complete for " + id);
          if (arm == "reference")
            check(chain == ranked["supplied"]["routes"][0]["steps"],
                  "emitted chain differs from ranked plan for " + id);
          if (arm == "xpath-then-unique")
            check(selects(role, "xml_text") && selects(role, "unique_lines"),
                  "minimal XPath route selected different operations");
        }
      }

      for (const auto &split : splits) {
        const auto fixture = fixtures / split;
        const auto data = bytes(fixture / "input" / input);
        auto saved_outputs = array();
        for (const auto &name : names)
          saved_outputs.append(bytes(fixture / "output" / name));
        const auto intended = oracle(id, data);
        const auto predicted = oracle(id, data, arm);
        const auto &actual = result["outputs"][split];
        check(matches(id, saved_outputs, intended) &&
                  matches(id, actual, predicted) &&
                  result["scores"][split] == (intended == predicted),
              "saved output differs from oracle for " + id + " " + arm + " " +
                  split + ": " + dump(actual));
        if (arm == "reference" || arm == "xpath-then-unique")
          check(actual == saved_outputs,
                "saved output changed at the byte level");
      }
      if (arm != "reference" && arm != "xpath-then-unique")
        check(oracle(id, fresh.at("main"), arm) != oracle(id, fresh.at("main")),
              "fresh fixture did not distinguish " + arm);
      for (const auto &[name, data] : fresh) {
        const auto input_file = work / "inputs" / id / name / input;
        const auto expected = oracle(id, data, arm);
        const auto actual = path_outputs(work / "witness" / id / arm / name,
                                         input_file, exe, bundle, names);
        check(matches(id, actual, expected), "fresh output differs for " + id +
                                                 " " + arm + " " + name + ": " +
                                                 dump(actual));
        entry["witness"][arm][name] =
            object({{"input_sha256", hash(input_file)}, {"outputs", actual}});
      }
      write(work / "progress.json", report);
    }
  }
  report["cohort"] = cohort(ranking, rank_pin, report["requests"]);
  check(hash(active / "SHA256SUMS") == active_pin, "published catalog changed");
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
