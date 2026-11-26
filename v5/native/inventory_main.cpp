#include "queue.hpp"
#include <iostream>
#include <sstream>

// Read only unpacked catalog metadata. A discovered occurrence has no authority
// to activate a provider, including when its descriptive row matches a trial.
using namespace v5;
namespace {
std::map<Path, std::set<std::string>> locations() {
  auto paths = run({"rg", "--files", "--hidden", "--no-ignore", "--null", "-g",
                    "catalog.json", "-g", "components.json", "-g",
                    "**/providers/contracts.json", "-g",
                    "**/providers/library.json", "-g", "!**/.git/**",
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
  return entries;
}
struct Snapshot {
  Database &db;
  const Path base, pack, out;
  const std::vector<Qualified> qualified;
  const J evidence = load(pack / "evidence/integration.json");
  const J registry = load(pack / "sources.json");
  void record(const std::string &file, unsigned ordinal, const Path &dir,
              const std::string &kind, const J &row) {
    const auto field = [&](const std::string &name) {
      return row.isObject() && row[name].isString() ? row[name].asString()
                                                    : std::string{};
    };
    auto id = kind == "class" ? field("brick") : field("id"),
         header = implementation(dir, row, kind), brick = field("brick");
    auto bytes = dump(row),
         version = sha(dump(array({kind, sha(bytes), header})));
    db.insert("INSERT OR IGNORE INTO revision VALUES(?,?,?,?,?,?,?,?,?,?)",
              {version, kind, id, brick, field("effect"), field("from"),
               field("to"), sha(bytes), header, bytes});
    J claims = object(), proof = object();
    std::string status = "legacy-only pending review", contract,
                reason = "Legacy occurrence has not passed V5 qualification";
    if (id.empty()) {
      status = "quarantined";
      reason = "Absent or unsupported identity";
    } else if ((kind == "binding" || kind == "class") && header.empty()) {
      status = "quarantined";
      reason = "Unresolved implementation header";
    } else if (kind == "class") {
      status = "class inventory pending qualification";
      reason = "Library descriptor preserves method identities and value "
               "types; object lifetime and independent qualification pending";
    } else if (kind == "binding") {
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
            if (evidence[c["capability_id"].asString()]["baseline"]["result"] !=
                "passed")
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
               contract.empty() ? "" : "unllm.brick-contract/1",
               contract.empty() ? "unassessed" : dump(claims, false),
               contract.empty() ? "" : dump(proof, false), "0"});
  }
  void inventory_file(const Path &dir, const std::string &name) {
    auto file = dir / name;
    check(fs::is_regular_file(file),
          "discovered metadata moved during snapshot: " + file.string());
    auto raw = bytes(file), hash = sha(raw);
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
    for (unsigned i = 0; i < count; ++i)
      record(file.string(), i, dir, kind, rows[i]);
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
  check(argc == 3,
        "usage: v5-inventory STAGED_PACKAGE EXPECTED_MANIFEST_SHA256");
  auto pack = fs::canonical(argv[1]), base = verified_base();
  auto ids = verify(pack, argv[2]);
  auto dir = fresh_run("v5-migration"), out = dir / ".snapshot";
  fs::create_directory(out);
  auto dirs = locations();
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
      CREATE INDEX by_version ON occurrence(revision);
      CREATE INDEX by_binding ON revision(kind,id,brick,effect,inport,outport);
      CREATE VIEW inventory AS SELECT g.role,i.generation,i.path,i.bytes_sha256 AS legacy_document_sha256,i.legacy_version,o.*,r.kind,r.id,r.brick,r.effect,r.inport,r.outport,r.header_sha256,
        CASE WHEN o.contract_hash='' THEN 'pending' ELSE 'classified in staged contract' END AS classification
        FROM occurrence o JOIN input i ON i.path=o.input JOIN generation g ON g.path=i.generation JOIN revision r ON r.key=o.revision;
    )sql");
    Snapshot snap{db, base, pack, out, admitted(pack)};
    for (const auto &[d, files] : dirs) {
      auto role = d == base              ? "active legacy"
                  : d == pack / "bundle" ? "reviewed stage input"
                  : under(d, main_repo)  ? "sibling source metadata"
                  : under(d, volume / "unllm/boilerplate")
                      ? "installed legacy"
                      : "historical or experimental";
      auto manifest = d / "SHA256SUMS";
      db.insert(
          "INSERT INTO generation VALUES(?,?,?)",
          {d, role, fs::is_regular_file(manifest) ? digest(manifest) : ""});
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
          "experiments/boilerplate; symlinked trees are not traversed. Raw "
          "documents frozen by hash. Compressed archives and nonstandard "
          "formats require separate review. Revision identity covers the full "
          "row and resolved header where available. Identical bytes in other "
          "directories carry no qualification automatically."}});
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
