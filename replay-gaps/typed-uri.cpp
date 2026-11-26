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
const std::string tsv = "text/tab-separated-values";
const Strings splits{"ordinary", "boundary", "empty", "transfer", "sparse"};
const Path response = "run/calls/interpret/response.json";
const std::map<std::string, std::string> controls{
    {"H073", "count-every-uri"},
    {"H075", "literal-prefix"},
    {"H077", "first-occurrence-order"},
    {"H079", "count-distinct-uris"}};

std::string lower(std::string value) {
  for (auto &c : value)
    if (c >= 'A' && c <= 'Z')
      c += 'a' - 'A';
  return value;
}

std::string scheme(const std::string &uri) {
  const auto colon = uri.find(':');
  check(colon != std::string::npos && colon != 0, "expected absolute URI");
  return lower(uri.substr(0, colon));
}

Strings records(const std::string &input) {
  std::istringstream lines(input);
  Strings result;
  for (std::string line; std::getline(lines, line);) {
    if (line.ends_with('\r'))
      line.pop_back();
    if (!line.empty() && !line.starts_with('#'))
      result.push_back(line);
  }
  return result;
}

std::string tally(const std::string &key, size_t count) {
  return key + '\t' + std::to_string(count) + '\n';
}

J oracle(const std::string &id, const std::string &input, bool control) {
  const auto uris = records(input);
  std::string output;
  if (id == "H073") {
    std::map<std::string, Strings> groups;
    for (const auto &uri : uris) {
      const auto protocol = scheme(uri);
      if (protocol != "http" && protocol != "https")
        continue;
      const auto start = protocol.size() + 3;
      const auto end = uri.find_first_of("/?#", start);
      check(uri.substr(protocol.size(), 3) == "://" && start < uri.size(),
            "expected plain HTTP(S) authority");
      groups[lower(uri.substr(start, end - start))].push_back(uri);
    }
    std::vector<std::pair<std::string, size_t>> counts;
    for (const auto &[host, members] : groups)
      counts.emplace_back(
          host, control ? members.size()
                        : std::set(members.begin(), members.end()).size());
    std::stable_sort(
        counts.begin(), counts.end(),
        [](const auto &a, const auto &b) { return a.second > b.second; });
    for (const auto &[host, count] : counts)
      output += tally(host, count);
  } else if (id == "H075") {
    std::string https;
    for (const auto &uri : uris) {
      const auto token = control ? uri.substr(0, uri.find(':')) : scheme(uri);
      if (token == "file")
        output += uri + '\n';
      if (token == "https")
        https += uri + '\n';
    }
    return array({output, https});
  } else if (id == "H077") {
    std::map<std::string, size_t> total, seen;
    for (const auto &uri : uris)
      ++total[uri];
    for (const auto &uri : uris)
      if (++seen[uri] == (control ? 1u : 2u) && total.at(uri) > 1)
        output += tally(uri, total.at(uri));
  } else {
    check(id == "H079", "unknown URI request");
    std::map<std::string, size_t> counts;
    std::set<std::string> seen;
    Strings encounter;
    for (const auto &uri : uris) {
      if (control && !seen.insert(uri).second)
        continue;
      const auto token = scheme(uri);
      if (++counts[token] == 1)
        encounter.push_back(token);
    }
    for (const auto &token : encounter)
      output += tally(token, counts.at(token));
  }
  return array({output});
}

J control_spec(const std::string &id, J spec, const J &saved) {
  auto &ops = spec["roles"][0]["requirement"]["operations"];
  if (id == "H073") {
    ops[3]["parameters"]["measure"] = "count";
  } else if (id == "H075") {
    for (unsigned slot = 0; slot < spec["roles"].size(); ++slot)
      spec["roles"][slot]["requirement"]["operations"] =
          saved["roles"][slot]["requirement"]["operations"];
  } else if (id == "H077") {
    auto duplicate = ops[3];
    duplicate["parameters"]["value"] = "1";
    auto first = duplicate;
    duplicate["parameters"]["column"] = 2;
    duplicate["parameters"]["comparison"] = "gt";
    auto report = ops[5];
    report["parameters"]["fields"] = "0,2";
    ops = array({ops[0], ops[1], ops[2], first, duplicate, report});
  } else {
    check(id == "H079", "unknown control");
    ops[0]["parameters"]["fields"] = "scheme,original";
    ops[2]["parameters"]["measure"] = "distinct";
    ops[2]["parameters"]["value_column"] = 1;
  }
  return spec;
}

J tool_pins() {
  auto result = tools();
  result["unllm_logic_worker"] =
      hash(main_repo / "build/native/unllm_logic_worker");
  return result;
}
} // namespace

int main() try {
  const auto previous = runs / "v5-typed-reconciliation-XXVKXZxL/report.json";
  const auto previous_pin = hash(previous);
  check(previous_pin ==
            "3f6359bb7836418e0f3d6356e12275cfd600d57078869db528741cadb6cb2f8e",
        "changed typed-conversion reconciliation");
  const auto ledger = load(previous);
  const auto parent = runs / "v5-queue-XXH5XMi9/stage/package";
  const auto stage = runs / "v5-tsv-writer-XXAf0x0y/stage";
  const auto base = parent / "bundle", staged = stage / "bundle";
  const auto active = fs::canonical(volume / "unllm/boilerplate/active");
  check(hash(parent / "manifest.json") ==
                ledger["parent_manifest_sha256"].asString() &&
            hash(stage / "manifest.json") ==
                ledger["writer_manifest_sha256"].asString() &&
            active.string() == ledger["active_path"].asString() &&
            hash(active / "SHA256SUMS") == ledger["active_sha256"].asString(),
        "changed catalog context");
  verify_bundle(
      base, "436db92629463daee559de2cbbc1c81519e12306d5bf01cc6d2883f3140e779c");
  verify_bundle(
      staged,
      "b2f8a40d44b1589779fc82fcfa23995df5cb2b2432167e9383f6f22e9c828d54");
  const auto writer =
      exact_row(staged, "write_tsv_lines", "strings", "out:" + tsv);
  check(writer == ledger["writer"] &&
            remove_binding(read_tree(staged), writer).at("SHA256SUMS") ==
                bytes(base / "SHA256SUMS"),
        "parent differs outside exact writer binding");

  const std::string a = "https://Z.test/a%2f#one", b = "FILE:///tmp/a%23",
                    c = "https://z.test/a%2F#one", d = "hTTps://B.test/item";
  const std::string mixed =
      "# https://ignored.test/a\r\n" + a + "\n" + b +
      "\r\n\r\nhttp://a.test/\n" + b + "\n" + c + "\r\n" + d + "\n" + a +
      "\r\n" + d +
      "\nhttp://Z.test/p\r\nmailto:a@example.org\nfile:///tmp/a%23";
  const std::map<std::string, std::string> witnesses{
      {"mixed", mixed}, {"comments-only", "# first\r\n\n# last"}};
  const std::map<std::string, J> mixed_expected{
      {"H073", array({"z.test\t3\na.test\t1\nb.test\t1\n"})},
      {"H075", array({b + "\n" + b + "\nfile:///tmp/a%23\n",
                      a + "\n" + c + "\n" + d + "\n" + a + "\n" + d + "\n"})},
      {"H077", array({tally(b, 2) + tally(a, 2) + tally(d, 2)})},
      {"H079", array({"https\t5\nfile\t3\nhttp\t2\nmailto\t1\n"})}};

  const auto work = fresh_run("v5-typed-uri-replay");
  std::cout << work.string() << std::endl;
  auto report =
      object({{"reconciliation_sha256", previous_pin},
              {"active_path", active.string()},
              {"active_sha256", ledger["active_sha256"]},
              {"parent_manifest_sha256", ledger["parent_manifest_sha256"]},
              {"writer_manifest_sha256", ledger["writer_manifest_sha256"]},
              {"writer", writer},
              {"tools", tool_pins()}});
  const auto archive = runs / "english-audit-bcsgf8pi/replay-staged";
  const auto transfer = runs / "shared-review-5ui86km8/transfer";
  std::vector<Path> programs;
  unsigned verified_outputs = 0;
  for (const auto &[id, control] : controls) {
    const auto &entry = ledger["requests"][id];
    const auto saved_file = archive / id / response;
    const auto saved = load(saved_file);
    check(entry["execution_status"] == "pending_scoped_replay" &&
              hash(saved_file) == entry["reply_sha256"].asString() &&
              hash(archive / id / "request.txt") ==
                  entry["request_sha256"].asString() &&
              saved["input_type"] == uri_list &&
              entry["supplied_input_type"] == uri_list &&
              saved["roles"].size() == (id == "H075" ? 2u : 1u),
          "changed archived URI request " + id);
    auto &evidence = report["requests"][id];
    evidence["reply_sha256"] = entry["reply_sha256"];
    evidence["request_sha256"] = entry["request_sha256"];
    auto spec = saved;
    for (auto &role : spec["roles"]) {
      const auto &record = entry["roles"][role["id"].asString()];
      check(role["output_type"] == tsv && record["output_type"] == tsv &&
                role["requirement"]["operations"] ==
                    record["archived_operations"] &&
                role["requirement"]["uncovered"] ==
                    record["archived_uncovered"],
            "changed archived URI role " + id);
      role["requirement"]["operations"] = record["supplied_operations"];
      role["requirement"]["uncovered"] = array();
    }
    auto &inputs = evidence["fixture_input_sha256"];
    for (const auto &split : splits)
      inputs[split] =
          hash(transfer / id / "fixtures" / split / "input/saved.uri");

    for (const auto &arm : Strings{"without-writer", "supplied", control}) {
      const bool missing = arm == "without-writer", altered = arm == control;
      const auto &bundle = missing ? base : staged;
      const auto supplied = work / "specs" / id / (arm + ".json");
      write(supplied, altered ? control_spec(id, spec, saved) : spec);
      const auto data =
          trial(work, arm, id, bundle, supplied, "saved.uri", inputs);
      const auto project = work / arm / id / "run/project";
      const auto exe = project / "converter";
      auto &result = evidence["arms"][arm];
      result = data;
      if (!data["program_sha256"].isNull()) {
        programs.push_back(exe);
        result["source_sha256"] = hash(project / "converter.cpp");
        result["packages_sha256"] = hash(project / "packages.txt");
      }
      write(work / "progress.json", report);
      check(data["roles"].size() == spec["roles"].size(),
            "unexpected replay role count for " + id + " " + arm);
      for (unsigned slot = 0; slot < spec["roles"].size(); ++slot) {
        const auto &role = data["roles"][std::to_string(slot)];
        const auto &earlier =
            entry["roles"][spec["roles"][slot]["id"].asString()];
        if (!altered || id == "H075") {
          const auto &route =
              earlier[missing   ? "supplied_without_writer"
                      : altered ? "writer_only"
                                : "supplied_with_writer"]["routes"][0];
          check(role["steps"] == route["steps"] &&
                    ports(role) == route["ports"],
                "URI replay provider chain differs for " + id + " " + arm);
        }
        if (missing)
          check(role["status"] == "partial" &&
                    ports(role) == array({array({"strings", "out:" + tsv})}),
                "writer removal changed other URI ports");
        else
          check(complete(role) &&
                    role["steps"][role["steps"].size() - 1]["provider"] ==
                        "write_tsv_lines",
                "URI role did not complete");
      }
      bool all_passed = !missing;
      for (const auto &split : splits) {
        const auto fixture = transfer / id / "fixtures" / split;
        const auto input = bytes(fixture / "input/saved.uri");
        const auto expected = oracle(id, input, false);
        const auto prediction = oracle(id, input, altered);
        for (unsigned slot = 0; slot < expected.size(); ++slot) {
          const auto file = fixture / "output" /
                            ("result-" + std::to_string(slot + 1) + ".txt");
          check(bytes(file) == expected[slot].asString(),
                "saved output differs from independent oracle for " + id + " " +
                    split);
          evidence["fixture_output_sha256"][split][std::to_string(slot)] =
              hash(file);
        }
        const auto passed = !missing && expected == prediction;
        all_passed &= passed;
        check(data["scores"][split] == passed &&
                  (missing || data["outputs"][split] == prediction),
              "unexpected fixture result for " + id + " " + arm + " " + split);
        if (!missing && !altered)
          verified_outputs += expected.size();
      }
      check(data["verified"] == all_passed,
            "unexpected replay verdict for " + id + " " + arm);
      if (!missing) {
        check(oracle(id, mixed, false) == mixed_expected.at(id) &&
                  oracle(id, mixed, true) != mixed_expected.at(id),
              "fresh URI input does not discriminate " + id);
        for (const auto &[name, input] : witnesses) {
          const auto run = extra_case(work / "witness" / id / arm / name, exe,
                                      bundle, input, oracle(id, input, false));
          result["witness"][name] = run;
          write(work / "progress.json", report);
          check(run["actual"] == oracle(id, input, altered) &&
                    run["passed"] == (!altered || name != "mixed"),
                "unexpected fresh result for " + id + " " + arm + " " + name);
        }
      }
    }
  }
  check(verified_outputs == 25 && tool_pins() == report["tools"] &&
            fs::canonical(volume / "unllm/boilerplate/active") == active &&
            hash(active / "SHA256SUMS") == ledger["active_sha256"].asString(),
        "changed verified coverage or replay context");
  report["verified_saved_fixtures"] = controls.size() * splits.size();
  report["verified_saved_outputs"] = verified_outputs;
  report["remaining"] = array({"H016", "H065"});
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
