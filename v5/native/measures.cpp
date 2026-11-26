#include "database.hpp"
#include "pilot.hpp"
#include <algorithm>
#include <sstream>

namespace v5 {
namespace {
const std::string source = "retained-text-measures";
const Strings measures = {"byte_lengths", "item_count"};
} // namespace
void extract_measures(const Path &out) {
  auto s = sources()[source],
       receipt = retained_source(out, source, "text-measures-LICENSE");
  auto doc = run({"bsdtar", "-xOf", s["archive"].asString(),
                  s["documentation"].asString()});
  check(sha(doc) == s["documentation_sha256"].asString(),
        "operation documentation digest mismatch");
  auto bindings = load(verified_base() / "providers/contracts.json");
  J selectors = object(), descriptors = object();
  for (const auto &a : load(v5_root() / "fixtures/annotations.json"))
    if (a["source_id"] == source)
      selectors[a["id"].asString()] = a["binding"];
  std::istringstream lines(doc);
  for (std::string line; std::getline(lines, line);)
    for (const auto &key : selectors.getMemberNames()) {
      if (!line.starts_with(key + "|"))
        continue;
      check(!descriptors.isMember(key),
            "duplicate retained operation definition");
      const auto &sel = selectors[key];
      auto row =
          select_binding(bindings["providers"], key, sel["brick"].asString(),
                         sel["from"].asString(), sel["to"].asString());
      Strings parameters;
      const auto &types = bindings["outcomes"][key]["parameters"];
      for (const auto &arg : row["args"]) {
        auto name = arg.asString();
        check(types[name].isString(), "missing parameter type for " + name);
        parameters.push_back(name + ":" + types[name].asString());
      }
      check(std::count(s["symbols"].begin(), s["symbols"].end(), row["call"]) ==
                    1 &&
                line == key + "|" + row["from"].asString() + "|" +
                            row["to"].asString() + "|" +
                            row["call"].asString() + "|" +
                            join(parameters, ",") + "|" +
                            bindings["outcomes"][key]["meaning"].asString(),
            "upstream documentation differs from planner declaration");
      descriptors[key] =
          object({{"documentation_line", line}, {"binding", row}});
    }
  check(descriptors.size() == selectors.size() &&
            descriptors.size() == s["symbols"].size(),
        "incomplete upstream documentation");
  receipt["operations"] = descriptors;
  receipt["documentation_sha256"] = sha(doc);
  write(out / "measure-source.json", receipt);
}
void qualify_measures(const Path &extracted, const Path &out) {
  auto fixture = v5_root() / "fixtures/measure-probes.json",
       client = v5_root() / "fixtures/measure-client.cpp";
  auto cases = load(fixture), inputs = array();
  for (const auto &c : cases)
    inputs.append(c["records"]);
  auto exe = probe_program(out / "client");
  Strings build = {"g++-15", "-std=c++17",
                   "-O1",    "-fPIC",
                   "-I",     extracted / "original/headers",
                   "-I",     extracted / "original/providers",
                   client,   "-o",
                   exe};
  auto flags = run({"pkg-config", "--cflags", "--libs", "Qt6Core", "jsoncpp"});
  append(build, command_args(object({{"command", flags}})));
  run(build);
  J report = object(), observations = object();
  for (const auto &key : measures) {
    auto argv = sandbox(exe);
    argv.push_back(key);
    RunOptions opts{.input = dump(inputs), .seconds = 8, .probe_limits = true};
    auto first = parse(run(argv, opts)), repeat = parse(run(argv, opts));
    check(first == repeat && first.size() == cases.size(),
          "measurement repeat failed for " + key);
    for (unsigned i = 0; i < first.size(); ++i)
      check(first[i] == cases[i][key],
            "measurement differs from independent oracle: " +
                cases[i]["name"].asString());
    observations[key] = first;
  }
  for (const auto &key : measures) {
    const auto &rival = measures[key == measures.front() ? 1 : 0];
    J rejected = array(), coincident = array();
    for (unsigned i = 0; i < cases.size(); ++i) {
      (observations[rival][i] == cases[i][key] ? coincident : rejected)
          .append(cases[i]["name"]);
    }
    check(!coincident.empty() && !rejected.empty(),
          "frozen witnesses fail to distinguish signatures");
    report[key] =
        object({{"outcome", "All normalized observations matched twice; "
                            "substituting the other callable rejected"},
                {"client_sha256", digest(client)},
                {"oracle_sha256", digest(fixture)},
                {"observations", observations[key]},
                {"substitution", rival},
                {"rejected_cases", rejected},
                {"coincident_cases", coincident}});
  }
  qualify_sequences(extracted, out, report);
  report["prefixed_lines"] = qualify_prefix(extracted, out);
  write(out / "standalone.json", report);
}
J semantic_ports(const J &c, const J &binding, const J &annotation) {
  auto entries = array();
  for (const auto *dir : {"input", "output"}) {
    J port = annotation["port_semantics"][dir];
    auto id = c["capability_id"].asString();
    bool reviewed = !port.isNull();
    if (!reviewed)
      for (const auto *k : {"role", "unit", "cardinality"}) {
        port[k] = J();
        port[std::string(k) + "_status"] = "unknown";
      }
    port["provider"] = c["provider_id"];
    port["binding"] = binding["id"];
    port["direction"] = dir;
    port["contract_hash"] = sha(dump(c));
    port["detail"] = reviewed ? "Evidence covers the selected binding and the "
                                "fixed stored-string probes."
                              : "No reviewed role, unit or cardinality for "
                                "this port in this pilot.";
    port["evidence"] =
        reviewed ? array({id == "line_xxh32" ? "evidence/xxhash-source.json"
                                             : "evidence/measure-source.json",
                          "evidence/standalone.json#" + id,
                          "evidence/integration.json#" + id})
                 : array();
    entries.append(port);
  }
  return entries;
}
namespace {
void check_witnesses(const Path &package, const J &port, const J &c,
                     const J &binding, const J &vocab) {
  const auto key = c["capability_id"].asString();
  auto evidence = package / "evidence";
  auto origin = load(evidence / "measure-source.json");
  check(origin["operations"][key]["binding"] == binding &&
            origin["source_id"] == c["source"]["id"] &&
            origin["revision"] == c["source"]["revision"] &&
            origin["archive_sha256"] == c["source"]["archive_sha256"],
        "measurement evidence belongs to a different binding");
  for (const auto &file : origin["unchanged_members"].getMemberNames())
    check(origin["unchanged_members"][file][1] ==
              digest(project_file(package / "bundle", file)),
          "stale measurement source member");
  check(port["evidence"] == array({"evidence/measure-source.json",
                                   "evidence/standalone.json#" + key,
                                   "evidence/integration.json#" + key}),
        "semantic evidence is not scoped to this binding");
  auto behavior = load(evidence / "standalone.json")[key];
  check(behavior["client_sha256"] == digest(evidence / "measure-client.cpp") &&
            behavior["oracle_sha256"] ==
                digest(evidence / "measure-probes.json"),
        "stale measurement probe context");
  if (port["direction"] == "input") {
    check(port["role"] == "ordered_text_records" &&
              port["cardinality"] == "sequence" &&
              port["unit_status"] == "not-applicable",
          "input claim differs from measurement client");
    return;
  }
  auto cases = load(evidence / "measure-probes.json");
  J matches = array();
  // Compare both independent oracle columns against the same observed values.
  // A single example can agree with both and cannot decide the unit.
  for (const auto &model : vocab["measurement_oracles"]) {
    auto expected = array();
    for (const auto &example : cases)
      expected.append(example[model["column"].asString()]);
    if (expected == behavior["observations"])
      matches.append(model);
  }
  check(matches.size() == 1,
        "measurement observations are ambiguous or contradictory");
  for (const auto *field : {"role", "unit", "cardinality"})
    check(port[field] == matches[0][field],
          "semantic output claim contradicts independent witnesses");
}
} // namespace
void check_semantic_ports(const Path &package, const J &ports,
                          const J &contracts, const J &bindings) {
  auto vocabulary = load(package / "semantic-vocabulary.json");
  check(vocabulary["id"] == "unllm.semantic-ports/1",
        "unknown port vocabulary");
  std::set<std::pair<std::string, std::string>> consumed;
  std::map<std::string, J> by_hash;
  for (const auto &c : contracts)
    check(by_hash.emplace(sha(dump(c)), c).second,
          "duplicate canonical contract");
  for (const auto &p : ports) {
    auto hash = p["contract_hash"].asString(), dir = p["direction"].asString();
    check(by_hash.contains(hash) && (dir == "input" || dir == "output") &&
              consumed.emplace(hash, dir).second,
          "unknown or duplicate semantic port");
    const auto &c = by_hash.at(hash), &iface = c["interface"];
    auto row = select_binding(bindings, c["capability_id"].asString(),
                              c["provider_id"].asString(),
                              iface["structural_input"].asString(),
                              iface["structural_output"].asString());
    check(p["provider"] == c["provider_id"] && p["binding"] == row["id"] &&
              !p["detail"].asString().empty(),
          "semantic port binding mismatch");
    bool supported = false;
    for (const auto &[field, set] :
         std::map<std::string, std::string>{{"role", "roles"},
                                            {"unit", "units"},
                                            {"cardinality", "cardinalities"}}) {
      const auto &status = p[field + "_status"];
      check(p.isMember(field) &&
                (status == "observed" || status == "unknown" ||
                 (field == "unit" && status == "not-applicable")),
            "invalid semantic field status");
      if (status == "observed")
        check(p[field].isString() &&
                  vocabulary[set].isMember(p[field].asString()),
              "unknown qualified semantic identifier");
      else
        check(p[field].isNull(),
              "nonpositive semantic status requires null identifier");
      supported |= status != "unknown";
    }
    check(p["evidence"].isArray() && (!supported || !p["evidence"].empty()),
          "missing semantic port evidence");
    if (supported && c["capability_id"] == "line_xxh32")
      check_xxhash_witness(package, p, c, row);
    else if (supported && vocabulary.isMember("measurement_oracles"))
      check_witnesses(package, p, c, row, vocabulary);
    for (const auto &reference : p["evidence"]) {
      auto ref = reference.asString();
      auto sep = ref.find('#');
      check(ref.starts_with("evidence/"), "semantic evidence outside package");
      auto path = project_file(package, ref.substr(0, sep));
      if (sep != ref.npos)
        check(load(path).isMember(ref.substr(sep + 1)),
              "broken semantic evidence fragment");
    }
  }
  check(consumed.size() == 2 * contracts.size(),
        "incomplete semantic port inventory");
}
} // namespace v5

namespace v5 {
namespace {
Strings projection_row(const J &p) {
  return {p["provider"].asString(),           p["binding"].asString(),
          p["direction"].asString(),          p["role"].asString(),
          p["role_status"].asString(),        p["unit"].asString(),
          p["unit_status"].asString(),        p["cardinality"].asString(),
          p["cardinality_status"].asString(), p["contract_hash"].asString()};
}
} // namespace
std::map<std::string, Strings>
index_semantic_ports(const Path &package, Database &db, const J &ports) {
  db.script(
      R"sql(CREATE TABLE semantic_port(provider TEXT,binding TEXT,direction TEXT,role TEXT,role_status TEXT,unit TEXT,unit_status TEXT,cardinality TEXT,cardinality_status TEXT,contract_hash TEXT,PRIMARY KEY(provider,binding,direction));
                  CREATE INDEX by_semantic_output ON semantic_port(direction,role,unit,cardinality);)sql");
  const auto vocabulary = load(package / "semantic-vocabulary.json");
  std::map<std::string, Strings> descriptions;
  for (const auto &p : ports) {
    db.insert("INSERT INTO semantic_port "
              "VALUES(?,?,?,NULLIF(?,''),?,NULLIF(?,''),?,NULLIF(?,''),?,?)",
              projection_row(p));
    for (const auto &[attr, group] :
         std::map<std::string, std::string>{{"role", "roles"},
                                            {"unit", "units"},
                                            {"cardinality", "cardinalities"}})
      if (p[attr + "_status"] == "observed") {
        auto value = p[attr].asString();
        auto &terms = descriptions[p["provider"].asString()];
        terms.push_back(value);
        terms.push_back(vocabulary[group][value].asString());
      }
  }
  return descriptions;
}
void verify_semantic_index(Database &db, const J &ports) {
  std::vector<Strings> expected;
  for (const auto &p : ports)
    expected.push_back(projection_row(p));
  std::sort(expected.begin(), expected.end());
  check(expected ==
            db.query("SELECT "
                     "provider,binding,direction,role,role_status,unit,unit_"
                     "status,cardinality,cardinality_status,contract_hash FROM "
                     "semantic_port ORDER BY provider,binding,direction"),
        "structured semantic ports differ from canonical evidence");
  check(db.query("SELECT provider FROM semantic_port WHERE "
                 "(role_status='observed')!=(role IS NOT NULL) OR "
                 "(unit_status='observed')!=(unit IS NOT NULL) OR "
                 "(cardinality_status='observed')!=(cardinality IS NOT NULL)")
            .empty(),
        "incorrect SQL null semantics");
}
} // namespace v5
