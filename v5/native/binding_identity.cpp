#include "bundle.hpp"
#include "database.hpp"
#include "migration_reports.hpp"
#include "pilot.hpp"
#include <QByteArray>
#include <QStandardPaths>

namespace v5 {
namespace {
// The component locates shared implementation bytes. Each row names one
// binding.
J identify(const J &raw, const Path &bundle, const J &contracts) {
  J key = object({{"component_id", raw["brick"]},
                  {"binding_id", raw["id"]},
                  {"from", raw["from"]},
                  {"to", raw["to"]}});
  auto header = project_file(bundle / "headers", raw["header"].asString());
  J record = object({{"key", key},
                     {"logical_id", compact_hash(key)},
                     {"binding_sha256", compact_hash(raw)},
                     {"effect", raw["effect"]},
                     {"call", raw["call"]},
                     {"header", raw["header"]},
                     {"header_sha256", digest(header)},
                     {"contract_sha256", J()},
                     {"qualification", "legacy-only"}});
  for (const auto &c : contracts) {
    auto &iface = c["interface"];
    if (c["provider_id"] != raw["brick"] ||
        c["capability_id"] != raw["effect"] ||
        iface["structural_input"] != raw["from"] ||
        iface["structural_output"] != raw["to"])
      continue;
    check(record["contract_sha256"].isNull() && raw["id"] == "sort_lines" &&
              iface["callable"] == raw["call"] &&
              c["source"]["implementation_sha256"] == record["header_sha256"],
          "ambiguous or stale staged contract");
    check_sequence_evidence(bundle.parent_path(), c, raw);
    record["contract_sha256"] = sha(dump(c));
    record["qualification"] = c["qualification"]["level"];
  }
  return record;
}
// SQLite's IS operator preserves SQL NULL distinctly from an empty effect.
Strings binding_values(const J &r) {
  const auto &k = r["key"];
  return {r["logical_id"].asString(),
          k["component_id"].asString(),
          k["binding_id"].asString(),
          k["from"].asString(),
          k["to"].asString(),
          r["binding_sha256"].asString(),
          r["effect"].isNull() ? "1" : "0",
          r["effect"].asString(),
          r["call"].asString(),
          r["header"].asString(),
          r["header_sha256"].asString(),
          r["contract_sha256"].isNull() ? "1" : "0",
          r["contract_sha256"].asString(),
          r["qualification"].asString()};
}
void index_binding(Database &db, const J &r) {
  db.insert("INSERT INTO binding VALUES(?,?,?,?,?,?,CASE WHEN ?='1' THEN NULL "
            "ELSE ? END,?,?,?,CASE WHEN ?='1' THEN NULL ELSE ? END,?)",
            binding_values(r));
}
std::vector<Strings> lookup(Database &db, const J &r) {
  return db.query(
      "SELECT binding_id,qualification FROM binding WHERE logical_id=? "
      "AND component_id=? AND binding_id=? AND inport=? AND outport=? "
      "AND binding_sha256=? AND effect IS CASE WHEN ?='1' THEN NULL ELSE ? END "
      "AND callable=? AND header=? AND header_sha256=? "
      "AND contract_sha256 IS CASE WHEN ?='1' THEN NULL ELSE ? END AND "
      "qualification=?",
      binding_values(r));
}
J controls(Database &db, const J &sort, const J &writer) {
  J results = array();
  for (auto [kept, removed] :
       {std::pair{sort, writer}, std::pair{writer, sort}}) {
    db.script("SAVEPOINT subtract");
    db.insert("DELETE FROM binding WHERE logical_id=?",
              {removed["logical_id"].asString()});
    check(lookup(db, removed).empty() && lookup(db, kept).size() == 1,
          "removing a binding affected its sibling");
    db.script("ROLLBACK TO subtract; RELEASE subtract");
    results.append("remove " + removed["key"]["binding_id"].asString());
  }
  auto reject = [&](std::string label, J bad) {
    check(lookup(db, bad).empty(), "accepted mismatched " + label);
    results.append(label);
  };
  for (auto field : {"logical_id", "effect", "call", "binding_sha256",
                     "contract_sha256", "qualification"}) {
    J bad = writer;
    bad[field] = sort[field];
    if (bad[field] == writer[field])
      bad[field] = "legacy-only";
    reject(field, bad);
  }
  for (auto field : {"from", "to", "component_id", "binding_id"}) {
    J bad = writer;
    bad["key"][field] = "different";
    bad["logical_id"] = compact_hash(bad["key"]);
    reject(field, bad);
  }
  for (auto field : {"header", "header_sha256", "effect"}) {
    J bad = writer;
    bad[field] = "";
    reject(std::string("empty ") + field, bad);
  }
  return results;
}
J ordinary_program(const Path &bundle, const Path &work) {
  auto build = work / "ordinary", scratch = work / ".test-programs";
  fs::create_directory(scratch);
  auto spec = spec_for("text/plain", "text/plain",
                       object({{"outcome", "sort_lines"},
                               {"parameters", object()},
                               {"uncovered", array()}}));
  auto receipt = plan(bundle, spec, build);
  auto p = receipt["providers"][0];
  check(receipt["providers"].size() == 1 && p["status"] == "complete" &&
            p["uncovered"].empty(),
        "incomplete legacy sort-to-file plan");
  J ids = array();
  for (auto &step : p["plan"]["steps"])
    ids.append(step["provider"]);
  check(ids == array({"read_lines", "sort_lines", "write_lines"}),
        "ordinary path changed bindings");
  auto exe = scratch / "program";
  build_emission(p, build, exe, "g++-15");
  // Inputs/expected bytes are authored independently of the component
  // projections.
  J cases =
      array({array({"", ""}), array({"7a0a620a7a0a610a", "610a620a7a0a7a0a"}),
             array({"62000a0a61", "0a610a62000a"})});
  for (Json::ArrayIndex i = 0; i < cases.size(); ++i) {
    auto dir = build / ("case-" + std::to_string(i));
    fs::create_directory(dir);
    save(dir / "input",
         QByteArray::fromHex(QByteArray::fromStdString(cases[i][0].asString()))
             .toStdString());
    save(dir / "output", "old destination");
    auto cmd = sandbox(exe, dir);
    append(cmd, {"input", "output"});
    auto proc = execute(cmd, {.cwd = dir, .seconds = 8, .probe_limits = true});
    check(proc.status == 0 && QByteArray::fromStdString(bytes(dir / "output"))
                                      .toHex()
                                      .toStdString() == cases[i][1].asString(),
          "incorrect sort-to-file bytes");
  }
  J evidence = object(
      {{"spec", spec},
       {"bindings", ids},
       {"cases_input_expected_hex", cases},
       {"verdict", "passed"},
       {"program_sha256", digest(exe)},
       {"inputs", tree_hashes(build / "bricks")},
       {"main_sha256", digest(build / "main.cpp")},
       {"emitted_header_sha256", digest(build / "component.hpp")},
       {"helpers_sha256", digest(build / "components/helpers.json")},
       {"compiler_sha256",
        digest(QStandardPaths::findExecutable("g++-15").toStdString())}});
  write(work / "tested-program.json", evidence);
  run({"sync", "-f", work / "tested-program.json"});
  fs::remove(exe);
  fs::remove(scratch);
  return evidence;
}
} // namespace
J sequence_writer_identities(const Path &package) {
  const Path bundle = package / "bundle";
  auto raw = load(bundle / "providers/contracts.json")["providers"];
  auto contracts = load(package / "contracts.json");
  J records = object();
  for (const auto &row : raw) {
    if (row["brick"] != "x09" ||
        (row["id"] != "sort_lines" && row["id"] != "write_lines"))
      continue;
    auto id = row["id"].asString();
    check(!records.isMember(id), "duplicate x09 identity fixture row");
    records[id] = identify(row, bundle, contracts);
  }
  const auto &sort = records["sort_lines"], &writer = records["write_lines"];
  check(records.size() == 2 &&
            sort["qualification"] == "ordinary-emission-tested" &&
            writer["qualification"] == "legacy-only" &&
            writer["effect"].isNull() &&
            sort["header_sha256"] == writer["header_sha256"] &&
            sort["logical_id"] != writer["logical_id"],
        "unexpected stage identity or evidence");
  return array({sort, writer});
}
void project_binding_pair(const Path &stage) {
  auto canonical = load(stage / "payload.json");
  auto &bindings = canonical["bindings"];
  check(bindings.size() == 2, "expected two binding records");
  const auto &sort = bindings[0], &writer = bindings[1];
  J checked;
  {
    Database db(stage / "bindings.sqlite");
    db.script("CREATE TABLE binding(logical_id TEXT PRIMARY KEY,component_id "
              "TEXT NOT NULL,"
              "binding_id TEXT NOT NULL,inport TEXT NOT NULL,outport TEXT NOT "
              "NULL,binding_sha256 TEXT NOT NULL,"
              "effect TEXT,callable TEXT NOT NULL,header TEXT NOT "
              "NULL,header_sha256 TEXT NOT NULL,"
              "contract_sha256 TEXT,qualification TEXT NOT "
              "NULL,UNIQUE(component_id,binding_id,inport,outport))");
    for (const auto &r : bindings)
      index_binding(db, r);
    check(lookup(db, sort) ==
                  std::vector<Strings>{
                      {"sort_lines", "ordinary-emission-tested"}} &&
              lookup(db, writer) ==
                  std::vector<Strings>{
                      {"write_lines", writer["qualification"].asString()}},
          "binding lookup failed");
    checked = controls(db, sort, writer);
  }
  write(stage / "controls.json", checked);
}
Path binding_identity_fixture(const Path &package, const std::string &pin) {
  verify(package, pin);
  auto bindings = sequence_writer_identities(package);
  auto work = fresh_run("v5-binding-identity"), stage = work / ".fixture";
  fs::create_directory(stage);
  write(stage / "payload.json",
        object({{"schema", "unllm.binding-identity-fixture/1"},
                {"catalog_sha256", pin},
                {"bindings", bindings},
                {"scope", "Detached development fixture"}}));
  project_binding_pair(stage);
  write(stage / "ordinary.json", ordinary_program(package / "bundle", work));
  auto manifest =
      object({{"schema", "unllm.binding-identity-fixture-manifest/1"},
              {"catalog_sha256", pin},
              {"files", tree_hashes(stage)}});
  write(stage / "manifest.json", manifest);
  fs::rename(stage, work / "fixture");
  return work / "fixture/manifest.json";
}
} // namespace v5
