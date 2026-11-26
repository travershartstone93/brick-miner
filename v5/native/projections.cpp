#include "database.hpp"
#include "pilot.hpp"
#include <algorithm>
#include <cstdlib>
#include <regex>
#include <sstream>

namespace v5 {
namespace {
bool contains(const J &values, const J &v) {
  return std::find(values.begin(), values.end(), v) != values.end();
}
void validate_field(const J &v, const J &s, const std::string &path) {
  if (s.isMember("const"))
    check(v == s["const"], "unexpected constant at " + path);
  if (s.isMember("enum"))
    check(contains(s["enum"], v), "unknown vocabulary at " + path);
  if (s.isMember("type")) {
    auto type = s["type"].asString();
    check((type == "object" && v.isObject()) ||
              (type == "array" && v.isArray()) ||
              (type == "string" && v.isString()),
          "unexpected type at " + path);
  }
  if (v.isObject()) {
    for (const auto &key : s["required"])
      check(v.isMember(key.asString()),
            "missing " + path + "/" + key.asString());
    for (const auto &key : v.getMemberNames()) {
      if (s["properties"].isMember(key))
        validate_field(v[key], s["properties"][key], path + "/" + key);
      else if (s["additionalProperties"].isObject())
        validate_field(v[key], s["additionalProperties"], path + "/" + key);
      else
        check(!s.isMember("additionalProperties") ||
                  s["additionalProperties"].asBool(),
              "unexpected key " + path + "/" + key);
    }
  } else if (v.isArray()) {
    check(v.size() >= s.get("minItems", 0).asUInt(), "empty array at " + path);
    std::set<std::string> seen;
    for (const auto &item : v) {
      if (s.get("uniqueItems", false).asBool())
        check(seen.insert(dump(item, false)).second,
              "duplicate item at " + path);
      if (s.isMember("items"))
        validate_field(item, s["items"], path + "[]");
    }
  } else if (v.isString()) {
    auto text = v.asString();
    check(text.size() >= s.get("minLength", 0).asUInt(),
          "empty string at " + path);
    if (s.isMember("pattern"))
      check(std::regex_search(text, std::regex(s["pattern"].asString())),
            "invalid string at " + path);
  }
}
J contract(const J &a, const Path &work, const J &registry, const J &bindings) {
  auto key = a["id"].asString();
  auto src = registry[a["source_id"].asString()];
  J row;
  for (const auto &p : bindings["providers"])
    if (p["id"] == key) {
      check(row.isNull(), "duplicate binding");
      row = p;
    }
  auto test = "evidence/standalone.json#" + key,
       end = "evidence/integration.json#" + key;
  auto standalone = load(work / "standalone/standalone.json")[key];
  auto integration = load(work / "integration/integration.json")[key];
  check(!row.isNull() && !standalone["outcome"].asString().empty() &&
            integration["staged"]["result"] == "passed",
        "missing qualification for " + key);
  auto facets = a["facets"];
  facets["dependencies"] = src["dependencies"];
  facets["license"] =
      array({src["rights"]["upstream_files"], src["rights"]["scope"]});
  facets["qualification"] = array({"ordinary-emission-tested"});
  auto claims = a["claims"], signature = array();
  for (const auto &k : claims.getMemberNames())
    if (claims[k]["status"] == "observed")
      signature.append(k);
  auto header = "headers/" + row["header"].asString();
  return object(
      {{"schema", "unllm.brick-contract/1"},
       {"vocabulary", "unllm.brick-vocabulary/1"},
       {"capability_id", key},
       {"provider_id", row["brick"]},
       {"legacy_brick", row["brick"]},
       {"description", bindings["outcomes"][key]["meaning"]},
       {"primary_family", a["primary_family"]},
       {"facets", facets},
       {"interface",
        object({{"callable", row["call"]},
                {"structural_input", row["from"]},
                {"structural_output", row["to"]},
                {"semantic_input", a["semantic_input"]},
                {"semantic_output", a["semantic_output"]},
                {"parameters", bindings["outcomes"][key]["parameters"]}})},
       {"claims", claims},
       {"source", object({{"id", src["id"]},
                          {"revision", src["revision"]},
                          {"archive_sha256", src["sha256"]},
                          {"implementation_sha256",
                           digest(work / "integration/bundle" / header)},
                          {"header", header},
                          {"source_registry_sha256",
                           digest(v5_root() / "sources.json")}})},
       {"fingerprint",
        object(
            {{"suite_sha256", digest(v5_root() / "fixtures/requirements.json")},
             {"observations_sha256",
              sha(dump(integration["staged"]["probes"]))},
             {"scope", "fixed development probes on this Linux toolchain"}})},
       {"property_signature", signature},
       {"qualification",
        object({{"level", "ordinary-emission-tested"},
                {"independent", test},
                {"integration", end},
                {"scope", "Local staged fixed typed development task with "
                          "removal control; external review pending"}})}});
}
void project(const Path &package, const Path &scratch, const J &contracts) {
  Database db(package / "contracts.sqlite");
  db.script(R"sql(
        CREATE TABLE provider(capability TEXT, provider TEXT PRIMARY KEY, contract_hash TEXT, family TEXT, level TEXT);
        CREATE TABLE port(provider TEXT PRIMARY KEY, input TEXT, output TEXT, semantic_input TEXT, semantic_output TEXT);
        CREATE TABLE facet(provider TEXT, name TEXT, value TEXT, PRIMARY KEY(provider,name,value));
        CREATE TABLE property(provider TEXT, name TEXT, status TEXT, detail TEXT, PRIMARY KEY(provider,name));
        CREATE INDEX by_family ON provider(family,capability);
        CREATE INDEX by_property ON property(name,status);
        CREATE INDEX by_facet ON facet(name,value);
        BEGIN;
    )sql");
  auto pack = package / "packs/brickcontracts";
  fs::create_directories(pack);
  Database search(pack / "shard-000.sqlite");
  search.script(R"sql(
        CREATE TABLE meta(key TEXT PRIMARY KEY,value TEXT);
        CREATE TABLE docs(doc_id INTEGER PRIMARY KEY,title TEXT,url TEXT,author TEXT,license TEXT);
        CREATE TABLE passages(pid INTEGER PRIMARY KEY,role TEXT,text TEXT,doc_id INTEGER,section TEXT,ord INTEGER);
        BEGIN;
    )sql");
  for (const Strings &row :
       std::vector<Strings>{{"name", "brickcontracts"},
                            {"kind", "brick-contract"},
                            {"passages", std::to_string(contracts.size())},
                            {"version", "1"}})
    search.insert("INSERT INTO meta VALUES(?,?)", row);
  int pid = 0;
  for (const auto &c : contracts) {
    auto id = c["provider_id"].asString(), cap = c["capability_id"].asString();
    const auto &port = c["interface"];
    auto doc = std::to_string(++pid);
    db.insert("INSERT INTO provider VALUES(?,?,?,?,?)",
              {cap, id, sha(dump(c)), c["primary_family"].asString(),
               c["qualification"]["level"].asString()});
    db.insert("INSERT INTO port VALUES(?,?,?,?,?)",
              {id, port["structural_input"].asString(),
               port["structural_output"].asString(),
               port["semantic_input"].asString(),
               port["semantic_output"].asString()});
    for (const auto &k : c["facets"].getMemberNames())
      for (const auto &value : c["facets"][k])
        db.insert("INSERT INTO facet VALUES(?,?,?)", {id, k, value.asString()});
    for (const auto &k : c["claims"].getMemberNames()) {
      const auto &claim = c["claims"][k];
      db.insert(
          "INSERT INTO property VALUES(?,?,?,?)",
          {id, k, claim["status"].asString(), claim["detail"].asString()});
    }
    search.insert("INSERT INTO docs VALUES(?,?,?,?,?)",
                  {doc, cap, c["source"]["id"].asString(), "brick-miner",
                   c["facets"]["license"][0].asString()});
    Strings text = {c["description"].asString(),
                    port["semantic_input"].asString(),
                    port["semantic_output"].asString()};
    append(text, strings(c["facets"]["technical_hierarchy"]));
    search.insert("INSERT INTO passages VALUES(?,?,?,?,?,?)",
                  {doc, "contract", join(text, " "), doc, "behavior", "0"});
  }
  db.script("COMMIT");
  search.script("COMMIT");
  auto facts = package / "facts", deduced = package / "deduced";
  for (const auto &[name, query] : std::map<std::string, std::string>{
           {"Provider",
            "SELECT capability,provider,level FROM provider ORDER BY 1,2"},
           {"Port", "SELECT provider,input,output FROM port ORDER BY 1"},
           {"Property",
            "SELECT provider,name,status FROM property ORDER BY 1,2"}}) {
    std::string rows;
    for (const auto &row : db.query(query))
      rows += join(row, "\t") + "\n";
    save(facts / (name + ".facts"), rows);
  }
  fs::create_directories(deduced);
  run({main_repo / ".deps/bin/souffle", "-F", facts, "-D", deduced,
       package / "facts.dl"});
  J logical = object();
  for (const auto &[name, sql] : std::map<std::string, std::string>{
           {"TestedTransform",
            "SELECT capability,input,output FROM provider JOIN port "
            "USING(provider) WHERE level='ordinary-emission-tested'"},
           {"Observed", "SELECT capability,name FROM provider JOIN property "
                        "USING(provider) WHERE status='observed'"}}) {
    std::istringstream input(bytes(deduced / (name + ".csv")));
    Strings derived, expected;
    std::string line;
    while (std::getline(input, line))
      derived.push_back(line);
    for (const auto &row : db.query(sql))
      expected.push_back(join(row, "\t"));
    std::sort(derived.begin(), derived.end());
    std::sort(expected.begin(), expected.end());
    check(derived == expected, "Soufflé and SQLite disagree on " + name);
    logical[name] = string_array(expected);
  }
  auto rust = main_repo / "search/target/release",
       reader = scratch / "tantivy-query";
  run({"env", "-u", "HOME", rust / "unllm_index", pack, "--threads", "1",
       "--heap-mb", "32"});
  run({"c++", "-std=c++17", v5_root() / "fixtures/tantivy-query.cpp",
       rust / "libunllm_search.a", "-ldl", "-lpthread", "-lm", "-o", reader});
  J results = object();
  for (auto [q, want] : std::map<std::string, std::string>{
           {"digit", direct}, {"multiplicity", api}}) {
    auto id = std::to_string(std::stoll(run({reader, pack / "tantivy", q})));
    check(search.query("SELECT title FROM docs WHERE doc_id=?", {id}) ==
              std::vector<Strings>{{want}},
          "Rust Tantivy retrieval mismatch");
    results[q] = want;
  }
  write(package / "cross-check.json",
        object({{"logical", logical},
                {"tantivy", results},
                {"contract_set_sha256", digest(package / "contracts.json")},
                {"tantivy_binary", digest(rust / "unllm_index")},
                {"query_library", digest(rust / "libunllm_search.a")},
                {"queries", "two fixed development terms"}}));
}
} // namespace
void validate(const J &c, const J &schema) {
  validate_field(c, schema, "contract");
  for (const auto &k : c["claims"].getMemberNames()) {
    const auto &claim = c["claims"][k];
    if (claim["status"] == "established" || claim["status"] == "observed")
      check(!claim["evidence"].empty(), "unsupported assertion " + k);
  }
}
void assemble(const Path &work, const Path &dest) {
  auto package = dest / ".package", e = package / "evidence";
  fs::create_directories(e);
  for (auto [stage, f] : std::vector<std::pair<std::string, std::string>>{
           {"inventory", "inputs.json"},
           {"extract", "extraction.json"},
           {"extract", "sketch.json"},
           {"standalone", "standalone.json"},
           {"integration", "integration.json"}})
    fs::copy_file(work / stage / f, e / f);
  for (const auto *f :
       {"contract.schema.json", "vocabulary.json", "sources.json", "facts.dl"})
    fs::copy_file(v5_root() / f, package / f);
  fs::copy(work / "extract/notices", package / "notices",
           fs::copy_options::recursive);
  fs::copy_file(v5_root() / "fixtures/requirements.json",
                e / "requirements.json");
  fs::copy(work / "integration/bundle", package / "bundle",
           fs::copy_options::recursive);
  auto registry = sources(),
       bindings = load(package / "bundle/providers/contracts.json"),
       schema = load(package / "contract.schema.json");
  auto contracts = array();
  for (const auto &a : load(v5_root() / "fixtures/annotations.json")) {
    auto c = contract(a, work, registry, bindings);
    validate(c, schema);
    contracts.append(c);
  }
  write(package / "contracts.json", contracts);
  project(package, dest, contracts);
  auto guide = load(package / "vocabulary.json")["primary_families"],
       families = object();
  for (const auto &family : guide.getMemberNames()) {
    auto caps = array();
    for (const auto &c : contracts)
      if (c["primary_family"] == family)
        caps.append(c["capability_id"]);
    families[family] = object(
        {{"guidance", guide[family]},
         {"staged_capabilities", caps},
         {"tested_typed_programs", caps.size()},
         {"architecture_qualified", 0},
         {"application_family_qualified", 0},
         {"platforms", caps.empty() ? array() : array({"Linux x86_64"})},
         {"missing_evidence", "Independent whole-application and "
                              "cross-platform development probes pending."}});
  }
  write(
      package / "coverage.json",
      object({{"scope",
               "This V5 stage; legacy providers awaiting enrichment excluded"},
              {"families", families},
              {"development_programs", load(e / "integration.json")},
              {"usage_sketches", array({"append-backward-then-reverse"})},
              {"published", 0}}));
  write(package / "manifest.json",
        object({{"schema", "unllm.staged-acquisition/1"},
                {"baseline_sha256", base_hash},
                {"contract_version", "unllm.brick-contract/1"},
                {"vocabulary_version", "unllm.brick-vocabulary/1"},
                {"bundle_sha256", digest(package / "bundle/SHA256SUMS")},
                {"status", "local staged review"},
                {"files", tree_hashes(package)}}));
  verify(package, digest(package / "manifest.json"));
  fs::rename(package, dest / "package");
}
J verify(const Path &package, const std::string &expected) {
  check(digest(package / "manifest.json") == expected,
        "handoff manifest digest mismatch");
  auto m = load(package / "manifest.json");
  check(m["schema"] == "unllm.staged-acquisition/1", "unknown package schema");
  auto actual = tree_hashes(package);
  actual.removeMember("manifest.json");
  check(actual == m["files"], "package file inventory or digest mismatch");
  check(m["baseline_sha256"] == base_hash &&
            m["bundle_sha256"] == digest(package / "bundle/SHA256SUMS"),
        "bundle digest mismatch");
  auto schema = load(package / "contract.schema.json"),
       contracts = load(package / "contracts.json"), ids = array();
  Database db(package / "contracts.sqlite", true);
  std::vector<Strings> rows;
  for (const auto &c : contracts) {
    validate(c, schema);
    check(digest(project_file(package / "bundle",
                              c["source"]["header"].asString())) ==
              c["source"]["implementation_sha256"].asString(),
          "provider implementation mismatch");
    check(digest(package / "sources.json") ==
              c["source"]["source_registry_sha256"].asString(),
          "stale source record");
    ids.append(c["capability_id"]);
    rows.push_back({c["capability_id"].asString(), c["provider_id"].asString(),
                    sha(dump(c))});
  }
  std::sort(rows.begin(), rows.end());
  check(rows == db.query("SELECT capability,provider,contract_hash FROM "
                         "provider ORDER BY 1,2,3"),
        "stale structured projection");
  check(load(package / "cross-check.json")["contract_set_sha256"] ==
            digest(package / "contracts.json"),
        "stale search receipts");
  return ids;
}
} // namespace v5
