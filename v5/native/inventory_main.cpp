#include "queue.hpp"
#include "migration_reports.hpp"
#include <algorithm>
#include <iostream>
#include <memory>
#include <sstream>

// Read only unpacked catalog metadata. A discovered occurrence has no authority
// to activate a provider, including when its descriptive row matches a trial.
using namespace v5;
namespace {
using Locations = std::map<Path, std::set<std::string>>;
const Strings metadata_names = {"catalog.json", "components.json",
                                "providers/contracts.json",
                                "providers/library.json"};
Locations locations() {
  auto paths =
      run({"rg", "--files", "--hidden", "--no-ignore", "--null", "-g",
           "catalog.json", "-g", "components.json", "-g",
           "**/providers/contracts.json", "-g", "**/providers/library.json",
           "-g", "!**/.git/**", "-g", "!**/v5-eval-private-*/**",
           volume / "unllm", main_repo / "experiments/boilerplate"},
          {.seconds = 180});
  std::istringstream input(paths);
  std::map<Path, std::set<std::string>> dirs;
  for (std::string f; std::getline(input, f, '\0');) {
    Path path = f;
    auto dir = path.parent_path();
    if (path.filename() == "contracts.json" ||
        path.filename() == "library.json")
      dir = dir.parent_path();
    dirs[dir].insert(path.lexically_relative(dir).generic_string());
  }
  return dirs;
}
bool under(const Path &path, const Path &dir) {
  auto rel = path.lexically_relative(dir);
  return !rel.empty() && *rel.begin() != "..";
}
struct Frozen {
  Path directory;
  std::string pin;
  Database db;
  static Path checked(const Path &dir, const std::string &pin) {
    check_corpus(dir);
    auto path = project_file(dir, "ledger.sqlite");
    check(digest(path) == pin, "frozen ledger pin mismatch");
    return path;
  }
  Frozen(const Path &dir, const std::string &hash)
      : directory(dir), pin(hash), db(checked(dir, hash), true) {
    db.script("PRAGMA trusted_schema=OFF; BEGIN");
  }
  Locations locations_for(const Path &stage, const Path &base) {
    auto active = db.query("SELECT path,legacy_manifest_sha256 FROM generation "
                           "WHERE role='active legacy'");
    check(active == std::vector<Strings>{{base, base_hash}},
          "frozen active generation differs");
    Locations dirs;
    for (const auto &r : db.query(
             "SELECT generation,path FROM input ORDER BY generation,path")) {
      Path dir = r[0], path = r[1];
      auto name = path.lexically_relative(dir).generic_string();
      check((under(dir, volume / "unllm") ||
             under(dir, main_repo / "experiments/boilerplate")) &&
                std::find(metadata_names.begin(), metadata_names.end(), name) !=
                    metadata_names.end(),
            "unsupported frozen inventory location");
      dirs[dir].insert(name);
    }
    if (!dirs.contains(stage))
      for (const auto &name : metadata_names)
        if (fs::is_regular_file(stage / name))
          dirs[stage].insert(name);
    return dirs;
  }
};
std::string implementation(const Path &dir, const J &row,
                           const std::string &kind) {
  if (!row.isObject() || !row["header"].isString())
    return {};
  auto name = row["header"].asString();
  if (name.empty() || Path(name).is_absolute())
    return {};
  std::vector<Path> candidates;
  if (kind == "binding")
    candidates.push_back(dir / "providers" / name);
  candidates.push_back(dir / "headers" / name);
  if (kind == "class")
    candidates.push_back(dir / "include" / name);
  if (kind == "component" && row["source"].isString())
    candidates.push_back(dir / row["source"].asString());
  for (const auto &candidate : candidates) {
    if (!fs::is_regular_file(candidate))
      continue;
    auto path = fs::canonical(candidate);
    if (under(path, dir) ||
        (under(dir, main_repo / "experiments/boilerplate") &&
         under(path, main_repo / "experiments/boilerplate")))
      return digest(path);
  }
  return {};
}
struct Qualified {
  J binding, contract;
};
std::vector<Qualified> admitted(const Path &pack) {
  auto rows = load(pack / "bundle/providers/contracts.json")["providers"];
  std::vector<Qualified> entries;
  for (const auto &c : load(pack / "contracts.json")) {
    auto iface = c["interface"];
    entries.push_back({select_binding(rows, c["capability_id"].asString(),
                                      c["provider_id"].asString(),
                                      iface["structural_input"].asString(),
                                      iface["structural_output"].asString()),
                       c});
  }
  if (load(pack / "manifest.json")["interfaces"].isMember("library_class")) {
    auto lib = load(pack / "bundle/providers/library.json");
    for (const auto &c : load(pack / "class-contracts.json"))
      for (const auto &cl : lib["classes"])
        if (cl["brick"] == c["provider_id"])
          entries.push_back({cl, c});
  }
  return entries;
}
struct Snapshot {
  Database &db;
  const Path base, pack, out;
  const std::vector<Qualified> qualified;
  Frozen *prior;
  size_t frozen_headers = 0;
  const J evidence = load(pack / "evidence/integration.json");
  const J registry = load(pack / "sources.json");
  void record(const std::string &file, unsigned ordinal, const Path &dir,
              const std::string &kind, const J &row,
              const std::string &header) {
    const auto field = [&](const std::string &name) {
      return row.isObject() && row[name].isString() ? row[name].asString()
                                                    : std::string{};
    };
    auto id = kind == "class" ? field("brick") : field("id"),
         brick = field("brick");
    auto bytes = dump(row),
         version = sha(dump(array({kind, sha(bytes), header})));
    db.insert("INSERT OR IGNORE INTO revision VALUES(?,?,?,?,?,?,?,?,?,?)",
              {version, kind, id, brick, field("effect"), field("from"),
               field("to"), sha(bytes), header, bytes});
    J claims = object(), proof = object();
    std::string status = "legacy-only pending review", contract,
                contract_version,
                reason = "Legacy occurrence has not passed V5 qualification";
    if (id.empty()) {
      status = "quarantined";
      reason = "Absent or unsupported identity";
    } else if ((kind == "binding" || kind == "class") && header.empty()) {
      status = "quarantined";
      reason = "Unresolved implementation header";
    } else if (kind == "binding" || kind == "class") {
      status = "structural inventory pending qualification";
      reason = "Semantic claims, complete dependency closure and independent "
               "qualification pending";
      if (dir == pack / "bundle" || dir == base)
        for (const auto &q : qualified) {
          const auto &c = q.contract;
          if (q.binding != row ||
              header != c["source"]["implementation_sha256"].asString())
            continue;
          // Baseline receipts and unchanged dependency members are required to
          // extend the selected staged proof to the original occurrence.
          if (dir == base) {
            if (kind == "class") {
              auto tests = load(pack / "evidence/fifo-integration.json");
              check(!tests.empty(), "missing class baseline");
              for (const auto &t :
                   c["interface"]["value_types"].getMemberNames())
                check(!tests[t]["baseline"]["observations"].asString().empty(),
                      "missing class specialization baseline");
            } else if (evidence[c["capability_id"].asString()]["baseline"]
                               ["result"] != "passed")
              continue;
            const J &s = registry["sources"];
            J members;
            for (const auto &source : s)
              if (source["id"] == c["source"]["id"])
                members = source["members"];
            bool same = !members.empty();
            for (const auto &member : members.getMemberNames())
              same &= digest(project_file(dir, member)) ==
                      members[member][1].asString();
            if (!same)
              continue;
          }
          check(contract.empty(),
                "multiple staged proofs for one legacy binding");
          contract = sha(dump(c));
          contract_version = c["schema"].asString();
          status = "qualified in isolated stage";
          reason = "Independent and emitted-program evidence for this binding "
                   "in pinned review package";
          proof = c["qualification"];
          for (const auto &k : c["claims"].getMemberNames())
            claims[k] = c["claims"][k]["status"];
        }
    }
    db.insert("INSERT INTO occurrence VALUES(?,?,?,?,?,?,?,?,?,?)",
              {file, std::to_string(ordinal), version, status, reason, contract,
               contract_version,
               contract.empty() ? "unassessed" : dump(claims, false),
               contract.empty() ? "" : dump(proof, false), "0"});
  }
  void inventory_file(const Path &dir, const std::string &name) {
    auto file = dir / name;
    auto cached =
        prior ? prior->db.query("SELECT bytes_sha256 FROM input WHERE path=?",
                                {file})
              : std::vector<Strings>{};
    const bool immutable = dir == base || dir == pack / "bundle";
    std::string raw;
    std::vector<Strings> observations;
    if (cached.empty())
      raw = bytes(file);
    else {
      raw =
          bytes(project_file(prior->directory / "metadata", cached.front()[0]));
      check(sha(raw) == cached.front()[0], "frozen metadata pin mismatch");
      if (immutable)
        check(digest(file) == cached.front()[0],
              "pinned generation metadata changed since inventory");
      observations = prior->db.query(
          "SELECT ordinal,row_sha256,header_sha256 FROM occurrence o JOIN "
          "revision r ON r.key=o.revision WHERE input=? ORDER BY ordinal",
          {file});
    }
    auto hash = sha(raw);
    auto retained = out / "metadata" / hash;
    if (!fs::exists(retained))
      save(retained, raw);
    std::string kind = name == "catalog.json"             ? "card"
                       : name == "components.json"        ? "component"
                       : name == "providers/library.json" ? "class"
                                                          : "binding";
    J rows, doc;
    std::string issue;
    try {
      doc = parse(raw);
      if (kind == "binding" && doc.isObject())
        rows = doc["providers"];
      else if (kind == "class" && doc.isObject())
        rows = doc["classes"];
      else if (kind == "card" && doc.isObject() && doc["entries"].isArray()) {
        kind = "template-package";
        rows = doc["entries"];
      } else
        rows = doc;
      if (!rows.isArray())
        issue = "Unrecognized metadata structure";
    } catch (const std::exception &e) {
      issue = e.what();
    }
    auto count = issue.empty() ? rows.size() : 0;
    std::string version = "unversioned legacy metadata";
    if (doc.isObject())
      for (const auto *field : {"schema", "schema_version"})
        if (doc.isMember(field)) {
          version = dump(doc[field], false);
          break;
        }
    db.insert("INSERT INTO input VALUES(?,?,?,?,?,?,?)",
              {file, dir, hash, kind, std::to_string(count), issue, version});
    check(cached.empty() || observations.size() == count,
          "frozen row count mismatch");
    for (unsigned i = 0; i < count; ++i) {
      std::string header;
      if (!cached.empty()) {
        const auto &obs = observations[i];
        check(obs[0] == std::to_string(i) && obs[1] == sha(dump(rows[i])),
              "frozen row identity mismatch");
        header = obs[2];
        if (immutable)
          check(header == implementation(dir, rows[i], kind),
                "immutable generation header changed");
        else
          ++frozen_headers;
      } else
        header = implementation(dir, rows[i], kind);
      record(file.string(), i, dir, kind, rows[i], header);
    }
  }
};
J count_by(Database &db, const std::string &sql) {
  J counts = object();
  for (const auto &row : db.query(sql))
    counts[row[0]] = Json::UInt64(std::stoull(row[1]));
  return counts;
}
} // namespace
int main(int argc, char **argv) try {
  if (argc == 6 && std::string(argv[1]) == "--rank-supplied") {
    auto report = rank_supplied_usage(argv[2], argv[3], argv[4], argv[5]);
    auto out = fresh_run("v5-supplied-priority") / "priority.json";
    write(out, report);
    std::cout << out.string() << '\n' << digest(out) << '\n';
    return 0;
  }
  if (argc == 4 && std::string(argv[1]) == "--replay-usage") {
    auto report = replay_binding_usage(fs::canonical(argv[2]), argv[3]);
    auto out = fresh_run("v5-replay-usage") / "usage.json";
    write(out, report);
    std::cout << out.string() << '\n' << digest(out) << '\n';
    return 0;
  }
  if (argc == 4 && std::string(argv[1]) == "--binding-identity-fixture") {
    auto out = binding_identity_fixture(fs::canonical(argv[2]), argv[3]);
    std::cout << out.string() << '\n' << digest(out) << '\n';
    return 0;
  }
  if (argc == 4 && std::string(argv[1]) == "--qualify-writer") {
    auto out = qualify_writer(fs::canonical(argv[2]), argv[3]);
    std::cout << out.string() << '\n' << digest(out) << '\n';
    return 0;
  }
  const std::map<std::string, decltype(&qualify_uri_reader)> file_trials{
      {"--qualify-tsv-writer", qualify_tsv_writer},
      {"--qualify-recursive-files", qualify_recursive_files},
      {"--qualify-parent-paths", qualify_parent_paths},
      {"--qualify-uri-reader", qualify_uri_reader},
      {"--qualify-archive-names", qualify_archive_names},
      {"--qualify-zip-records", qualify_zip_records},
      {"--qualify-tree-records", qualify_tree_records},
      {"--qualify-json-records", qualify_json_records},
      {"--qualify-csv-records", qualify_csv_records},
      {"--qualify-sqlite-records", qualify_sqlite_records},
      {"--qualify-xml-records", qualify_xml_records},
      {"--qualify-filter-records", qualify_filter_records},
      {"--qualify-report-fields", qualify_report_fields},
      {"--qualify-group-rows", qualify_group_rows},
      {"--qualify-column", qualify_column},
      {"--qualify-tab-records", qualify_tab_records},
      {"--qualify-select-group-row", qualify_select_group_row},
      {"--qualify-aggregate-groups", qualify_aggregate_groups},
      {"--qualify-annotate-group-measure", qualify_annotate_group_measure},
      {"--qualify-derive-integer", qualify_derive_integer},
      {"--qualify-order-records", qualify_order_records},
      {"--qualify-rank-records", qualify_rank_records},
      {"--qualify-previous-field", qualify_previous_field},
      {"--qualify-filter-extension", qualify_filter_extension},
      {"--qualify-json-writer", qualify_json_writer}};
  if ((argc == 4 || argc == 7) && file_trials.contains(argv[1])) {
    auto qualify = file_trials.at(argv[1]);
    const bool frozen = argc == 7;
    check(!frozen || (std::string(argv[4]) == "--source-registry" &&
                      std::string(argv[6]).size() == 64),
          "expected --source-registry FILE SHA256");
    auto out = qualify(fs::canonical(argv[2]), argv[3],
                       frozen ? Path(argv[5]) : v5_root() / "sources.json",
                       frozen ? argv[6] : "");
    std::cout << out.string() << '\n' << digest(out) << '\n';
    return 0;
  }
  check(
      argc == 3 || (argc == 6 && std::string(argv[3]) == "--frozen"),
      "usage: v5-inventory STAGED_PACKAGE EXPECTED_MANIFEST_SHA256 [--frozen "
      "SNAPSHOT LEDGER_SHA256], --replay-usage SNAPSHOT LEDGER_SHA256, "
      "--rank-supplied AUDIT AUDIT_SHA256 SNAPSHOT LEDGER_SHA256 or "
      "--binding-identity-fixture, --qualify-writer, --qualify-tsv-writer, "
      "--qualify-recursive-files "
      "--qualify-parent-paths, --qualify-archive-names, --qualify-zip-records, "
      "--qualify-tree-records, --qualify-json-records, --qualify-csv-records, "
      "--qualify-sqlite-records, --qualify-xml-records, "
      "--qualify-filter-extension, "
      "--qualify-json-writer, --qualify-filter-records, --qualify-report-fields, "
      "--qualify-group-rows, --qualify-column, --qualify-select-group-row, "
      "--qualify-tab-records, --qualify-aggregate-groups, --qualify-annotate-group-measure "
      "--qualify-derive-integer, --qualify-order-records, --qualify-rank-records, "
      "--qualify-previous-field "
      "or --qualify-uri-reader STAGED_PACKAGE EXPECTED_MANIFEST_SHA256");
  auto pack = fs::canonical(argv[1]), base = verified_base();
  auto ids = verify(pack, argv[2]);
  std::unique_ptr<Frozen> prior;
  if (argc == 6)
    prior = std::make_unique<Frozen>(fs::canonical(argv[4]), argv[5]);
  auto dir = fresh_run("v5-migration"), out = dir / ".snapshot";
  fs::create_directory(out);
  auto dirs = prior ? prior->locations_for(pack / "bundle", base) : locations();
  check(dirs.contains(pack / "bundle") && dirs.contains(base),
        "missing selected package or active generation in inventory");
  J summary;
  {
    Database db(out / "ledger.sqlite");
    db.script(R"sql(PRAGMA foreign_keys=ON; BEGIN;
      CREATE TABLE generation(path TEXT PRIMARY KEY,role TEXT,legacy_manifest_sha256 TEXT);
      CREATE TABLE input(path TEXT PRIMARY KEY,generation TEXT REFERENCES generation,bytes_sha256 TEXT,kind TEXT,entries INTEGER,issue TEXT,legacy_version TEXT);
      CREATE TABLE revision(key TEXT PRIMARY KEY,kind TEXT,id TEXT,brick TEXT,effect TEXT,inport TEXT,outport TEXT,row_sha256 TEXT,header_sha256 TEXT,legacy_row TEXT);
      CREATE TABLE occurrence(input TEXT REFERENCES input,ordinal INTEGER,revision TEXT REFERENCES revision,disposition TEXT,reason TEXT,contract_hash TEXT,new_contract_version TEXT,claim_status TEXT,qualification TEXT,activated INTEGER,PRIMARY KEY(input,ordinal));
      CREATE TABLE port_assessment(contract_hash TEXT,direction TEXT,role_status TEXT,unit_status TEXT,cardinality_status TEXT,record_sha256 TEXT,PRIMARY KEY(contract_hash,direction));
      CREATE INDEX by_version ON occurrence(revision);
      CREATE INDEX by_binding ON revision(kind,id,brick,effect,inport,outport);
      CREATE VIEW inventory AS SELECT g.role,i.generation,i.path,i.bytes_sha256 AS legacy_document_sha256,i.legacy_version,o.*,r.kind,r.id,r.brick,r.effect,r.inport,r.outport,r.header_sha256,
        CASE WHEN o.contract_hash='' THEN 'pending' ELSE 'classified in staged contract' END AS classification
        FROM occurrence o JOIN input i ON i.path=o.input JOIN generation g ON g.path=i.generation JOIN revision r ON r.key=o.revision;
      CREATE VIEW output_assessment AS SELECT i.generation,i.role,i.id,i.revision,i.contract_hash,p.record_sha256,
          COALESCE(p.role_status,'pending') AS role_status,COALESCE(p.unit_status,'pending') AS unit_status,COALESCE(p.cardinality_status,'pending') AS cardinality_status
          FROM inventory i LEFT JOIN port_assessment p ON p.contract_hash=i.contract_hash AND p.direction='output' WHERE i.kind='binding';
    )sql");
    if (load(pack / "manifest.json")["interfaces"].isMember("semantic_ports"))
      for (const auto &p : load(pack / "semantic-ports.json"))
        db.insert("INSERT INTO port_assessment VALUES(?,?,?,?,?,?)",
                  {p["contract_hash"].asString(), p["direction"].asString(),
                   p["role_status"].asString(), p["unit_status"].asString(),
                   p["cardinality_status"].asString(), sha(dump(p))});
    Snapshot snap{db, base, pack, out, admitted(pack), prior.get()};
    for (const auto &[d, files] : dirs) {
      auto role = d == base              ? "active legacy"
                  : d == pack / "bundle" ? "reviewed stage input"
                  : under(d, main_repo)  ? "sibling source metadata"
                  : under(d, volume / "unllm/boilerplate")
                      ? "installed legacy"
                      : "historical or experimental";
      auto manifest = d / "SHA256SUMS";
      std::string manifest_pin;
      if (prior && d != base && d != pack / "bundle") {
        auto receipt = prior->db.query(
            "SELECT legacy_manifest_sha256 FROM generation WHERE path=?", {d});
        check(receipt.size() == 1, "missing frozen generation identity");
        manifest_pin = receipt[0][0];
      } else if (fs::is_regular_file(manifest))
        manifest_pin = digest(manifest);
      db.insert("INSERT INTO generation VALUES(?,?,?)",
                {d, role, manifest_pin});
      for (const auto &name : files)
        snap.inventory_file(d, name);
    }
    check(db.query("SELECT i.path FROM input i WHERE entries!=(SELECT COUNT(*) "
                   "FROM occurrence o WHERE o.input=i.path)")
              .empty(),
          "unaccounted input rows");
    check(db.query("PRAGMA foreign_key_check").empty(),
          "unresolved ledger references");
    for (const auto &row : db.query("SELECT DISTINCT bytes_sha256 FROM input"))
      check(digest(out / "metadata" / row[0]) == row[0],
            "legacy document round trip failed");
    check(db.query("SELECT COUNT(*) FROM inventory WHERE role='reviewed stage "
                   "input' AND contract_hash!=''")[0][0] ==
              std::to_string(ids.size()),
          "incomplete staged qualifications");
    db.script("COMMIT");
    summary = object(
        {{"schema", "unllm.migration-inventory/1"},
         {"package", pack.string()},
         {"manifest_sha256", argv[2]},
         {"active", base.string()},
         {"active_manifest_sha256", base_hash},
         {"locations", Json::UInt64(dirs.size())},
         {"active_occurrences",
          count_by(db, "SELECT kind,COUNT(*) FROM inventory WHERE role='active "
                       "legacy' GROUP BY kind")},
         {"active_binding_disposition",
          count_by(
              db,
              "SELECT disposition,COUNT(*) FROM inventory WHERE role='active "
              "legacy' AND kind='binding' GROUP BY disposition")},
         {"active_output_unit_status",
          count_by(db, "SELECT unit_status,COUNT(*) FROM output_assessment "
                       "WHERE role='active legacy' GROUP BY unit_status")},
         {"occurrences",
          count_by(db, "SELECT kind,COUNT(*) FROM inventory GROUP BY kind")},
         {"distinct_metadata_and_header_versions",
          count_by(db, "SELECT kind,COUNT(*) FROM revision GROUP BY kind")},
         {"input_formats",
          count_by(db, "SELECT kind,COUNT(*) FROM input GROUP BY kind")},
         {"unrecognized_documents",
          std::stoi(
              db.query("SELECT COUNT(*) FROM input WHERE issue!=''")[0][0])},
         {"stage_contracts", ids},
         {"activated", 0},
         {"scope",
          "Unpacked catalog.json, components.json, providers/library.json and "
          "providers/contracts.json "
          "under the corpus unllm directory and sibling "
          "experiments/boilerplate; sealed V5 evaluation directories and "
          "symlinked trees are excluded. Raw "
          "documents frozen by hash. Compressed archives and nonstandard "
          "formats require separate review. Revision identity covers the full "
          "row and resolved header where available. Identical bytes in other "
          "directories carry no qualification automatically."}});
    if (prior)
      summary["frozen_inputs"] = object(
          {{"snapshot", prior->directory.string()},
           {"ledger_sha256", prior->pin},
           {"historical_row_observations_reused",
            Json::UInt64(snap.frozen_headers)},
           {"scope", "Frozen catalog locations plus the named stage. The "
                     "active and selected stage headers were rechecked. All "
                     "qualification dispositions were recomputed."}});
  }
  summary["ledger_sha256"] = digest(out / "ledger.sqlite");
  write(out / "summary.json", summary);
  fs::rename(out, dir / "snapshot");
  std::cout << (dir / "snapshot/summary.json").string() << '\n'
            << dump(summary);
  return 0;
} catch (const std::exception &e) {
  std::cerr << e.what() << '\n';
  return 1;
}
