#include "bundle.hpp"
#include "pilot.hpp"
#include <QByteArray>

namespace v5 {
namespace {
const std::string id = "prefixed_lines";

std::string lines(const J &records) {
  std::string out;
  for (const auto &r : records) {
    check(r.isString() && r.asString().find('\n') == std::string::npos,
          "invalid record for line-oriented fixture");
    out += r.asString() + "\n";
  }
  return out;
}
std::string hex(const std::string &s) {
  return QByteArray::fromStdString(s).toHex().toStdString();
}
} // namespace

J prefix_requirement(const J &fixture) {
  J cases = array({object({{"input", hex(lines(fixture["records"]))},
                           {"output", hex(lines(fixture["expected"]))}}),
                   object({{"input", ""}, {"output", ""}}),
                   object({{"failure", "missing-input"}}),
                   object({{"failure", "destination-directory"}})});
  return object({{"input_type", "text/plain"},
                 {"output_type", "text/plain"},
                 {"parameters", object({{"prefix", fixture["prefix"]}})},
                 {"cases", cases}});
}

J qualify_prefix(const Path &extracted, const Path &out) {
  auto fixture = v5_root() / "fixtures/prefix-probes.json",
       client = v5_root() / "fixtures/prefix-client.cpp",
       exe = probe_program(out / "prefix-client");
  Strings cmd = {"g++-15", "-std=c++17",
                 "-O1",    "-fPIC",
                 "-I",     extracted / "original/headers",
                 "-I",     extracted / "original/providers",
                 client,   "-o",
                 exe};
  append(cmd, flags({"Qt6Core", "jsoncpp"}));
  run(cmd);
  RunOptions opts{.input = bytes(fixture), .seconds = 8, .probe_limits = true};
  auto observed = parse(run(sandbox(exe), opts));
  check(observed == parse(run(sandbox(exe), opts)),
        "prefix observations differ across processes");
  observed["outcome"] = "Exact fixed outputs, bounded independent reference, "
                        "idempotence and substring controls checked twice.";
  observed["binding_sha256"] = compact_hash(
      load(extracted / "measure-source.json")["operations"][id]["binding"]);
  observed["client_sha256"] = digest(client);
  observed["fixture_sha256"] = digest(fixture);
  observed["binary_sha256"] = digest(exe);
  return observed;
}

J prefix_argument_controls(const Path &bundle, const Path &dest) {
  J controls = object();
  for (const std::string name : {"missing", "wrong-type"}) {
    auto params = name == "missing" ? object() : object({{"prefix", 17}});
    auto spec = spec_for(
        "text/plain", "text/plain",
        object(
            {{"outcome", id}, {"parameters", params}, {"uncovered", array()}}));
    const auto emission = plan(bundle, spec, dest / name);
    check(emission["providers"].empty() &&
              emission["interpretation_errors"]["r"].asString().find(
                  "prefixed_lines.prefix") != std::string::npos,
          "invalid prefix parameter unexpectedly accepted: " + dump(emission));
    controls[name] =
        object({{"parameters", params},
                {"clarifications", emission["clarifications"]},
                {"interpretation_errors", emission["interpretation_errors"]},
                {"gaps", emission["gaps"]}});
  }
  return controls;
}

void check_prefix_evidence(const Path &package, const J &contract,
                           const J &binding) {
  auto e = package / "evidence";
  const auto fixtures = load(e / "prefix-probes.json"),
             standalone = load(e / "standalone.json")[id],
             trial = load(e / "integration.json")[id],
             source = load(e / "measure-source.json");
  check(binding["args"] == array({"prefix"}) &&
            contract["interface"]["parameters"] ==
                object({{"prefix", "text"}}) &&
            contract["source"]["archive_sha256"] == source["archive_sha256"] &&
            source["operations"][id]["binding"] == binding &&
            compact_hash(binding) == standalone["binding_sha256"].asString() &&
            digest(e / "prefix-client.cpp") ==
                standalone["client_sha256"].asString() &&
            digest(e / "prefix-probes.json") ==
                standalone["fixture_sha256"].asString() &&
            contract["fingerprint"]["suite_sha256"] ==
                standalone["fixture_sha256"] &&
            contract["fingerprint"]["observations_sha256"] ==
                prefix_fingerprint(trial) &&
            standalone["bounded_trials"] == 510 &&
            standalone["substring_differences"].asUInt() == 45 &&
            contract["claims"]["formal_predicates"]["status"] == "unknown" &&
            contract["claims"]["mathematical_properties"]["status"] ==
                "observed" &&
            standalone["fixed"].size() == fixtures.size() &&
            trial["parameter_trials"].size() + 1 == fixtures.size(),
        "prefix implementation, parameter domain or evidence mismatch");
  for (const auto &f : fixtures) {
    auto name = f["name"].asString();
    const auto &arm =
                   name == "comment" ? trial : trial["parameter_trials"][name],
               &direct = standalone["fixed"][name];
    auto expected = array({sha(lines(f["expected"])), sha(""), "missing-input",
                           "destination-directory"});
    check(arm["prefix"] == f["prefix"] && direct["prefix"] == f["prefix"] &&
              direct["result"] == f["expected"] &&
              arm["baseline"]["result"] == "passed" &&
              arm["staged"]["result"] == "passed" &&
              arm["staged"]["chain"] ==
                  array({"read_lines", id, "write_lines"}) &&
              arm["staged"]["chain"] == arm["baseline"]["chain"] &&
              arm["baseline"]["probes"] == expected &&
              arm["staged"]["probes"] == expected &&
              arm["baseline"]["binary_sha256"] ==
                  arm["staged"]["binary_sha256"] &&
              arm["removed"]["result"] == "no complete program" &&
              !arm["removed"]["gaps"].empty(),
          "incorrect prefix trial: " + name);
  }
  check(standalone["fixed"]["comment"]["substring"] !=
            standalone["fixed"]["comment"]["result"],
        "missing substring control");
  for (const std::string name : {"missing", "wrong-type"}) {
    const auto &control = trial["argument_controls"][name];
    check(control["parameters"] ==
                  (name == "missing" ? object() : object({{"prefix", 17}})) &&
              control["interpretation_errors"]["r"].asString().find(
                  "prefixed_lines.prefix") != std::string::npos,
          "missing parameter diagnosis: " + name);
  }
}
std::string prefix_fingerprint(const J &trial) {
  J observed = object();
  auto record = [&](const std::string &name, const J &arm) {
    observed[name] = object(
        {{"prefix", arm["prefix"]}, {"probes", arm["staged"]["probes"]}});
  };
  record("comment", trial);
  for (const auto &name : trial["parameter_trials"].getMemberNames())
    record(name, trial["parameter_trials"][name]);
  return sha(dump(observed));
}
} // namespace v5
