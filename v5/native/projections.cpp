#include "queue.hpp"
#include <algorithm>
#include <cstdlib>
#include <regex>
#include <sstream>

namespace v5 {
Path tantivy_reference() {
  const char *value = std::getenv("UNLLM_TANTIVY_REFERENCE_DIR");
  return fs::canonical(value && *value ? Path(value)
                                       : main_repo / "search/target/release");
}
J select_binding(const J &rows, const std::string &capability,
                 const std::string &brick, const std::string &input,
                 const std::string &output) {
  J match;
  for (const auto &row : rows)
    if (row["effect"] == capability && row["brick"] == brick &&
        row["from"] == input && row["to"] == output) {
      check(match.isNull(), "ambiguous provider binding for " + brick);
      match = row;
    }
  check(!match.isNull(), "missing provider binding for " + brick);
  return match;
}
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
std::string header_path(const Path &bundle, const J &binding) {
  const auto name = binding["header"].asString();
  check(Path(name).filename() == name, "unexpected planner header name");
  return (fs::is_regular_file(bundle / "providers" / name) ? "providers/"
                                                           : "headers/") +
         name;
}
J contract(const J &a, const Path &work, const J &registry, const J &bindings) {
  auto key = a["id"].asString();
  auto src = registry[a["source_id"].asString()];
  const auto &selector = a["binding"];
  J row =
      select_binding(bindings["providers"], key, selector["brick"].asString(),
                     selector["from"].asString(), selector["to"].asString());
  auto test = "evidence/standalone.json#" + key,
       end = "evidence/integration.json#" + key;
  auto standalone = load(work / "materials/standalone/standalone.json")[key];
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
  auto header = header_path(work / "integration/bundle", row);
  auto parameters = bindings["outcomes"][key]["parameters"];
  // The invocation interface contains callable inputs. A fixed dispatch choice
  // selects the template specialization in the pinned planner binding.
  for (const auto &name : row["when"].getMemberNames()) {
    check(!contains(row["args"], name) && parameters[name].isArray() &&
              contains(parameters[name], row["when"][name]),
          "unsupported dispatch predicate for " + key);
    parameters.removeMember(name);
  }
  return object(
      {{"schema", "unllm.brick-contract/1"},
       {"vocabulary", "unllm.brick-vocabulary/1"},
       {"capability_id", key},
       {"provider_id", row["brick"]},
       {"legacy_brick", row["brick"]},
       {"description", bindings["outcomes"][key]["meaning"]},
       {"primary_family", a["primary_family"]},
       {"facets", facets},
       {"interface", object({{"callable", row["call"]},
                             {"structural_input", row["from"]},
                             {"structural_output", row["to"]},
                             {"semantic_input", a["semantic_input"]},
                             {"semantic_output", a["semantic_output"]},
                             {"parameters", parameters}})},
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
            {{"suite_sha256",
              digest(v5_root() / "fixtures" /
                     a.get("probe_suite", "requirements.json").asString())},
             {"observations_sha256",
              key == "prefixed_lines"
                  ? prefix_fingerprint(integration)
                  : sha(dump(integration["staged"]["probes"]))},
             {"scope", "fixed development probes on this Linux toolchain"}})},
       {"property_signature", signature},
       {"qualification",
        object({{"level", "ordinary-emission-tested"},
                {"independent", test},
                {"integration", end},
                {"scope", "Local staged fixed typed development task with "
                          "removal control; external review pending"}})}});
}
using Rows = std::vector<Strings>;
struct Lifecycle {
  Rows steps, component_inputs;
};
Lifecycle lifecycle(const Path &package, const J &c, const J &binding) {
  Lifecycle result;
  auto &rows = result.steps;
  const auto &claim = c["claims"]["state_protocol"];
  if (claim["status"] != "observed")
    return result;
  const std::string prefix = "evidence/lifecycle.json#";
  for (const auto &ref : claim["evidence"]) {
    auto path = ref.asString();
    if (!path.starts_with(prefix))
      continue;
    auto id = path.substr(prefix.size());
    auto evidence = load(package / "evidence/lifecycle.json")[id];
    check(rows.empty() && evidence["schema"] == "unllm.observed-lifecycle/1" &&
              evidence["provider"] == c["provider_id"] &&
              id == binding["id"].asString() &&
              evidence["binding"] == binding["id"] &&
              evidence["columns"] ==
                  array({"instance", "key", "event", "response"}),
          "lifecycle evidence identity mismatch");
    auto fixture = package / "evidence";
    check(digest(fixture / "latch-client.cpp") ==
                  evidence["client_sha256"].asString() &&
              digest(fixture / "latch-rows-client.cpp") ==
                  evidence["adapter_client_sha256"].asString() &&
              digest(fixture / "latch-traces.json") ==
                  evidence["oracle_sha256"].asString(),
          "lifecycle fixture pin mismatch");
    auto source = load(fixture / "latch-source.json");
    check(source["revision"] == c["source"]["revision"] &&
              source["archive_sha256"] == c["source"]["archive_sha256"] &&
              source["unchanged_members"]["headers/keyed_latch.hpp"][1] ==
                  evidence["implementation_sha256"],
          "lifecycle implementation pin mismatch");
    for (const auto &f : source["unchanged_members"].getMemberNames())
      check(digest(project_file(package / "bundle", f)) ==
                source["unchanged_members"][f][1].asString(),
            "changed retained header " + f);
    const auto &traces = evidence["traces"];
    auto alphabet = strings(evidence["bounded_reference"]["event_alphabet"]);
    std::sort(alphabet.begin(), alphabet.end());
    check(!alphabet.empty() &&
              std::adjacent_find(alphabet.begin(), alphabet.end()) ==
                  alphabet.end(),
          "invalid component event alphabet");
    for (const auto &event : alphabet)
      result.component_inputs.push_back({c["provider_id"].asString(), event});
    check(traces == load(fixture / "latch-traces.json")["traces"] &&
              !traces.empty(),
          "lifecycle observations differ from oracle");
    for (const auto &name : traces.getMemberNames()) {
      unsigned ordinal = 0;
      for (const auto &s : traces[name]) {
        check(s.size() == 4, "invalid lifecycle columns");
        Strings row = {c["provider_id"].asString(), name,
                       std::to_string(++ordinal)};
        for (const auto &v : s)
          row.push_back(v.asString());
        for (const auto &v : row)
          check(!v.empty() && v.find_first_of("\t\r\n") == v.npos,
                "invalid lifecycle fact");
        rows.push_back(std::move(row));
      }
    }
    check(!rows.empty(), "empty lifecycle receipt");
  }
  return result;
}
void project(const Path &package, const Path &scratch, const J &contracts,
             const J &bindings, const J &classes, const J &ports) {
  Database db(package / "contracts.sqlite");
  db.script(R"sql(
        CREATE TABLE provider(capability TEXT, provider TEXT PRIMARY KEY, contract_hash TEXT, family TEXT, level TEXT);
        CREATE TABLE port(provider TEXT PRIMARY KEY, input TEXT, output TEXT, semantic_input TEXT, semantic_output TEXT);
        CREATE TABLE facet(provider TEXT, name TEXT, value TEXT, PRIMARY KEY(provider,name,value));
        CREATE TABLE property(provider TEXT, name TEXT, status TEXT, detail TEXT, PRIMARY KEY(provider,name));
        CREATE TABLE lifecycle(provider TEXT, trace TEXT, step INTEGER, instance TEXT, subject TEXT, event TEXT, response TEXT, PRIMARY KEY(provider,trace,step));
        CREATE TABLE component_input(provider TEXT, event TEXT, PRIMARY KEY(provider,event));
        CREATE TABLE class_binding(provider TEXT PRIMARY KEY,type TEXT,header TEXT,level TEXT,contract_hash TEXT);
        CREATE TABLE class_method(provider TEXT,operation TEXT,member TEXT,parameter TEXT,result TEXT,PRIMARY KEY(provider,operation));
        CREATE TABLE class_value_type(provider TEXT,name TEXT,cpp_type TEXT,PRIMARY KEY(provider,name));
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
  for (const Strings &row : std::vector<Strings>{
           {"name", "brickcontracts"},
           {"kind", "brick-contract"},
           {"passages", std::to_string(contracts.size() + classes.size())},
           {"version", "1"}})
    search.insert("INSERT INTO meta VALUES(?,?)", row);
  auto port_terms = index_semantic_ports(package, db, ports);
  int pid = 0;
  auto entries = contracts;
  for (const auto &c : classes)
    entries.append(c);
  for (const auto &c : entries) {
    auto id = c["provider_id"].asString(),
         cap = c.get("capability_id", c["provider_id"]).asString();
    const auto &port = c["interface"];
    auto doc = std::to_string(++pid);
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
    if (port_terms.contains(id))
      append(text, port_terms.at(id));
    Lifecycle events;
    if (port["kind"] == "library-class") {
      db.insert("INSERT INTO class_binding VALUES(?,?,?,?,?)",
                {id, port["type"].asString(), port["header"].asString(),
                 c["qualification"]["level"].asString(), sha(dump(c))});
      for (const auto &op : port["methods"].getMemberNames()) {
        const auto &m = port["methods"][op];
        db.insert("INSERT INTO class_method VALUES(?,?,?,?,?)",
                  {id, op, m["member"].asString(), m["parameter"].asString(),
                   m["result"].asString()});
        text.push_back(m["meaning"].asString());
      }
      for (const auto &v : port["value_types"].getMemberNames())
        db.insert("INSERT INTO class_value_type VALUES(?,?,?)",
                  {id, v, port["value_types"][v].asString()});
      text.push_back(c["claims"]["state_protocol"]["detail"].asString());
    } else {
      db.insert("INSERT INTO provider VALUES(?,?,?,?,?)",
                {cap, id, sha(dump(c)), c["primary_family"].asString(),
                 c["qualification"]["level"].asString()});
      db.insert("INSERT INTO port VALUES(?,?,?,?,?)",
                {id, port["structural_input"].asString(),
                 port["structural_output"].asString(),
                 port["semantic_input"].asString(),
                 port["semantic_output"].asString()});
      auto binding =
          select_binding(bindings, cap, id, port["structural_input"].asString(),
                         port["structural_output"].asString());
      events = lifecycle(package, c, binding);
    }
    if (!events.steps.empty()) {
      std::set<std::string> terms;
      text.push_back(c["claims"]["state_protocol"]["detail"].asString());
      for (const auto &input : events.component_inputs) {
        db.insert("INSERT INTO component_input VALUES(?,?)", input);
        terms.insert(input[1]);
      }
      for (const auto &row : events.steps) {
        db.insert("INSERT INTO lifecycle VALUES(?,?,?,?,?,?,?)", row);
        if (std::binary_search(events.component_inputs.begin(),
                               events.component_inputs.end(),
                               Strings{id, row[5]}))
          terms.insert(row[6]);
      }
      text.insert(text.end(), terms.begin(), terms.end());
    }
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
           {"LifecycleStep",
            "SELECT * FROM lifecycle ORDER BY provider,trace,step"},
           {"ComponentInput",
            "SELECT * FROM component_input ORDER BY provider,event"},
           {"SemanticPort",
            "SELECT "
            "provider,binding,direction,COALESCE(role,'__absent'),role_status,"
            "COALESCE(unit,'__absent'),unit_status,COALESCE(cardinality,'__"
            "absent'),cardinality_status FROM semantic_port ORDER BY "
            "provider,binding,direction"},
           {"ClassBinding", "SELECT provider,type,header,level FROM "
                            "class_binding ORDER BY provider"},
           {"ClassMethod",
            "SELECT * FROM class_method ORDER BY provider,operation"},
           {"ClassValue",
            "SELECT * FROM class_value_type ORDER BY provider,name"},
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
            "SELECT DISTINCT capability,input,output FROM provider JOIN port "
            "USING(provider) WHERE level='ordinary-emission-tested'"},
           {"MeasuredOutput",
            "SELECT s.provider,binding,role,unit,cardinality FROM "
            "semantic_port s JOIN provider p USING(provider) WHERE "
            "direction='output' AND level='ordinary-emission-tested' AND "
            "role_status='observed' AND unit_status='observed' AND "
            "cardinality_status='observed'"},
           {"Observed",
            "SELECT DISTINCT capability,name FROM provider JOIN property "
            "USING(provider) WHERE status='observed'"},
           {"TestedMethod",
            "SELECT provider,operation,name FROM class_binding JOIN "
            "class_method USING(provider) JOIN class_value_type "
            "USING(provider) WHERE level='ordinary-header-tested'"},
           {"ObservedEvent",
            "SELECT DISTINCT capability,event,response FROM provider JOIN "
            "lifecycle USING(provider) JOIN property USING(provider) WHERE "
            "name='state_protocol' AND status='observed' AND EXISTS(SELECT 1 "
            "FROM component_input i WHERE i.provider=provider.provider AND "
            "i.event=lifecycle.event)"}}) {
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
  auto rust = tantivy_reference(),
       reader = probe_program(scratch / "tantivy-query");
  run({"env", "-u", "HOME", rust / "unllm_index", pack, "--threads", "1",
       "--heap-mb", "32"});
  run({"c++", "-std=c++17", v5_root() / "fixtures/tantivy-query.cpp",
       rust / "libunllm_search.a", "-ldl", "-lpthread", "-lm", "-o", reader});
  J results = object();
  for (auto [q, want] :
       std::map<std::string, std::string>{{"digit", direct},
                                          {"multiplicity", api},
                                          {"rearm", "threshold_entries"},
                                          {"oldest", "u84"},
                                          {"octet", "byte_lengths"},
                                          {"dimensionless", "item_count"},
                                          {"checksum", "line_xxh32"},
                                          {"ascending", "sort_lines"},
                                          {"outer", "reverse_order"},
                                          {"occurrence", "unique_lines"},
                                          {"maximal", "adjacent_unique"},
                                          {"whitespace", "nonempty_lines"},
                                          {"beginning", "prefixed_lines"}}) {
    auto id = std::to_string(std::stoll(run({reader, pack / "tantivy", q})));
    check(search.query("SELECT title FROM docs WHERE doc_id=?", {id}) ==
              std::vector<Strings>{{want}},
          "Rust Tantivy retrieval mismatch");
    results[q] = want;
  }
  write(
      package / "cross-check.json",
      object(
          {{"logical", logical},
           {"tantivy", results},
           {"semantic_ports_sha256", digest(package / "semantic-ports.json")},
           {"contract_set_sha256", digest(package / "contracts.json")},
           {"class_contracts_sha256", digest(package / "class-contracts.json")},
           {"tantivy_binary", digest(rust / "unllm_index")},
           {"query_library", digest(rust / "libunllm_search.a")},
           {"queries", "fixed development terms"}}));
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
  auto checkpoints = array();
  for (const auto &job : pilot_jobs(load(v5_root() / "sources.json")))
    if (job.name != "stage")
      checkpoints.append(load(work / job.name / "checkpoint.json"));
  write(e / "acquisition.json",
        object({{"schema", "unllm.acquisition-evidence/1"},
                {"registry_sha256", digest(v5_root() / "sources.json")},
                {"roadmap_sha256",
                 digest(main_repo / "docs/UNLLM_MASTER_PLAN_V5.md")},
                {"activation", "pending main-project review"},
                {"checkpoints", checkpoints},
                {"human_time", "not measured for this replay"}}));
  for (auto [stage, f] : std::vector<std::pair<std::string, std::string>>{
           {"inventory", "inputs.json"},
           {"extract", "extraction.json"},
           {"extract", "sketch.json"},
           {"extract", "latch-source.json"},
           {"extract", "fifo-source.json"},
           {"extract", "measure-source.json"},
           {"extract", "xxhash-source.json"},
           {"standalone", "lifecycle.json"},
           {"standalone", "standalone.json"},
           {"integration", "integration.json"}})
    fs::copy_file((stage == "extract" || stage == "standalone"
                       ? work / "materials"
                       : work) /
                      stage / f,
                  e / f);
  for (const auto *f :
       {"contract.schema.json", "vocabulary.json", "sources.json", "facts.dl"})
    fs::copy_file(v5_root() / f, package / f);
  fs::copy(work / "materials/extract/notices", package / "notices",
           fs::copy_options::recursive);
  fs::copy_file(v5_root() / "fixtures/requirements.json",
                e / "requirements.json");
  fs::copy_file(root / "tests/client.cpp", e / "xxhash-client.cpp");
  fs::copy_file(root / "contracts/tasks.json", e / "xxhash-tasks.json");
  fs::copy_file(v5_root() / "semantic-vocabulary.json",
                package / "semantic-vocabulary.json");
  for (const auto *name : {"measure-client.cpp", "measure-probes.json",
                           "sequence-client.cpp", "sequence-probes.json",
                           "prefix-client.cpp", "prefix-probes.json"})
    fs::copy_file(v5_root() / "fixtures" / name, e / name);
  fs::copy_file(work / "integration/fifo/fifo-integration.json",
                e / "fifo-integration.json");
  for (const auto *name :
       {"latch-traces.json", "latch-client.cpp", "latch-rows-client.cpp",
        "fifo-client.cpp", "fifo-spec.json"})
    fs::copy_file(v5_root() / "fixtures" / name, e / name);
  fs::copy(work / "integration/bundle", package / "bundle",
           fs::copy_options::recursive);
  auto registry = sources(),
       bindings = load(package / "bundle/providers/contracts.json"),
       schema = load(package / "contract.schema.json");
  auto contracts = array(), ports = array();
  for (const auto &a : load(v5_root() / "fixtures/annotations.json")) {
    auto c = contract(a, work, registry, bindings);
    validate(c, schema);
    contracts.append(c);
    const auto &iface = c["interface"];
    auto binding = select_binding(
        bindings["providers"], c["capability_id"].asString(),
        c["provider_id"].asString(), iface["structural_input"].asString(),
        iface["structural_output"].asString());
    for (const auto &p : semantic_ports(c, binding, a))
      ports.append(p);
  }
  write(package / "contracts.json", contracts);
  check_semantic_ports(package, ports, contracts, bindings["providers"]);
  write(package / "semantic-ports.json", ports);
  auto cs = class_schema(schema,
                         load(v5_root() / "class-interface.schema.json")),
       klass = class_contract(package);
  validate(klass, cs);
  auto classes = array({klass});
  write(package / "class-vocabulary.json",
        object({{"id", klass["vocabulary"]},
                {"extends", load(package / "vocabulary.json")["id"]},
                {"interface_kinds", array({klass["interface"]["kind"]})},
                {"qualification", array({klass["qualification"]["level"]})}}));
  write(package / "class-contract.schema.json", cs);
  write(package / "class-contracts.json", classes);
  project(package, dest, contracts, bindings["providers"], classes, ports);
  auto guide = load(package / "vocabulary.json")["primary_families"],
       families = object();
  for (const auto &family : guide.getMemberNames()) {
    auto caps = array();
    for (const auto &c : contracts)
      if (c["primary_family"] == family && !contains(caps, c["capability_id"]))
        caps.append(c["capability_id"]);
    families[family] = object(
        {{"guidance", guide[family]},
         {"staged_capabilities", caps},
         {"retained_class_providers", klass["primary_family"] == family
                                          ? array({klass["provider_id"]})
                                          : array()},
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
              {"retained_class_providers", array({klass["provider_id"]})},
              {"class_methods",
               string_array(klass["interface"]["methods"].getMemberNames())},
              {"class_header_trials", load(e / "fifo-integration.json")},
              {"usage_sketches", array({"append-backward-then-reverse"})},
              {"published", 0}}));
  write(
      package / "manifest.json",
      object({{"schema", "unllm.staged-acquisition/1"},
              {"baseline_sha256", base_hash},
              {"contract_version", "unllm.brick-contract/1"},
              {"interfaces",
               object({{"semantic_ports",
                        object({{"version", "unllm.semantic-ports/1"},
                                {"contracts", "semantic-ports.json"},
                                {"vocabulary", "semantic-vocabulary.json"}})},
                       {"library_class",
                        object({{"version", "unllm.library-class/1"},
                                {"contracts", "class-contracts.json"},
                                {"vocabulary", "class-vocabulary.json"},
                                {"schema", "class-contract.schema.json"}})}})},
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
  const auto bindings =
      load(package / "bundle/providers/contracts.json")["providers"];
  std::vector<Strings> rows;
  for (const auto &c : contracts) {
    validate(c, schema);
    auto iface = c["interface"];
    auto binding = select_binding(bindings, c["capability_id"].asString(),
                                  c["legacy_brick"].asString(),
                                  iface["structural_input"].asString(),
                                  iface["structural_output"].asString());
    check(c["provider_id"] == binding["brick"] &&
              iface["callable"] == binding["call"] &&
              c["source"]["header"].asString() ==
                  header_path(package / "bundle", binding),
          "contract implementation does not match selected binding");
    check(digest(project_file(package / "bundle",
                              c["source"]["header"].asString())) ==
              c["source"]["implementation_sha256"].asString(),
          "provider implementation mismatch");
    check(digest(package / "sources.json") ==
              c["source"]["source_registry_sha256"].asString(),
          "stale source record");
    if (std::find(sequence_keys.begin(), sequence_keys.end(),
                  c["capability_id"].asString()) != sequence_keys.end())
      check_sequence_evidence(package, c, binding);
    if (c["capability_id"] == "prefixed_lines")
      check_prefix_evidence(package, c, binding);
    ids.append(c["capability_id"]);
    auto events = lifecycle(package, c, binding);
    if (!events.steps.empty()) {
      check(db.query(
                "SELECT * FROM lifecycle WHERE provider=? ORDER BY trace,step",
                {c["provider_id"].asString()}) == events.steps,
            "lifecycle projection differs from observations");
      if (fs::is_regular_file(package / "facts/ComponentInput.facts"))
        check(
            db.query(
                "SELECT * FROM component_input WHERE provider=? ORDER BY event",
                {c["provider_id"].asString()}) == events.component_inputs,
            "component event alphabet differs from bounded probes");
    }
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
  const auto &view = m["interfaces"]["library_class"];
  if (!view.isNull()) {
    check(view["version"] == "unllm.library-class/1" &&
              view["schema"] == "class-contract.schema.json" &&
              view["contracts"] == "class-contracts.json",
          "unknown class interface");
    auto classes = load(package / "class-contracts.json"),
         library = load(package / "bundle/providers/library.json"),
         cs = load(package / "class-contract.schema.json"),
         standalone = load(package / "evidence/standalone.json");
    Rows descriptors, methods, values;
    for (const auto &c : classes) {
      validate(c, cs);
      auto provider = c["provider_id"].asString();
      const auto &iface = c["interface"];
      if (view.isMember("vocabulary")) {
        check(view["vocabulary"] == "class-vocabulary.json",
              "unknown class vocabulary location");
        auto voc = load(package / "class-vocabulary.json");
        check(voc["id"] == c["vocabulary"] &&
                  voc["extends"] == m["vocabulary_version"] &&
                  contains(voc["qualification"], c["qualification"]["level"]) &&
                  contains(voc["interface_kinds"], iface["kind"]),
              "class vocabulary does not cover contract");
      }
      auto entry = iface;
      entry.removeMember("kind");
      entry.removeMember("value_types");
      entry["brick"] = provider;
      check(std::count(library["classes"].begin(), library["classes"].end(),
                       entry) == 1 &&
                iface["value_types"] == library["value_types"] &&
                c["source"]["header"].asString() ==
                    "headers/" + iface["header"].asString() &&
                c["source"]["implementation_sha256"] ==
                    digest(project_file(package / "bundle",
                                        c["source"]["header"].asString())) &&
                c["source"]["source_registry_sha256"] ==
                    digest(package / "sources.json"),
            "class does not match pinned implementation");
      check(standalone[provider]["client_sha256"] ==
                    digest(package / "evidence/fifo-client.cpp") &&
                c["fingerprint"]["observations_sha256"] ==
                    sha(dump(load(package / "evidence/fifo-integration.json"))),
            "stale class qualification receipt");
      auto retained = load(package / "evidence/fifo-source.json");
      check(retained["source_id"] == c["source"]["id"] &&
                retained["revision"] == c["source"]["revision"] &&
                retained["archive_sha256"] == c["source"]["archive_sha256"] &&
                retained["class"] == entry &&
                retained["value_types"] == iface["value_types"],
            "stale retained class source");
      for (const auto &member : retained["unchanged_members"].getMemberNames())
        check(retained["unchanged_members"][member][1] ==
                  digest(project_file(package / "bundle", member)),
              "stale retained class member");
      descriptors.push_back(
          {provider, iface["type"].asString(), iface["header"].asString(),
           c["qualification"]["level"].asString(), sha(dump(c))});
      for (const auto &op : iface["methods"].getMemberNames()) {
        const auto &method = iface["methods"][op];
        methods.push_back({provider, op, method["member"].asString(),
                           method["parameter"].asString(),
                           method["result"].asString()});
      }
      for (const auto &v : iface["value_types"].getMemberNames())
        values.push_back({provider, v, iface["value_types"][v].asString()});
      for (const auto &prop : c["claims"].getMemberNames())
        check(db.query("SELECT status,detail FROM property WHERE provider=? "
                       "AND name=?",
                       {provider, prop}) ==
                  Rows{{c["claims"][prop]["status"].asString(),
                        c["claims"][prop]["detail"].asString()}},
              "class property differs from contract");
      ids.append(provider);
    }
    std::sort(descriptors.begin(), descriptors.end());
    std::sort(methods.begin(), methods.end());
    std::sort(values.begin(), values.end());
    check(
        descriptors ==
                db.query("SELECT * FROM class_binding ORDER BY provider") &&
            methods ==
                db.query(
                    "SELECT * FROM class_method ORDER BY provider,operation") &&
            values ==
                db.query(
                    "SELECT * FROM class_value_type ORDER BY provider,name") &&
            load(package / "cross-check.json")["class_contracts_sha256"] ==
                digest(package / "class-contracts.json"),
        "stale structured class projection");
  }
  if (m["interfaces"].isMember("semantic_ports")) {
    auto view = m["interfaces"]["semantic_ports"];
    check(view["version"] == "unllm.semantic-ports/1" &&
              view["contracts"] == "semantic-ports.json" &&
              view["vocabulary"] == "semantic-vocabulary.json" &&
              load(package / "cross-check.json")["semantic_ports_sha256"] ==
                  digest(package / "semantic-ports.json"),
          "semantic view mismatch");
    auto ports = load(package / "semantic-ports.json");
    check_semantic_ports(package, ports, contracts, bindings);
    verify_semantic_index(db, ports);
  }
  return ids;
}
} // namespace v5
