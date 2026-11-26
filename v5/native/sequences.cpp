#include "pilot.hpp"
#include <QByteArray>
#include <algorithm>

namespace v5 {
J sequence_requirements(const J &cases, const std::string &key) {
  J files = array();
  for (const auto &c : cases) {
    std::string raw;
    for (const auto &s : c["records"])
      raw += s.asString() + "\n";
    check(c[key].isArray(), "missing sequence oracle column");
    files.append(
        object({{"input", QByteArray::fromStdString(raw).toHex().toStdString()},
                {"json", c[key]}}));
  }
  files.append(object({{"failure", "missing-input"}}));
  files.append(object({{"failure", "destination-directory"}}));
  return object({{"input_type", "text/plain"},
                 {"output_type", "application/json"},
                 {"parameters", object()},
                 {"cases", files}});
}
namespace {
J filter_controls(const J &cases, const J &observed) {
  check(observed.size() == cases.size(), "missing filter observations");
  J rejected = object();
  for (std::string rule : {"drop_ascii_blank", "drop_empty_c_string"}) {
    J witnesses = array();
    for (Json::ArrayIndex i = 0; i < cases.size(); ++i) {
      J alternative = array();
      for (const auto &record : cases[i]["records"]) {
        auto bytes = record.asString();
        bool keep = rule == "drop_ascii_blank"
                        ? bytes.find_first_not_of(" \t\r\n\f\v") != bytes.npos
                        : !bytes.empty() && bytes.front() != '\0';
        if (keep)
          alternative.append(record);
      }
      if (alternative != observed[i])
        witnesses.append(
            object({{"case_index", i}, {"alternative_output", alternative}}));
    }
    check(!witnesses.empty(), "missing filter witness for " + rule);
    rejected[rule] = witnesses;
  }
  return rejected;
}
J differences(const J &a, const J &b) {
  check(a.size() == 121 && b.size() == 121, "incomplete finite comparison");
  J first;
  unsigned n = 0;
  for (unsigned i = 0; i < a.size(); ++i)
    if (a[i] != b[i]) {
      if (!n)
        first = i;
      ++n;
    }
  check(n > 0, "sequence substitutions indistinguishable");
  return object({{"different_outputs", n}, {"first_case_index", first}});
}
} // namespace
void qualify_sequences(const Path &extracted, const Path &out, J &report) {
  const auto fixture = v5_root() / "fixtures/sequence-probes.json",
             client = v5_root() / "fixtures/sequence-client.cpp";
  const auto cases = load(fixture),
             source = sources()["retained-text-measures"];
  auto prior_file = Path(source["prior_profile"]["file"].asString());
  check(digest(prior_file) == source["prior_profile"]["sha256"].asString(),
        "incorrect prior comparison profile");
  auto profile = load(prior_file),
       origin = load(extracted / "measure-source.json");
  auto exe = probe_program(out / "sequence-client");
  Strings build = {"g++-15", "-std=c++17",
                   "-O1",    "-fPIC",
                   "-I",     extracted / "original/headers",
                   "-I",     extracted / "original/providers",
                   client,   "-o",
                   exe};
  append(build, command_args(
                    object({{"command", run({"pkg-config", "--cflags", "--libs",
                                             "Qt6Core", "jsoncpp"})}})));
  run(build);
  for (const auto &key : sequence_keys) {
    auto binding = origin["operations"][key]["binding"];
    auto pin = compact_hash(binding);
    J lineage;
    for (const auto &entry : profile["candidates"])
      if (entry["id"] == key && entry["binding"] == pin) {
        check(lineage.isNull(), "ambiguous prior consumer comparison");
        lineage = entry;
      }
    const auto &archive = lineage["observations"];
    check(archive.isArray() && archive.size() == 6, "missing prior comparison");
    J inputs = array();
    for (const auto &c : cases)
      inputs.append(c["records"]);
    for (const auto &p : archive) {
      check(p["input_hash"] == compact_hash(p["input"]) &&
                p["output_hash"] == compact_hash(p["output"]),
            "stale prior observation");
      inputs.append(p["input"]);
    }
    auto args = sandbox(exe);
    args.push_back(key);
    RunOptions opts{.input = dump(inputs), .seconds = 8, .probe_limits = true};
    auto observed = parse(run(args, opts));
    check(observed == parse(run(args, opts)) &&
              observed["fixed"].size() == inputs.size() &&
              observed["bounded"].size() == 121,
          "sequence repeat or finite check failed");
    J fixed = array();
    for (unsigned i = 0; i < cases.size(); ++i) {
      auto value = observed["fixed"][i];
      check(value == cases[i][key], "fixed sequence case failed: " + key);
      fixed.append(value);
    }
    for (unsigned i = 0; i < archive.size(); ++i)
      check(observed["fixed"][cases.size() + i] == archive[i]["output"],
            "prior consumer observation differs");
    report[key] = object(
        {{"outcome",
          "Fixed and finite probes matched the independent reference twice."},
         {"binding_digest", pin},
         {"client_sha256", digest(client)},
         {"oracle_sha256", digest(fixture)},
         {"fixed", fixed},
         {"bounded_outputs", observed["bounded"]},
         {"domain", "Depth-first, prefix before children; maximum length four "
                    "over the ordered alphabet empty string, a, z."},
         {"bounded_applications_per_probe", 2},
         {"processes", 2},
         {"prior_profile", object({{"profile_sha256", digest(prior_file)},
                                   {"generation", profile["package"]},
                                   {"context", profile["context_hash"]},
                                   {"binary_sha256", lineage["binary"]},
                                   {"matched_probes", archive.size()}})}});
    if (key == "nonempty_lines")
      report[key]["filter_controls"] = filter_controls(cases, fixed);
  }
  for (const auto &key : sequence_keys)
    for (const auto &rival : sequence_keys)
      if (key != rival)
        report[key]["substitutions"][rival] = differences(
            report[key]["bounded_outputs"], report[rival]["bounded_outputs"]);
}
void check_sequence_evidence(const Path &pack, const J &c, const J &binding) {
  auto e = pack / "evidence";
  auto key = c["capability_id"].asString();
  const auto source = load(e / "measure-source.json"),
             reports = load(e / "standalone.json"), r = reports[key],
             cases = load(e / "sequence-probes.json");
  J source_record;
  for (const auto &s : load(pack / "sources.json").get("sources", array()))
    if (s["id"] == c["source"]["id"])
      source_record = s;
  check(r["prior_profile"]["profile_sha256"].isString() &&
            r["prior_profile"]["profile_sha256"] ==
                source_record["prior_profile"]["sha256"] &&
            source["operations"][key]["binding"] == binding &&
            source["source_id"] == c["source"]["id"] &&
            source["archive_sha256"] == c["source"]["archive_sha256"] &&
            source["revision"] == c["source"]["revision"] &&
            source["unchanged_members"][c["source"]["header"].asString()][1] ==
                c["source"]["implementation_sha256"] &&
            r["binding_digest"] == compact_hash(binding) &&
            r["client_sha256"] == digest(e / "sequence-client.cpp") &&
            r["oracle_sha256"] == digest(e / "sequence-probes.json") &&
            r["oracle_sha256"] == c["fingerprint"]["suite_sha256"] &&
            r["processes"] == 2 && r["fixed"].size() == cases.size() &&
            r["prior_profile"]["matched_probes"] == 6,
        "wrong retained sequence qualification context");
  for (unsigned i = 0; i < cases.size(); ++i)
    check(r["fixed"][i] == cases[i][key], "stale fixed sequence result");
  // Each immutable package pins the oracle columns for its own comparison
  // cohort.
  auto cohort = cases[0].getMemberNames();
  std::erase(cohort, "records");
  check(std::erase(cohort, key) == 1 &&
            r["substitutions"].getMemberNames() == cohort,
        "wrong sequence substitution cohort");
  for (const auto &rival : cohort)
    check(dump(r["substitutions"][rival]) ==
              dump(differences(r["bounded_outputs"],
                               reports[rival]["bounded_outputs"])),
          "sequence substitution witness mismatch");
  if (key == "nonempty_lines")
    check(dump(r["filter_controls"]) ==
              dump(filter_controls(cases, r["fixed"])),
          "sequence filter evidence mismatch");
  check(c["claims"]["mathematical_properties"]["status"] == "observed" &&
            c["claims"]["formal_predicates"]["status"] == "unknown",
        "unsupported finite verification strength");
}
} // namespace v5
