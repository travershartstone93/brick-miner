#include "row-trial.hpp"
#include <iostream>
#include <map>

using namespace miner;
using namespace row_replay;
namespace {
const std::string sqlite = "application/vnd.sqlite3";
const std::string plain = "text/plain";
const std::string tsv = "text/tab-separated-values";
const std::string empty_stop = "Expected between one and 32 ordered outcomes";
const Path sqlite_cli = "/usr/bin/sqlite3";
const Path response = "run/calls/interpret/response.json";
const Strings splits{"ordinary", "boundary", "empty", "transfer", "sparse"};
const Strings requests{"H022", "H023", "H025", "H026", "H028"};
const std::string fixture = R"(
CREATE TABLE scans(seq INTEGER NOT NULL UNIQUE, parcel TEXT NOT NULL,
                   depot TEXT NOT NULL, state TEXT NOT NULL, minute INTEGER NOT NULL);
CREATE TEMP TABLE data AS SELECT * FROM scans WHERE 0;
INSERT INTO data VALUES
  (55,'Z','C','delivered',100),
  (65,'A','South','pickup',88),
  (12,'A','West','delivered',45),
  (94,'P','D','out',150),
  (22,'Z','B','pickup',10),
  (78,'Z','D','out',140),
  (42,'A','East','delivered',75),
  (10,'Z','A','out',0),
  (83,'M','','delivered',60),
  (46,'P','D','pickup',80),
  (100,'R','Single','delivered',300),
  (32,'Z','C','delivered',50),
  (61,'D','West','pickup',60),
  (26,'A','','pickup',45),
  (70,'P','D','out',120),
  (92,'A','South','delivered',130),
  (31,'A','','out',55),
  (74,'D','West','out',90),
  (49,'M','M','pickup',30),
  (58,'Z','D','out',130),
  (89,'N','North','pickup',200),
  (66,'O','O','delivered',100),
  (84,'O','O','pickup',100),
  (85,'O','O','delivered',100);
)";

const std::map<std::string, std::string> queries{{"H022", R"(
SELECT e.parcel,e.seq FROM events e
WHERE e.state='delivered'
  AND COALESCE((SELECT p.state FROM events p
                WHERE p.parcel=e.parcel AND p.ord<e.ord
                ORDER BY p.ord DESC LIMIT 1),'') <> 'delivered'
ORDER BY e.ord;
)"},
                                                 {"H023", R"(
SELECT p.parcel,d.minute-p.minute
FROM events p JOIN events d ON d.parcel=p.parcel AND d.ord=(
    SELECT MIN(x.ord) FROM events x
    WHERE x.parcel=p.parcel AND x.state='delivered' AND x.ord>p.ord)
WHERE p.state='pickup' AND p.ord=(
    SELECT MIN(y.ord) FROM events y
    WHERE y.parcel=p.parcel AND y.state='pickup')
ORDER BY p.parcel COLLATE BINARY;
)"},
                                                 {"H025", R"(
SELECT e.parcel,e.depot FROM events e
WHERE e.depot <> (SELECT p.depot FROM events p
                  WHERE p.parcel=e.parcel AND p.ord<e.ord
                  ORDER BY p.ord DESC LIMIT 1)
ORDER BY e.ord;
)"},
                                                 {"H026", R"(
SELECT e.parcel FROM events e
WHERE e.state<>'delivered' AND EXISTS (
    SELECT 1 FROM events p WHERE p.parcel=e.parcel
    AND p.state='delivered' AND p.ord<e.ord)
GROUP BY e.parcel
ORDER BY MIN(e.ord);
)"},
                                                 {"H028", R"(
, legs AS (
  SELECT e.parcel,e.seq,e.minute-(SELECT p.minute FROM events p
     WHERE p.parcel=e.parcel AND p.ord<e.ord
     ORDER BY p.ord DESC LIMIT 1) AS elapsed
  FROM events e
)
SELECT parcel,elapsed FROM legs WHERE elapsed>=30
ORDER BY elapsed DESC,seq ASC;
)"}};

const std::map<std::string, std::string> expected{
    {"H022", "A\t12\nZ\t32\nA\t42\nO\t66\nM\t83\nO\t85\nA\t92\nR\t100\n"},
    {"H023", "A\t30\nM\t30\nO\t0\nZ\t40\n"},
    {"H025", "Z\tB\nA\t\nZ\tC\nA\tEast\nZ\tD\nA\tSouth\nM\t\n"},
    {"H026", "A\nZ\nO\n"},
    {"H028", "Z\t50\nA\t42\nZ\t40\nP\t40\nZ\t30\nD\t30\nM\t30\nP\t30\n"}};

std::string read_sql(const Path &db, const std::string &query) {
  return run({sqlite_cli, "-readonly", "-batch", "-bail", "-noheader",
              "-separator", "\t", db, query});
}

std::string oracle_sql(const std::string &id, const std::string &order) {
  return "WITH events AS (SELECT seq,parcel,depot,state,minute,"
         "row_number() OVER (ORDER BY " +
         order + ") AS ord FROM scans)\n" + queries.at(id);
}

Path checked_input(const Path &run, const std::string &split) {
  return run /
         (split == "transfer" || split == "sparse" ? "held"
                                                   : "run/check-initial") /
         split / "input/scans.db";
}
} // namespace

int main() try {
  const auto prior = runs / "v5-unordered-finance-replay-XXvu2pML/report.json";
  const auto prior_pin = hash(prior);
  check(prior_pin ==
            "20ab431f685db586b3fd07fcfb1d1a7e5bc0dba8727a14642aa7340ba5d3b252",
        "changed frequency report");
  const auto ranking = load(prior);
  auto ids = array();
  for (const auto &id : requests)
    ids.append(id);
  check(ranking["family_frequency"][3]["name"] == "parcel dispatch" &&
            ranking["family_frequency"][3]["requests"] == ids,
        "changed parcel-dispatch cohort");
  const auto recipe = root / "shared/request-recipes.json";
  const auto recipe_pin = hash(recipe);
  check(recipe_pin ==
            "768676cbc34ef7db9d869520bd3d26d86123f49a3c9803901eb5653de3a6f49b",
        "changed supplied recipes");
  const auto recipes = load(recipe);

  const auto parent = runs / "v5-queue-XXH5XMi9/stage/package";
  const auto writer_stage = runs / "v5-tsv-writer-XXAf0x0y/stage";
  const auto base = parent / "bundle", with_tsv = writer_stage / "bundle";
  const auto base_pin = hash(parent / "manifest.json");
  const auto writer_pin = hash(writer_stage / "manifest.json");
  check(base_pin == "d15aee96917ff440f58f76270e3ca48575268dcfcf2bce951805c865aa"
                    "e3af63" &&
            writer_pin == "0047b47009df08f7ee228729eb46b4438ec9d59eadd7c44647ec"
                          "6761eeabb994",
        "changed catalog stages");
  verify_bundle(
      base, "436db92629463daee559de2cbbc1c81519e12306d5bf01cc6d2883f3140e779c");
  verify_bundle(
      with_tsv,
      "b2f8a40d44b1589779fc82fcfa23995df5cb2b2432167e9383f6f22e9c828d54");
  const auto writer =
      exact_row(with_tsv, "write_tsv_lines", "strings", "out:" + tsv);
  check(remove_binding(read_tree(with_tsv), writer).at("SHA256SUMS") ==
            bytes(base / "SHA256SUMS"),
        "catalog comparison differs outside the TSV writer");
  const auto source_file = base / "qualifications/shared/inputs/source.json";
  const auto source_pin = hash(source_file);
  const auto source = load(source_file);
  check(source_pin == "cfbd58d26ebaab781ff61c4859b5fa80b62a8dae1c113715b498ba5c"
                      "4af9bdb1" &&
            source["kernel_sha256"] ==
                hash(base / "runtime/duckdb/lib/libbrick_shared_inputs.so") &&
            source["runtime_sha256"]["libsqlite3.so.0"] ==
                hash(base / "runtime/duckdb/lib/libsqlite3.so.0"),
        "changed SQLite kernel or lineage");
  const auto active = fs::canonical(volume / "unllm/boilerplate/active");
  const auto active_pin = hash(active / "SHA256SUMS");
  check(active_pin ==
            "e4564e3079f0b32b3379a411b170ce9af61ec80ab7938b6a974e724abd538328",
        "changed published catalog");

  const auto work = fresh_run("v5-unordered-dispatch-replay");
  std::cout << work.string() << std::endl;
  auto report = object(
      {{"active_path", active.string()},
       {"active_sha256", active_pin},
       {"frequency_report_sha256", prior_pin},
       {"recipes_sha256", recipe_pin},
       {"parent_manifest_sha256", base_pin},
       {"writer_manifest_sha256", writer_pin},
       {"reader", exact_row(base, "sqlite_records", "in:" + sqlite, "rows")},
       {"reader_source_sha256", source_pin},
       {"sqlite_cli_sha256", hash(sqlite_cli)},
       {"writer", writer},
       {"tools", tools()}});
  report["tools"]["unllm_logic_worker"] =
      hash(main_repo / "build/native/unllm_logic_worker");
  for (const auto &name : {"sorted", "shuffled"}) {
    const std::string order = std::string(name) == "sorted" ? "seq" : "rowid";
    const auto sql = fixture +
                     "INSERT INTO scans SELECT * FROM data ORDER BY " + order +
                     ";\n";
    const auto db = work / (std::string(name) + ".db");
    save(work / (std::string(name) + ".sql"), sql);
    run({sqlite_cli, "-batch", "-bail", db, sql});
    report["databases"][name] =
        object({{"sha256", hash(db)}, {"fixture_sql_sha256", sha(sql)}});
  }
  const auto sorted = work / "sorted.db", shuffled = work / "shuffled.db";
  const std::string order_check = "SELECT seq FROM scans ORDER BY ";
  const auto sorted_ids = read_sql(sorted, order_check + "seq");
  check(read_sql(shuffled, order_check + "seq") == sorted_ids &&
            read_sql(sorted, order_check + "rowid") == sorted_ids &&
            read_sql(shuffled, order_check + "rowid") != sorted_ids &&
            report["databases"]["sorted"]["sha256"] !=
                report["databases"]["shuffled"]["sha256"],
        "fixture does not distinguish scan order from insertion order");

  const auto historical = runs / "shared-review-5ui86km8/transfer";
  const auto archive = runs / "english-audit-bcsgf8pi/replay-staged";
  std::vector<Path> programs;
  for (const auto &id : requests) {
    const auto dir = archive / id;
    const auto &record = ranking["archive"][id];
    const auto saved_pin = hash(dir / response);
    const auto saved = load(dir / response);
    const auto &role = saved["roles"][0];
    const auto output = role["output_type"].asString();
    const auto mode = (id == "H023" || id == "H025") ? tsv : plain;
    const auto steps = operations(recipes[id][0]);
    const auto &reader = steps[0]["parameters"];
    check(saved_pin == record["reply_sha256"].asString() &&
              hash(record["reply_source"].asString()) == saved_pin &&
              hash(dir / "request.txt") ==
                  record["request_sha256"].asString() &&
              saved["input_type"] == sqlite && saved["roles"].size() == 1 &&
              output == mode && role["requirement"]["operations"] == array() &&
              steps[0]["outcome"] == "sqlite_records" &&
              reader == object({{"fields", "parcel,depot,state,minute,seq"},
                                {"order_by", "seq:asc"},
                                {"table", "scans"}}),
          "changed saved or supplied dispatch context for " + id);
    auto &entry = report["dispatch"][id];
    entry["archive"] = record;
    const auto archived =
        plan_probe(base, work / "plans" / id / "archived", saved);
    check(archived["gaps"]["r"] == empty_stop && archived["routes"].empty(),
          "changed archived operation stop for " + id + ": " + dump(archived));
    entry["plans"]["archived"] = archived;

    auto supplied = saved;
    auto &requirement = supplied["roles"][0]["requirement"];
    requirement["uncovered"] = array();
    requirement["operations"] = steps;
    if (output == tsv) {
      const auto gap =
          plan_probe(base, work / "plans" / id / "without-writer", supplied);
      check(gap["routes"].size() == 1 && gap["validation"].empty() &&
                gap["routes"][0]["ports"] ==
                    array({array({"strings", "out:" + tsv})}),
            "changed dispatch sink gap for " + id + ": " + dump(gap));
      entry["plans"]["without-writer"] = gap;
    }

    const auto &bundle = output == tsv ? with_tsv : base;
    const auto sink = output == tsv ? "write_tsv_lines" : "write_lines";
    const auto logical = oracle_sql(id, "seq");
    const auto physical = oracle_sql(id, "rowid");
    entry["oracle"]["seq"] = logical;
    entry["oracle"]["rowid"] = physical;
    const auto intended = read_sql(shuffled, logical);
    const auto misplaced = read_sql(shuffled, physical);
    check(intended == expected.at(id) &&
              read_sql(sorted, logical) == intended &&
              read_sql(sorted, physical) == intended && misplaced != intended,
          "dispatch witness does not distinguish " + id);
    auto rowid_spec = supplied;
    rowid_spec["roles"][0]["requirement"]["operations"][0]["parameters"]
              ["order_by"] = "rowid:asc";

    const auto fixtures = historical / id / "fixtures";
    for (const auto &arm : {"seq", "rowid"}) {
      const bool follows_seq = std::string(arm) == "seq";
      const auto file = work / "specs" / arm / (id + ".json");
      write(file, follows_seq ? supplied : rowid_spec);
      const auto result = trial(work, arm, id, bundle, file);
      const auto trial_dir = work / arm / id;
      const auto exe = trial_dir / "run/project/converter";
      entry["runs"][arm] = result;
      write(work / "progress.json", report);
      const auto &r = result["roles"]["0"];
      check(fs::is_regular_file(exe) && complete(r) &&
                r["steps"][0]["provider"] == "op_0_sqlite_records" &&
                r["steps"][r["steps"].size() - 1]["provider"] == sink,
            "incomplete dispatch construction for " + id + " " + arm);
      programs.push_back(exe);
      for (const auto &split : splits) {
        const auto saved_db = fixtures / split / "input/scans.db";
        const auto regenerated = checked_input(trial_dir, split);
        const auto dump_sql = read_sql(saved_db, ".dump");
        check(dump_sql == read_sql(regenerated, ".dump"),
              "SQLite schema or rows differ for " + id + " " + split);
        const auto correct = read_sql(saved_db, logical);
        const auto predicted =
            follows_seq ? correct : read_sql(saved_db, physical);
        const bool should_pass = predicted == correct;
        check(correct == bytes(fixtures / split / "output/result-1.txt") &&
                  result["scores"][split] == should_pass &&
                  result["outputs"][split][0] == predicted,
              "saved dispatch fixture mismatch for " + id + " " + arm + " " +
                  split);
        auto &e = entry["fixtures"][split];
        e["archived_sha256"] = hash(saved_db);
        e["logical_dump_sha256"] = sha(dump_sql);
        e["generated_sha256"][arm] = hash(regenerated);
        e["rowid_matches_seq"] = read_sql(saved_db, physical) == correct;
        e["seq_matches_rowid_values"] =
            read_sql(saved_db,
                     "SELECT COUNT(*) FROM scans WHERE seq<>rowid;") == "0\n";
        e["scans_schema"] =
            read_sql(saved_db, "SELECT sql FROM sqlite_schema WHERE "
                               "type='table' AND name='scans';");
        if (follows_seq) {
          const auto exact =
              path_output(trial_dir / "exact" / split, saved_db, exe, bundle);
          check(exact == correct, "exact SQLite file replay failed for " + id);
          e["exact_archived_output"] = exact;
        }
      }

      for (const auto &name : {"sorted", "shuffled"}) {
        const auto db = work / (std::string(name) + ".db");
        const auto answer = !follows_seq && std::string(name) == "shuffled"
                                ? misplaced
                                : intended;
        const auto actual =
            path_output(work / "witness" / id / arm / name, db, exe, bundle);
        check(actual == answer, "emitted dispatch witness mismatch for " + id +
                                    " " + arm + " " + name + ": " + actual);
        entry["witness"][arm][name] =
            object({{"output", actual}, {"expected", answer}});
      }
      write(work / "progress.json", report);
    }
  }
  for (const auto &name : {"sorted", "shuffled"})
    check(hash(work / (std::string(name) + ".db")) ==
              report["databases"][name]["sha256"].asString(),
          "dispatch witness was modified");
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
