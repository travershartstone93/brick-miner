#include "row-trial.hpp"
#include "xml-fixture.hpp"
#include <charconv>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <tuple>

using namespace miner;
using namespace row_replay;
namespace {
const std::string tsv = "text/tab-separated-values";
const std::string xml = "application/xml";
const std::string empty_stop = "Expected between one and 32 ordered outcomes";
const Path response = "run/calls/interpret/response.json";
const Strings splits{"ordinary", "boundary", "empty", "transfer", "sparse"};
const Strings requests{"H009", "H042"};

const std::string stock_witness =
    "Z\tR\t4\nQ\tR\t0\nA\tL\t0\nZ\tR\t0\nA\tL\t-3\nQ\tL\t-1\n"
    "Z\tM\t7\nA\tL\t8\nQ\tM\t0\nQ\tM\t1\nZ\tM\t0\nZ\tM\t10\n"
    "P\tR\t-4\nP\tR\t1\nN\tR\t-0\nN\tR\t1\nJ\tR\t0\nJ\tR\t-3\nA\tY\t9\n";
const std::string article_witness = R"(<feed>
  <article editor="Zed" section="One" state="draft"><title>A &amp; B</title></article>
  <article editor="Al&#105;ce" section="One" state="live"><title>A &amp; B</title></article>
  <article editor="Bo" section="One" state="draft"><title>Edition</title></article>
  <article editor="Z&#101;d" section="Two" state="draft"><title>A &#38; B</title></article>
  <article editor="Bo" section="One" state="live"><title>Edition</title></article>
  <article editor="Dora" section="Other" state="draft"><title>Only</title></article>
  <article editor="Alice" section="Two" state="live"><title>A &#38; B</title></article>
  <article editor="Bo" section="Two" state="draft"><title>Edition</title></article>
  <article editor="Elle" section="Other" state="live"><title>Single</title></article>
  <article editor="Alice" section="One" state="live"><title>Other</title></article>
</feed>)";

std::string stock_reference(const std::string &data, std::string_view arm) {
  std::istringstream input(data);
  std::set<std::string> zero_seen, previous_zero, recovered;
  std::map<std::string, size_t> by_sku;
  std::string output;
  size_t position = 0;
  for (std::string line; std::getline(input, line);) {
    ++position;
    if (line.ends_with('\r'))
      line.pop_back();
    const auto first = line.find('\t'), second = line.find('\t', first + 1);
    check(first != std::string::npos && second != std::string::npos,
          "expected three inventory fields");
    const auto sku = line.substr(0, first);
    const auto *end = line.data() + line.size();
    int64_t value = 0;
    const auto parsed = std::from_chars(line.data() + second + 1, end, value);
    check(parsed.ec == std::errc{} && parsed.ptr == end,
          "expected a signed inventory integer");
    const auto &history = arm == "previous-zero" ? previous_zero : zero_seen;
    if (value > 0 && history.contains(sku) && recovered.insert(sku).second) {
      if (arm == "group-order")
        by_sku.emplace(sku, position);
      else
        output += sku + "\t" + std::to_string(position) + "\n";
    }
    if (value == 0) {
      zero_seen.insert(sku);
      previous_zero.insert(sku);
    } else {
      previous_zero.erase(sku);
    }
  }
  for (const auto &[sku, pos] : by_sku)
    output += sku + "\t" + std::to_string(pos) + "\n";
  return output;
}

std::string publishing_reference(const std::string &data,
                                 std::string_view arm) {
  std::map<std::string, std::pair<size_t, size_t>> counts;
  std::set<std::tuple<std::string, std::string, std::string>> seen;
  for (const auto &[editor, section, state, title] : read_feed(data)) {
    if (arm == "deduplicate-titles" &&
        !seen.emplace(editor, state, title).second)
      continue;
    auto &[draft, live] = counts[editor];
    if (state == "draft")
      ++draft;
    else {
      check(state == "live", "expected draft or live");
      ++live;
    }
  }
  std::string output;
  for (const auto &[editor, count] : counts) {
    const auto &[draft, live] = count;
    if (arm != "require-both" || (draft != 0 && live != 0))
      output += editor + "\t" + std::to_string(draft) + "\t" +
                std::to_string(live) + "\n";
  }
  return output;
}

J op(const std::string &effect, const J &parameters) {
  return object({{"outcome", effect}, {"parameters", parameters}});
}

J int_filter(int col, const std::string &comparison) {
  return op("filter_records", object({{"column", col},
                                      {"comparison", comparison},
                                      {"type", "int"},
                                      {"value", "0"}}));
}

J variant(J steps, const std::string &arm) {
  if (arm == "group-order") {
    check(steps[4]["outcome"] == "select_group_row",
          "changed recovery selector");
    steps[4]["parameters"]["result_order"] = "group";
    return steps;
  }
  if (arm == "previous-zero") {
    check(steps[0]["outcome"] == "first_matching_field",
          "changed recovery history");
    auto result =
        array({op("previous_field", object({{"keys", "0"}, {"column", 2}})),
               steps[1], int_filter(4, "eq")});
    for (unsigned i = 2; i < steps.size(); ++i)
      result.append(steps[i]);
    return result;
  }
  check(steps[0]["outcome"] == "xml_records" &&
            steps[1]["outcome"] == "annotate_group_measure" &&
            steps[2]["outcome"] == "annotate_group_measure",
        "changed article count recipe");
  auto result = array({steps[0]});
  if (arm == "deduplicate-titles") {
    result.append(
        op("group_rows", object({{"keys", "0,2,3"}, {"group_order", "asc"}})));
    result.append(
        op("select_group_row", object({{"which", "first"},
                                       {"order_by", ""},
                                       {"result_order", "source_asc"}})));
  }
  result.append(steps[1]);
  result.append(steps[2]);
  if (arm == "require-both") {
    result.append(int_filter(4, "gt"));
    result.append(int_filter(5, "gt"));
  }
  for (unsigned i = 3; i < steps.size(); ++i)
    result.append(steps[i]);
  return result;
}

struct Evidence {
  std::string directory, sha256, section, arm;
};
const std::vector<Evidence> earlier{
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
    {"v5-unordered-telemetry-replay-XXj6C6EH",
     "13e1d9667144ded4a2526a9fe78e1b12599c66ff41344914b14607e68c4ef1d6",
     "telemetry", "reference"}};

J cohort(const J &ranking, const J &current) {
  auto results = object();
  auto read = [&](const J &cases, const std::string &arm, const J &pin) {
    for (const auto &id : cases.getMemberNames()) {
      const auto &entry = cases[id];
      const auto &run =
          id == "H086" ? entry["reused"]["arms"]["fixed"] : entry["runs"][arm];
      check(!results.isMember(id) && run["verified"] == true &&
                complete(run["roles"]["0"]) &&
                run["scores"].size() == splits.size(),
            "missing or duplicate verified zero-operation request " + id);
      for (const auto &split : splits)
        check(run["scores"][split] == true,
              "failed fixture " + id + " " + split);
      results[id] = object({{"report_sha256", pin},
                            {"arm", id == "H086" ? "fixed" : arm},
                            {"program_sha256", run["program_sha256"]},
                            {"scores", run["scores"]}});
    }
  };
  for (const auto &e : earlier) {
    const auto file = runs / e.directory / "report.json";
    check(hash(file) == e.sha256,
          "changed retained cohort evidence " + e.section);
    const auto report = load(file);
    read(report[e.section], e.arm, e.sha256);
  }
  read(current, "reference", J());
  check(results.size() == 32 &&
            results.getMemberNames() == ranking["archive"].getMemberNames(),
        "verified cohort differs from the 32 archived zero-operation replies");
  return object({{"verified_requests", results.size()},
                 {"saved_fixture_passes", results.size() * splits.size()},
                 {"requests", results}});
}
} // namespace

int main() try {
  const auto file = runs / earlier[0].directory / "report.json";
  const auto rate_pin = hash(file);
  check(rate_pin == earlier[0].sha256, "changed zero-operation ranking");
  const auto ranking = load(file);
  check(ranking["family_frequency"][5]["name"] == "inventory" &&
            ranking["family_frequency"][5]["requests"] == array({"H009"}) &&
            ranking["family_frequency"][6]["name"] == "publishing" &&
            ranking["family_frequency"][6]["requests"] == array({"H042"}),
        "changed singleton membership");
  const auto recipe = root / "shared/request-recipes.json";
  const auto recipe_pin = hash(recipe);
  check(recipe_pin ==
            "768676cbc34ef7db9d869520bd3d26d86123f49a3c9803901eb5653de3a6f49b",
        "changed supplied recipes");
  const auto recipes = load(recipe);
  const auto parent = runs / "v5-queue-XXH5XMi9/stage/package";
  const auto writer_stage = runs / "v5-tsv-writer-XXAf0x0y/stage";
  const auto base = parent / "bundle", with_tsv = writer_stage / "bundle";
  const auto base_pin = hash(parent / "manifest.json");
  const auto writer_pin = hash(writer_stage / "manifest.json");
  check(base_pin == "d15aee96917ff440f58f76270e3ca48575268dcfcf2bce951805c865aa"
                    "e3af63" &&
            writer_pin == "0047b47009df08f7ee228729eb46b4438ec9d59eadd7c44647ec"
                          "6761eeabb994",
        "changed catalog stages");
  verify_bundle(
      base, "436db92629463daee559de2cbbc1c81519e12306d5bf01cc6d2883f3140e779c");
  verify_bundle(
      with_tsv,
      "b2f8a40d44b1589779fc82fcfa23995df5cb2b2432167e9383f6f22e9c828d54");
  const auto writer =
      exact_row(with_tsv, "write_tsv_lines", "strings", "out:" + tsv);
  check(remove_binding(read_tree(with_tsv), writer).at("SHA256SUMS") ==
            bytes(base / "SHA256SUMS"),
        "catalog differs outside the TSV writer");
  const auto source = base / "qualifications/shared/inputs/source.json";
  const auto source_pin = hash(source);
  check(source_pin == "cfbd58d26ebaab781ff61c4859b5fa80b62a8dae1c113715b498ba5c"
                      "4af9bdb1" &&
            load(source)["kernel_sha256"] ==
                hash(base / "runtime/duckdb/lib/libbrick_shared_inputs.so"),
        "changed retained XML source or kernel");
  const auto active = fs::canonical(volume / "unllm/boilerplate/active");
  const auto active_pin = hash(active / "SHA256SUMS");
  check(active_pin ==
            "e4564e3079f0b32b3379a411b170ce9af61ec80ab7938b6a974e724abd538328",
        "changed published catalog");

  const auto work = fresh_run("v5-unordered-singletons-replay");
  std::cout << work.string() << std::endl;
  auto report = object({{"active_path", active.string()},
                        {"active_sha256", active_pin},
                        {"frequency_report_sha256", rate_pin},
                        {"recipes_sha256", recipe_pin},
                        {"parent_manifest_sha256", base_pin},
                        {"writer_manifest_sha256", writer_pin},
                        {"xml_source_sha256", source_pin},
                        {"xml_oracle_version", xmlParserVersion},
                        {"writer", writer},
                        {"tools", tools()}});
  report["tools"]["unllm_logic_worker"] =
      hash(main_repo / "build/native/unllm_logic_worker");

  check(stock_reference(stock_witness, "reference") ==
                "Z\t7\nA\t8\nQ\t10\nN\t16\n" &&
            stock_reference(stock_witness, "previous-zero") ==
                "Z\t7\nQ\t10\nN\t16\n" &&
            stock_reference(stock_witness, "group-order") ==
                "A\t8\nN\t16\nQ\t10\nZ\t7\n" &&
            publishing_reference(article_witness, "reference") ==
                "Alice\t0\t3\nBo\t2\t1\nDora\t1\t0\nElle\t0\t1\nZed\t2\t0\n" &&
            publishing_reference(article_witness, "require-both") ==
                "Bo\t2\t1\n" &&
            publishing_reference(article_witness, "deduplicate-titles") ==
                "Alice\t0\t2\nBo\t1\t1\nDora\t1\t0\nElle\t0\t1\nZed\t1\t0\n",
        "lost a frozen distinction in the singleton witnesses");

  const auto archive = runs / "english-audit-bcsgf8pi/replay-staged";
  const auto historical = runs / "shared-review-5ui86km8/transfer";
  std::vector<Path> programs;
  for (const auto &id : requests) {
    const bool inventory = id == "H009";
    const auto &record = ranking["archive"][id];
    const auto dir = archive / id;
    const auto saved_pin = hash(dir / response);
    const auto saved = load(dir / response);
    const auto &role = saved["roles"][0];
    const auto steps = operations(recipes[id][0]);
    check(saved_pin == record["reply_sha256"].asString() &&
              hash(record["reply_source"].asString()) == saved_pin &&
              hash(dir / "request.txt") ==
                  record["request_sha256"].asString() &&
              saved["input_type"] == (inventory ? tsv : xml) &&
              saved["roles"].size() == 1 && role["output_type"] == tsv &&
              role["requirement"]["operations"] == array(),
          "changed saved singleton context for " + id);
    auto &entry = report["singletons"][id];
    entry["archive"] = record;
    const auto archived =
        plan_probe(base, work / "plans" / id / "archived", saved);
    check(archived["gaps"]["r"] == empty_stop && archived["routes"].empty(),
          "changed archived operation stop for " + id + ": " + dump(archived));
    entry["plans"]["archived"] = archived;
    auto supplied = saved;
    supplied["roles"][0]["requirement"]["uncovered"] = array();
    supplied["roles"][0]["requirement"]["operations"] = steps;
    const auto gap =
        plan_probe(base, work / "plans" / id / "without-writer", supplied);
    check(gap["routes"].size() == 1 && gap["validation"].empty() &&
              gap["routes"][0]["ports"] ==
                  array({array({"strings", "out:" + tsv})}),
          "changed singleton sink gap for " + id + ": " + dump(gap));
    entry["plans"]["without-writer"] = gap;

    const auto fixtures = historical / id / "fixtures";
    const std::string input = inventory ? "stock.tsv" : "feed.xml";
    const auto oracle = inventory ? stock_reference : publishing_reference;
    const Strings arms =
        inventory ? Strings{"reference", "previous-zero", "group-order"}
                  : Strings{"reference", "require-both", "deduplicate-titles"};
    const std::string witness = inventory ? stock_witness : article_witness;
    const std::string boundary =
        inventory
            ? "A\tW\t1\nB\tW\t0\nA\tW\t0\nB\tW\t-3\n"
            : R"(<feed><article editor="Draft" section="X" state="draft"><title>One</title></article><article editor="Live" section="Y" state="live"><title>Two</title></article></feed>)";
    auto inputs = object();
    for (const auto &split : splits)
      inputs[split] = hash(fixtures / split / "input" / input);
    entry["fixture_input_sha256"] = inputs;
    for (const auto &arm : arms) {
      auto spec = supplied;
      if (arm != "reference")
        spec["roles"][0]["requirement"]["operations"] = variant(steps, arm);
      const auto file = work / "specs" / arm / (id + ".json");
      write(file, spec);
      const auto result = trial(work, arm, id, with_tsv, file, input, inputs);
      const auto &emitted = result["roles"]["0"];
      const auto exe = work / arm / id / "run/project/converter";
      entry["runs"][arm] = result;
      write(work / "progress.json", report);
      check(complete(emitted) && fs::is_regular_file(exe) &&
                emitted["steps"][0]["provider"] ==
                    (inventory ? "tab_records" : "op_0_xml_records") &&
                emitted["steps"][emitted["steps"].size() - 1]["provider"] ==
                    "write_tsv_lines",
            "unexpected singleton route for " + id + " " + arm);
      programs.push_back(exe);
      for (const auto &split : splits) {
        const auto fixture = fixtures / split;
        const auto data = bytes(fixture / "input" / input);
        const auto correct = oracle(data, "reference");
        const auto predicted = oracle(data, arm);
        check(correct == bytes(fixture / "output/result-1.txt") &&
                  result["outputs"][split][0] == predicted &&
                  result["scores"][split] == (predicted == correct),
              "archived output differs from oracle for " + id + " " + arm +
                  " " + split);
      }
      for (const auto &[name, data] :
           {std::pair{"main", witness}, std::pair{"boundary", boundary}}) {
        const auto e = extra_case(work / "witness" / id / arm / name, exe,
                                  with_tsv, data, array({oracle(data, arm)}));
        check(e["passed"] == true, "singleton witness differs for " + id + " " +
                                       arm + " " + name + ": " + dump(e));
        entry["witness"][arm][name] = e;
      }
      write(work / "progress.json", report);
    }
  }
  report["cohort"] = cohort(ranking, report["singletons"]);
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
