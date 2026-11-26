#include "row-trial.hpp"
#include <algorithm>
#include <iostream>
#include <sstream>

using namespace miner;
using namespace row_replay;
namespace {
const std::string id = "H080";
const std::string uri_list = "text/uri-list";
const std::string json = "application/json";
const Path response = "run/calls/interpret/response.json";
const Strings splits{"ordinary", "boundary", "empty", "transfer", "sparse"};
const std::string witness = "# HTTPS://example.org/comment#omit\r\n\r\n"
                            "https://example.org/v%23no-fragment\r\n"
                            "HtTpS://EXAMPLE.org/Case%2fNext?q=%2B#Alpha\n"
                            "http://example.org/ignore#fragment\r\n"
                            "https://example.org/Case%2fNext?q=%2B#Alpha\r\n"
                            "https://example.org/empty#\n"
                            "HTTPS://example.org/Case%2fNext?q=%2B#Alpha\r\n"
                            "HTTPS://example.org/Case%2fNext?q=%2B#Alpha\n"
                            "https://example.org/Case%2FNext?q=%2B#alpha\r\n"
                            "https://example.org/query?v=%23\n"
                            "httpsx://example.org/skip#x\r\n"
                            "https://example.org/empty#tail";

struct Policy {
  bool literal_prefix = false;
  bool require_fragment = true;
  bool strip_fragment = false;
  bool unique = true;
  bool sort = true;
};

// On these well-formed bookmark inputs the delimiters provide independent
// evidence without reparsing or normalizing any URI components.
J reference(const std::string &data, Policy policy = {}) {
  std::istringstream lines(data);
  std::string original;
  Strings selected;
  std::set<std::string> seen;
  while (std::getline(lines, original)) {
    if (original.ends_with('\r'))
      original.pop_back();
    if (original.empty() || original.starts_with('#'))
      continue;
    auto scheme = original.substr(0, original.find(':'));
    for (auto &ch : scheme)
      if (ch >= 'A' && ch <= 'Z')
        ch = char(ch - 'A' + 'a');
    if (scheme != "https" ||
        (policy.literal_prefix && !original.starts_with("https://")))
      continue;
    const auto fragment = original.find('#');
    if (policy.require_fragment && fragment == std::string::npos)
      continue;
    if (policy.strip_fragment && fragment != std::string::npos)
      original.erase(fragment);
    if (!policy.unique || seen.insert(original).second)
      selected.push_back(original);
  }
  if (policy.sort)
    std::sort(selected.begin(), selected.end());
  return string_array(selected);
}

J without_operation(J spec, unsigned index, const std::string &expected) {
  auto &steps = spec["roles"][0]["requirement"]["operations"];
  J removed;
  check(steps.removeIndex(index, &removed) && removed["outcome"] == expected,
        "changed URI recipe operation " + expected);
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
  const auto reader_binding =
      exact_row(base, "uri_records", "in:" + uri_list, "rows");
  const auto source_file = base / "qualifications/shared/inputs/source.json";
  const auto source_pin = hash(source_file);
  const auto source = load(source_file);
  check(source_pin == "cfbd58d26ebaab781ff61c4859b5fa80b62a8dae1c113715b498ba5c"
                      "4af9bdb1" &&
            source["kernel_sha256"] ==
                hash(base / "runtime/duckdb/lib/libbrick_shared_inputs.so") &&
            source["runtime_sha256"]["liburiparser.so.1"] ==
                hash(base / "runtime/duckdb/lib/liburiparser.so.1"),
        "changed URI kernel or lineage");
  const auto active = fs::canonical(volume / "unllm/boilerplate/active");
  const auto active_pin = hash(active / "SHA256SUMS");
  check(active_pin ==
            "e4564e3079f0b32b3379a411b170ce9af61ec80ab7938b6a974e724abd538328",
        "changed published context");
  const auto ranking = runs / "ranked-gaps-5x2_hyp7/ranking.json";
  const auto ranking_pin = hash(ranking);
  check(ranking_pin ==
            "3d902a86f77c7159581a1721cf07c96ccd7b0147259ffdee6edce8f976119495",
        "changed archived ranking");
  const auto archive = runs / "english-audit-bcsgf8pi/replay-staged";
  const auto stops =
      load(ranking)["stops"]["no complete chain for the requested operations"];
  Strings cohort;
  for (const auto &r : stops)
    if (load(archive / r.asString() / response)["input_type"] == uri_list)
      cohort.push_back(r.asString());
  check(cohort == Strings{id} && stops.size() == 13,
        "changed disconnected URI cohort");
  const auto original =
      volume / "unllm/experiments/boilerplate-B42-VrtYbHSf/fresh-staged";
  const auto archived_pin = hash(archive / id / response);
  check(archived_pin == hash(original / id / response),
        "changed original reply");
  const auto saved = load(archive / id / response);
  const auto role = saved["roles"][0];
  check(saved["roles"].size() == 1 && role["output_type"] == json &&
            role["requirement"]["operations"] ==
                operations(array({array({"named_sets", object()}),
                                  array({"sort_lines", object()})})),
        "changed archived bookmark operations");

  const auto recipes = root / "shared/request-recipes.json";
  const auto recipe_pin = hash(recipes);
  check(recipe_pin ==
            "768676cbc34ef7db9d869520bd3d26d86123f49a3c9803901eb5653de3a6f49b",
        "changed supplied recipe");
  auto fixed = saved;
  auto &req = fixed["roles"][0]["requirement"];
  req["operations"] = operations(load(recipes)[id][0]);
  req["uncovered"] = array();
  const auto steps = req["operations"];
  const auto read = steps[0];
  check(steps.size() == 6 && read["outcome"] == "uri_records" &&
            read["parameters"] ==
                object({{"fields", "original,scheme,has_fragment"}}) &&
            steps[1]["parameters"]["value"] == "https" &&
            steps[2]["parameters"]["column"] == 2 &&
            steps[3]["outcome"] == "ordered_column",
        "changed URI projection or filters");

  const auto work = fresh_run("v5-uri-fragment-replay");
  std::cout << work.string() << std::endl;
  auto report = object({{"request", id},
                        {"active_path", active.string()},
                        {"active_sha256", active_pin},
                        {"parent_manifest_sha256", parent_pin},
                        {"ranking_sha256", ranking_pin},
                        {"recipes_sha256", recipe_pin},
                        {"archived_sha256", archived_pin},
                        {"reader", reader_binding},
                        {"reader_source_sha256", source_pin},
                        {"tools", tools()}});
  const auto archived = plan_probe(base, work / "parameters/archived", saved);
  const auto previous = load(archive / id / "run/components/emission.json");
  const auto gap = archived["gaps"]["r"].asString();
  const auto old_gap = previous["gaps"][role["id"].asString()].asString();
  const std::string prefix = "No provider chain for requested outcomes: ";
  check(gap.starts_with(prefix) && old_gap.starts_with(prefix) &&
            parse(gap.substr(prefix.size())) ==
                role["requirement"]["operations"] &&
            parse(old_gap.substr(prefix.size())) ==
                role["requirement"]["operations"] &&
            archived["validation"].empty() && archived["routes"].empty(),
        "changed archived bookmark chain stop");
  report["plans"]["archived"] = archived;
  report["incompatible_pair"] =
      array({exact_row(base, "named_sets", "in:" + json, "prerequisites"),
             exact_row(base, "sort_lines", "strings", "strings")});
  auto reader_only = saved;
  reader_only["roles"][0]["requirement"]["operations"][0] = read;
  const auto partial =
      plan_probe(base, work / "parameters/reader-only", reader_only);
  check(partial["validation"].empty() && partial["routes"].size() == 1 &&
            partial["routes"][0]["ports"] ==
                array({array({"rows", "strings"})}) &&
            partial["routes"][0]["uncovered"] ==
                role["requirement"]["uncovered"],
        "reader-only intervention altered other obligations");
  report["plans"]["reader-only"] = partial;

  const auto historical =
      runs / "shared-review-5ui86km8/transfer" / id / "fixtures";
  auto input_hashes = object();
  for (const auto &split : splits)
    input_hashes[split] = hash(historical / split / "input/saved.uri");
  report["input_sha256"] = input_hashes;
  const auto input = work / "mixed-bookmarks.uri";
  save(input, witness);
  report["witness_input_sha256"] = hash(input);
  const auto expected = reference(witness);
  check(expected == array({"HTTPS://example.org/Case%2fNext?q=%2B#Alpha",
                           "HtTpS://EXAMPLE.org/Case%2fNext?q=%2B#Alpha",
                           "https://example.org/Case%2FNext?q=%2B#alpha",
                           "https://example.org/Case%2fNext?q=%2B#Alpha",
                           "https://example.org/empty#",
                           "https://example.org/empty#tail"}),
        "independent mixed bookmark expectation drifted");
  std::vector<Path> programs;
  auto replay = [&](const std::string &arm, const J &spec, Policy policy = {}) {
    const auto spec_file = work / "specs" / (arm + ".json");
    write(spec_file, spec);
    const auto result =
        trial(work, arm, id, base, spec_file, "saved.uri", input_hashes);
    check(result["roles"].size() == 1 && complete(result["roles"]["0"]) &&
              selects(result["roles"]["0"], "uri_records") &&
              selects(result["roles"]["0"], "write_texts") &&
              !result["program_sha256"].isNull(),
          "URI comparison did not emit " + arm);
    const auto dir = work / arm / id;
    const auto exe = dir / "run/project/converter";
    programs.push_back(exe);
    report["runs"][arm] = result;
    write(work / "progress.json", report);
    const auto output = path_output(dir / "witness", input, exe, base);
    const auto actual = parse(output);
    check(actual == reference(witness, policy) &&
              ((arm == "fixed") == (actual == expected)),
          "mixed bookmark output disagreed with independent reference for " +
              arm);
    report["witness"][arm] =
        object({{"decoded", actual}, {"output_sha256", sha(output)}});
    return result;
  };
  const auto accepted = replay("fixed", fixed);
  check(accepted["verified"] == true, "full bookmark recipe did not pass");
  for (const auto &split : splits) {
    const auto fixture = historical / split;
    const auto raw = accepted["outputs"][split][0].asString();
    check(raw == bytes(fixture / "output/result-1.json") &&
              parse(raw) == reference(bytes(fixture / "input/saved.uri")),
          "archived output differs from saved bytes or the independent "
          "reference");
  }

  auto literal = fixed;
  auto &literal_steps = literal["roles"][0]["requirement"]["operations"];
  auto amended = array();
  for (unsigned i = 0; i < steps.size(); ++i) {
    amended.append(steps[i]);
    if (i == 3)
      amended.append(
          object({{"outcome", "prefixed_lines"},
                  {"parameters", object({{"prefix", "https://"}})}}));
  }
  literal_steps = amended;
  replay("lowercase-prefix", literal, {.literal_prefix = true});
  replay("without-fragment-test", without_operation(fixed, 2, "filter_records"),
         {.require_fragment = false});
  auto stripped = fixed;
  stripped["roles"][0]["requirement"]["operations"][0]["parameters"]["fields"] =
      "base,scheme,has_fragment";
  replay("fragment-free-base", stripped, {.strip_fragment = true});
  replay("without-dedup", without_operation(fixed, 4, "unique_lines"),
         {.unique = false});
  replay("without-sort", without_operation(fixed, 5, "sort_lines"),
         {.sort = false});

  check(tools() == report["tools"] &&
            hash(active / "SHA256SUMS") == active_pin &&
            hash(input) == report["witness_input_sha256"].asString(),
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
