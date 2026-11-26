#include "row-trial.hpp"
#include <algorithm>
#include <iostream>
#include <map>
#include <set>
#include <sstream>

using namespace miner;
using namespace row_replay;
namespace {
const std::string uri_list = "text/uri-list";
const std::string scalar = "gir_g_uri_list_extract_uris";
const Strings requests{"H071", "H072", "H074", "H078"};
const Strings splits{"ordinary", "boundary", "empty", "transfer", "sparse"};
const Path response = "run/calls/interpret/response.json";
const std::map<std::string, Strings> variants{
    {"H071", {"archived"}},
    {"H072", {"literal-prefix", "limit-first"}},
    {"H074", {"global-unique"}},
    {"H078", {"archived", "reverse-unique-reverse"}}};

const std::string a = "HtTpS://EXAMPLE.org/a%2f?p=%23#first";
const std::string b = "https://example.org/a%2f?p=%23#first";
const std::string c = "https://example.org/a%2F?p=%23#first";
const std::string d = "https://example.org/a%2f?p=%23#second";
const std::string http = "http://OTHER.org/non#stay";
const std::string local = "file:///tmp/f%23name";
const std::string opaque = "mailto:team@example.org";
const std::string witness = "# export https://example.org/ignore#comment\r\n" +
                            a + "\r\n# between duplicate URIs\n\r\n" + a +
                            "\n" + http + "\r\n" + b + "\n" + b + "\r\n" +
                            local + "\n" + c + "\r\n" + d + "\n" + a +
                            "\r\n# final separator\r\n" + opaque;

Strings bookmarks(const std::string &data) {
  Strings result;
  std::istringstream input(data);
  for (std::string line; std::getline(input, line);) {
    if (line.ends_with('\r'))
      line.pop_back();
    if (!line.empty() && !line.starts_with('#'))
      result.push_back(line);
  }
  return result;
}

std::string scheme_of(const std::string &uri) {
  const auto colon = uri.find(':');
  check(colon != std::string::npos && colon != 0,
        "oracle expects absolute URI");
  auto scheme = uri.substr(0, colon);
  for (auto &ch : scheme)
    if (ch >= 'A' && ch <= 'Z')
      ch += 'a' - 'A';
  return scheme;
}

Strings first_unique(const Strings &values) {
  Strings result;
  std::set<std::string> seen;
  for (const auto &v : values)
    if (seen.insert(v).second)
      result.push_back(v);
  return result;
}

Strings oracle(const std::string &id, const Strings &items,
               std::string_view arm = "reference") {
  if (id == "H071")
    return arm == "archived" ? Strings{}
                             : Strings(items.rbegin(), items.rend());
  if (id == "H072") {
    Strings https;
    for (const auto &item : items)
      if (arm == "literal-prefix" ? item.starts_with("https://")
                                  : scheme_of(item) == "https")
        https.push_back(item);
    if (arm == "limit-first" && https.size() > 3)
      https.resize(3);
    auto result = first_unique(https);
    if (result.size() > 3)
      result.resize(3);
    return result;
  }
  if (id == "H074") {
    if (arm == "global-unique")
      return first_unique(items);
    Strings result;
    for (const auto &item : items)
      if (result.empty() || item != result.back())
        result.push_back(item);
    return result;
  }

  check(id == "H078", "unknown URI request");
  if (arm == "archived")
    return first_unique(items);
  std::map<std::string, size_t> latest;
  for (size_t i = 0; i < items.size(); ++i)
    latest[items[i]] = i;
  Strings result;
  for (size_t i = 0; i < items.size(); ++i)
    if (latest.at(items[i]) == i)
      result.push_back(items[i]);
  return result;
}

std::string text_lines(const Strings &values) {
  return values.empty() ? std::string{} : join(values, "\n") + "\n";
}

bool matches(const std::string &id, const std::string &actual,
             const Strings &expected) {
  if (id == "H074")
    return actual == text_lines(expected);
  return parse(actual) == string_array(expected);
}

J op(const std::string &effect, const J &parameters = object()) {
  return object({{"outcome", effect}, {"parameters", parameters}});
}

J change(J spec, const std::string &arm) {
  auto &steps = spec["roles"][0]["requirement"]["operations"];
  if (arm == "literal-prefix") {
    check(steps.size() == 5 && steps[1]["outcome"] == "filter_records" &&
              steps[2]["outcome"] == "ordered_column",
          "changed shortlist filter");
    steps[1] = steps[2];
    steps[2] = op("prefixed_lines", object({{"prefix", "https://"}}));
  } else if (arm == "limit-first") {
    check(steps[3]["outcome"] == "unique_lines" &&
              steps[4]["outcome"] == "first_items",
          "changed shortlist limit");
    std::swap(steps[3], steps[4]);
  } else if (arm == "global-unique") {
    check(steps == array({op("adjacent_unique")}), "changed adjacency recipe");
    steps = array({op("unique_lines")});
  } else {
    check(arm == "reverse-unique-reverse", "unknown URI control");
    steps =
        array({op("reverse_order"), op("unique_lines"), op("reverse_order")});
  }
  return spec;
}

J reader_provenance(const Path &bundle) {
  const auto implicit = exact_row(bundle, scalar, "in:" + uri_list, "strings");
  const auto records =
      exact_row(bundle, "uri_records", "in:" + uri_list, "rows");
  const auto stage = runs / "v5-uri-reader-XXLemjgN/stage";
  const auto pin = hash(stage / "manifest.json");
  check(pin ==
            "d12d288ae54c51a2efbd301b4903b11565dd4052de6e4a18068d5261e42ff0ae",
        "changed retained URI reader evidence");
  const auto stage_manifest = load(stage / "manifest.json");
  for (const auto *file : {"source.json", "contract.json"})
    check(hash(stage / file) == stage_manifest["files"][file].asString(),
          "changed URI reader evidence member");
  const auto scalar_source = load(stage / "source.json");
  const auto glib_pin = hash("/usr/lib/libglib-2.0.so.0");
  check(scalar_source["binding"] == implicit &&
            scalar_source["generated"]["per-line"]["header_sha256"] ==
                hash(bundle / "headers" / implicit["header"].asString()) &&
            scalar_source["runtime"]["archive"]["files"]
                         ["usr/lib/libglib-2.0.so.0.9000.1"] == glib_pin,
        "retained scalar URI reader differs from scoped evidence");

  const auto source_file = bundle / "qualifications/shared/inputs/source.json";
  const auto source_pin = hash(source_file);
  const auto source = load(source_file);
  check(source_pin == "cfbd58d26ebaab781ff61c4859b5fa80b62a8dae1c113715b498ba5c"
                      "4af9bdb1" &&
            source["kernel_sha256"] ==
                hash(bundle / "runtime/duckdb/lib/libbrick_shared_inputs.so") &&
            source["runtime_sha256"]["liburiparser.so.1"] ==
                hash(bundle / "runtime/duckdb/lib/liburiparser.so.1"),
        "changed correlated URI reader runtime");
  return object({{"scalar", implicit},
                 {"correlated", records},
                 {"scalar_manifest_sha256", pin},
                 {"glib_sha256", glib_pin},
                 {"correlated_source_sha256", source_pin}});
}
} // namespace

int main() try {
  const auto rank_file = runs / "v5-residual-ranking-XXXSWRmG/report.json";
  const auto rank_pin = hash(rank_file);
  check(rank_pin ==
            "c83b0ad78c9d2054f17580b1047a5ec2ab30f833810e415391265be4341328c9",
        "changed residual ranking");
  const auto ranking = load(rank_file);
  check(ranking["open_families"][1]["name"] == "URI lists" &&
            ranking["open_families"][1]["requests"] == string_array(requests),
        "changed open URI cohort");
  const auto previous = runs / "v5-residual-zip-replay-XXRDOTGr/report.json";
  const auto previous_pin = hash(previous);
  check(previous_pin == "bc1d40444fd5e431806629b915754899ee9f8a172dc7526d582e26"
                        "ab502eda6f" &&
            load(previous)["archives"].size() == 4,
        "changed preceding checkpoint");
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
  const auto active = fs::canonical(volume / "unllm/boilerplate/active");
  const auto active_pin = hash(active / "SHA256SUMS");
  check(active_pin == ranking["active_sha256"].asString(),
        "changed published catalog");
  const auto lineage = reader_provenance(bundle);

  const auto work = fresh_run("v5-residual-uri-replay");
  std::cout << work.string() << std::endl;
  auto report = object({{"active_path", active.string()},
                        {"active_sha256", active_pin},
                        {"ranking_sha256", rank_pin},
                        {"previous_sha256", previous_pin},
                        {"recipes_sha256", recipe_pin},
                        {"parent_manifest_sha256", parent_pin},
                        {"reader_provenance", lineage},
                        {"tools", tools()}});
  report["tools"]["unllm_logic_worker"] =
      hash(main_repo / "build/native/unllm_logic_worker");
  const std::map<std::string, std::string> fresh{
      {"mixed", witness}, {"comments-only", "# first\r\n\r\n# final"}};
  const auto mixed = bookmarks(witness);
  check(mixed == Strings{a, a, http, b, b, local, c, d, a, opaque} &&
            oracle("H071", mixed) ==
                Strings{opaque, a, d, c, local, b, b, http, a, a} &&
            oracle("H072", mixed) == Strings{a, b, c} &&
            oracle("H072", mixed, "literal-prefix") == Strings{b, c, d} &&
            oracle("H072", mixed, "limit-first") == Strings{a, b} &&
            oracle("H074", mixed) ==
                Strings{a, http, b, local, c, d, a, opaque} &&
            oracle("H078", mixed) == Strings{http, b, local, c, d, a, opaque},
        "lost a frozen URI witness distinction");

  const auto archive = runs / "english-audit-bcsgf8pi/replay-staged";
  const auto historical = runs / "shared-review-5ui86km8/transfer";
  std::vector<Path> programs;
  for (const auto &id : requests) {
    const auto &record = ranking["requests"][id];
    const auto dir = archive / id;
    const auto saved_pin = hash(dir / response);
    const auto saved = load(dir / response);
    const auto &role = saved["roles"][0];
    const auto &ranked = record["roles"][role["id"].asString()];
    const auto steps = operations(recipes[id][0]);
    const bool plain = id == "H074";
    check(saved["input_type"] == uri_list && saved["roles"].size() == 1 &&
              saved_pin == record["reply_sha256"].asString() &&
              hash(dir / "request.txt") ==
                  record["request_sha256"].asString() &&
              record["previously_verified"] == false &&
              role["output_type"] ==
                  (plain ? "text/plain" : "application/json") &&
              role["output_type"] == ranked["output_type"] &&
              role["requirement"]["operations"] ==
                  ranked["archived_operations"] &&
              role["requirement"]["uncovered"] == ranked["uncovered"] &&
              steps == ranked["supplied_operations"],
          "changed ranked URI case " + id);
    auto &entry = report["uri"][id];
    entry["reply_sha256"] = saved_pin;
    auto supplied = saved;
    supplied["roles"][0]["requirement"]["operations"] = steps;
    supplied["roles"][0]["requirement"]["uncovered"] = array();
    const auto fixtures = historical / id / "fixtures";
    auto inputs = object();
    for (const auto &split : splits)
      inputs[split] = hash(fixtures / split / "input/saved.uri");
    entry["fixture_input_sha256"] = inputs;
    const std::string output_name = plain ? "result-1.txt" : "result-1.json";
    Strings arms{"reference"};
    append(arms, variants.at(id));
    for (const auto &arm : arms) {
      const auto file = work / "specs" / arm / (id + ".json");
      write(file, arm == "reference"  ? supplied
                  : arm == "archived" ? saved
                                      : change(supplied, arm));
      const auto result =
          trial(work, arm, id, bundle, file, "saved.uri", inputs);
      const auto trial_dir = work / arm / id;
      const auto exe = trial_dir / "run/project/converter";
      entry["runs"][arm] = result;
      write(work / "progress.json", report);
      const auto &emitted = result["roles"]["0"];
      const auto &chain = emitted["steps"];
      check(fs::is_regular_file(exe) && result["roles"].size() == 1 &&
                chain[chain.size() - 1]["provider"] ==
                    (plain ? "write_lines" : "write_texts") &&
                selects(emitted,
                        (arm == "archived" || arm == "reverse-unique-reverse" ||
                         id == "H071" || id == "H074")
                            ? scalar
                            : "uri_records"),
            "changed emitted URI provider chain for " + id + " " + arm);
      programs.push_back(exe);
      if (arm == "archived") {
        check(emitted["status"] == "partial" && ports(emitted).empty() &&
                  chain == ranked["archived"]["routes"][0]["steps"] &&
                  result["scores"] == scores(load(dir / "receipt.json")),
              "archived URI role diverged from ranked evidence");
      } else {
        check(complete(emitted), "supplied URI role did not complete");
        if (arm == "reference")
          check(chain == ranked["supplied"]["routes"][0]["steps"],
                "emitted URI chain differs from ranked plan " + id);
      }
      for (const auto &split : splits) {
        const auto fixture = fixtures / split;
        const auto items = bookmarks(bytes(fixture / "input/saved.uri"));
        const auto intended = oracle(id, items);
        const auto predicted = oracle(id, items, arm);
        const auto saved_output = bytes(fixture / "output" / output_name);
        const auto actual = result["outputs"][split][0].asString();
        check(matches(id, saved_output, intended) &&
                  matches(id, actual, predicted) &&
                  result["scores"][split] == (predicted == intended),
              "saved URI output differs from oracle for " + id + " " + arm +
                  " " + split + ": " + dump(result["outputs"][split]));
        if (arm == "reference" || arm == "reverse-unique-reverse")
          check(actual == saved_output,
                "saved URI output changed at the byte level");
      }

      if (arm != "reference" && arm != "reverse-unique-reverse")
        check(oracle(id, mixed, arm) != oracle(id, mixed),
              "mixed URI fixture did not distinguish " + arm);
      for (const auto &[name, data] : fresh) {
        const auto expected = oracle(id, bookmarks(data), arm);
        const auto expected_output =
            plain ? text_lines(expected) : dump(string_array(expected), false);
        const auto e = extra_case(work / "witness" / id / arm / name, exe,
                                  bundle, data, array({expected_output}));
        check(matches(id, e["actual"][0].asString(), expected),
              "fresh URI output differs for " + id + " " + arm + " " + name);
        entry["witness"][arm][name] =
            object({{"input_sha256", e["input_sha256"]},
                    {"actual", e["actual"][0]},
                    {"expected_uris", string_array(expected)}});
      }
      write(work / "progress.json", report);
    }
  }
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
