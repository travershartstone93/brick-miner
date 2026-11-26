#include "row-trial.hpp"
#include <iostream>

using namespace miner;
using namespace row_replay;
namespace {
const std::string id = "H030";
const std::string sqlite = "application/vnd.sqlite3";
const std::string tsv = "text/tab-separated-values";
const Path sqlite_cli = "/usr/bin/sqlite3";
const Path response = "run/calls/interpret/response.json";
const Strings splits{"ordinary", "boundary", "empty", "transfer", "sparse"};
const std::string fixture_sql = R"(
CREATE TABLE scans(seq INTEGER NOT NULL UNIQUE, parcel TEXT NOT NULL,
                   depot TEXT NOT NULL, state TEXT NOT NULL, minute INTEGER NOT NULL);
INSERT INTO scans VALUES
  (50,'P','East','out',50),
  (10,'P','East','out',10),
  (40,'Q','East','pickup',40),
  (30,'D','Mixed','out',30),
  (60,'D','Mixed','delivered',60),
  (20,'L','Idle','pickup',20),
  (80,'R','West','out',80),
  (15,'Y','West','pickup',15),
  (70,'T','West','out',70),
  (5,'G','OnlyDelivered','delivered',5),
  (35,'S','south','out',35),
  (25,'T','South','delivered',25),
  (45,'T','South','out',45);
)";
const std::string count_sql = R"(
SELECT depot, count(*) FILTER (WHERE state COLLATE BINARY = 'out')
FROM scans
GROUP BY depot COLLATE BINARY
HAVING count(*) FILTER (WHERE state COLLATE BINARY = 'delivered') = 0
   AND count(*) FILTER (WHERE state COLLATE BINARY = 'out') > 0
ORDER BY depot COLLATE BINARY;
)";

std::string read_sql(const Path &db, const std::string &query) {
  return run({sqlite_cli, "-readonly", "-batch", "-noheader", "-separator",
              "\t", db, query});
}

J op(const std::string &effect, const J &parameters = object()) {
  return object({{"outcome", effect}, {"parameters", parameters}});
}

J scalar_column(const Path &dir, const Path &bundle, const Path &db,
                const std::string &column, std::vector<Path> &programs) {
  const auto spec = spec_for(
      sqlite, "application/json",
      object({{"operations",
               array({op("text_column",
                         object({{"column", column}, {"table", "scans"}}))})},
              {"uncovered", array()}}));
  const auto p = plan(bundle, spec, dir);
  const auto &role = p["providers"][0];
  const auto &steps = role["plan"]["steps"];
  check(p["providers"].size() == 1 && complete(role) && p["gaps"].empty() &&
            p["interpretation_errors"].empty() && steps.size() == 2 &&
            selects(object({{"steps", steps}}), "text_column") &&
            steps[1]["provider"] == "write_texts",
        "SQLite scalar column control did not plan");
  const auto exe = dir / "converter";
  build_emission(role, dir, exe, "g++-15");
  programs.push_back(exe);
  const auto actual = parse(path_output(dir / "witness", db, exe, bundle));
  check(actual.isArray() && actual.size() == 13,
        "missing scalar column values");
  Strings lines;
  for (const auto &value : actual)
    lines.push_back(value.asString());
  check(join(lines, "\n") + "\n" ==
            read_sql(db, "select " + column + " from scans order by rowid;"),
        "scalar column did not preserve rowid order");
  return object({{"plan_sha256", hash(dir / "components/emission.json")},
                 {"program_sha256", hash(exe)},
                 {"observed", actual}});
}

void append_count(J &steps, const std::string &measure) {
  steps.append(op("aggregate_groups", object({{"measure", measure},
                                              {"result_order", "group"},
                                              {"value_column", 0}})));
  steps.append(op("report_fields", object({{"fields", "0,1"}})));
}
} // namespace

int main() try {
  const auto parent = runs / "v5-queue-XXH5XMi9/stage/package";
  const auto parent_pin = hash(parent / "manifest.json");
  check(parent_pin ==
            "d15aee96917ff440f58f76270e3ca48575268dcfcf2bce951805c865aae3af63",
        "changed V5 parent");
  const auto base = parent / "bundle";
  verify_bundle(
      base, "436db92629463daee559de2cbbc1c81519e12306d5bf01cc6d2883f3140e779c");
  const auto writer_stage = runs / "v5-tsv-writer-XXAf0x0y/stage";
  const auto writer_pin = hash(writer_stage / "manifest.json");
  check(writer_pin ==
            "0047b47009df08f7ee228729eb46b4438ec9d59eadd7c44647ec6761eeabb994",
        "changed TSV evidence");
  const auto with_tsv = writer_stage / "bundle";
  verify_bundle(
      with_tsv,
      "b2f8a40d44b1589779fc82fcfa23995df5cb2b2432167e9383f6f22e9c828d54");
  const auto writer =
      exact_row(with_tsv, "write_tsv_lines", "strings", "out:" + tsv);
  check(remove_binding(read_tree(with_tsv), writer).at("SHA256SUMS") ==
            bytes(base / "SHA256SUMS"),
        "TSV control differs outside the writer row");

  const auto binding =
      exact_row(base, "sqlite_records", "in:" + sqlite, "rows");
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
  const auto cli_pin = hash(sqlite_cli);

  const auto active = fs::canonical(volume / "unllm/boilerplate/active");
  const auto active_pin = hash(active / "SHA256SUMS");
  check(active_pin ==
            "e4564e3079f0b32b3379a411b170ce9af61ec80ab7938b6a974e724abd538328",
        "changed published context");
  const auto ranking = runs / "ranked-gaps-5x2_hyp7/ranking.json";
  const auto ranking_pin = hash(ranking);
  check(ranking_pin ==
            "3d902a86f77c7159581a1721cf07c96ccd7b0147259ffdee6edce8f976119495",
        "changed archived ranking");
  const auto archive = runs / "english-audit-bcsgf8pi/replay-staged";
  const auto disconnected =
      load(ranking)["stops"]["no complete chain for the requested operations"];
  Strings cohort;
  for (const auto &r : disconnected)
    if (load(archive / r.asString() / response)["input_type"] == sqlite)
      cohort.push_back(r.asString());
  check(cohort == Strings{id}, "changed disconnected SQLite cohort");
  const auto original =
      volume / "unllm/experiments/boilerplate-B42-VrtYbHSf/fresh-staged";
  const auto saved_pin = hash(archive / id / response);
  check(saved_pin == hash(original / id / response), "changed original answer");
  const auto saved = load(archive / id / response);
  const auto role = saved["roles"][0];
  check(saved["roles"].size() == 1 && role["output_type"] == tsv &&
            role["requirement"]["operations"] ==
                array({op("text_column",
                          object({{"table", "scans"}, {"column", "depot"}})),
                       op("text_column",
                          object({{"table", "scans"}, {"column", "state"}}))}),
        "changed archived SQLite operations");

  const auto recipe = root / "shared/request-recipes.json";
  const auto recipe_pin = hash(recipe);
  check(recipe_pin ==
            "768676cbc34ef7db9d869520bd3d26d86123f49a3c9803901eb5653de3a6f49b",
        "changed supplied recipe");
  auto fixed = saved;
  auto &req = fixed["roles"][0]["requirement"];
  req["operations"] = operations(load(recipe)[id][0]);
  req["uncovered"] = array();
  const auto operations_fixed = req["operations"];
  const auto reader = operations_fixed[0];
  check(operations_fixed.size() == 8 && reader["outcome"] == "sqlite_records" &&
            reader["parameters"] ==
                object({{"table", "scans"},
                        {"fields", "parcel,depot,state,minute,seq"},
                        {"order_by", "seq:asc"}}) &&
            operations_fixed[5]["outcome"] == "group_rows",
        "changed correlated scans recipe");

  const auto work = fresh_run("v5-depot-count-replay");
  std::cout << work.string() << std::endl;
  auto report = object({{"request", id},
                        {"active_path", active.string()},
                        {"active_sha256", active_pin},
                        {"parent_manifest_sha256", parent_pin},
                        {"writer_manifest_sha256", writer_pin},
                        {"ranking_sha256", ranking_pin},
                        {"recipes_sha256", recipe_pin},
                        {"archived_sha256", saved_pin},
                        {"reader", binding},
                        {"reader_source_sha256", source_pin},
                        {"sqlite_cli_sha256", cli_pin},
                        {"tools", tools()}});
  const auto archived = plan_probe(base, work / "parameters/archived", saved);
  const auto previous = load(archive / id / "run/components/emission.json");
  const auto gap = archived["gaps"]["r"].asString();
  const auto saved_gap = previous["gaps"][role["id"].asString()].asString();
  const std::string prefix = "No provider chain for requested outcomes: ";
  check(gap.starts_with(prefix) && saved_gap.starts_with(prefix) &&
            parse(gap.substr(prefix.size())) ==
                role["requirement"]["operations"] &&
            parse(saved_gap.substr(prefix.size())) ==
                role["requirement"]["operations"] &&
            archived["validation"].empty() && archived["routes"].empty(),
        "archived SQLite chain stop changed");
  report["plans"]["archived"] = archived;
  report["incompatible_binding"] =
      exact_row(base, "text_column", "in:" + sqlite, "strings");
  auto reader_only = saved;
  reader_only["roles"][0]["requirement"]["operations"] = array({reader});
  const auto partial =
      plan_probe(base, work / "parameters/reader-only", reader_only);
  check(partial["validation"].empty() && partial["routes"].size() == 1 &&
            partial["routes"][0]["ports"] ==
                array({array({"rows", "out:" + tsv})}) &&
            partial["routes"][0]["uncovered"] ==
                role["requirement"]["uncovered"],
        "reader-only substitution changed other obligations");
  report["plans"]["reader-only"] = partial;

  const auto db = work / "witness.db";
  run({sqlite_cli, "-batch", db, fixture_sql});
  const auto db_pin = hash(db);
  report["witness_sql"] = fixture_sql;
  report["witness_database_sha256"] = db_pin;
  report["oracle_sql"] = count_sql;
  std::vector<Path> programs;
  for (const auto *col : {"depot", "state"})
    report["scalar"][col] =
        scalar_column(work / "scalar" / col, base, db, col, programs);

  const auto historical =
      runs / "shared-review-5ui86km8/transfer" / id / "fixtures";
  auto replay = [&](const std::string &arm, const Path &bundle, const J &spec) {
    const auto path = work / "specs" / (arm + ".json");
    write(path, spec);
    const auto result = trial(work, arm, id, bundle, path);
    if (!result["program_sha256"].isNull())
      programs.push_back(work / arm / id / "run/project/converter");
    report["runs"][arm] = result;
    write(work / "progress.json", report);
    return result;
  };
  const auto blocked = replay("without-writer", base, fixed);
  check(blocked["roles"].size() == 1 && blocked["verified"] == false &&
            ports(blocked["roles"]["0"]) ==
                array({array({"strings", "out:" + tsv})}),
        "unexpected TSV comparison gaps");
  const auto accepted = replay("fixed", with_tsv, fixed);
  check(accepted["roles"].size() == 1 && accepted["verified"] == true &&
            complete(accepted["roles"]["0"]) &&
            selects(accepted["roles"]["0"], "sqlite_records") &&
            selects(accepted["roles"]["0"], "write_tsv_lines"),
        "full H030 recipe failed");

  const auto fixed_dir = work / "fixed" / id;
  for (const auto &split : splits) {
    const auto current =
        fixed_dir /
        ((split == "sparse" || split == "transfer") ? "held"
                                                    : "run/check-initial") /
        split / "input/scans.db";
    const auto prior = historical / split;
    const auto archived_db = prior / "input/scans.db";
    const auto dump = read_sql(archived_db, ".dump");
    check(dump == read_sql(current, ".dump"),
          "SQLite fixture schema or rows differ");
    const auto expected = read_sql(archived_db, count_sql);
    check(accepted["outputs"][split][0] == expected &&
              expected == bytes(prior / "output/result-1.txt"),
          "archive output differs from SQLite oracle");
    const auto exact =
        path_output(fixed_dir / "exact" / split, archived_db,
                    fixed_dir / "run/project/converter", with_tsv);
    check(exact == expected, "exact archived database output differed");
    report["exact_inputs"][split] =
        object({{"historical_sha256", hash(archived_db)},
                {"generated_sha256", hash(current)},
                {"logical_dump_sha256", sha(dump)},
                {"output", exact}});
  }

  const auto is_out = op("filter_records", object({{"column", 2},
                                                   {"comparison", "eq"},
                                                   {"type", "text"},
                                                   {"value", "out"}}));
  auto early = fixed;
  auto early_ops = array({reader, is_out});
  for (unsigned j = 1; j < operations_fixed.size(); ++j)
    early_ops.append(operations_fixed[j]);
  early["roles"][0]["requirement"]["operations"] = early_ops;
  auto total = fixed;
  auto &total_ops = total["roles"][0]["requirement"]["operations"];
  total_ops.resize(6);
  append_count(total_ops, "count");
  auto distinct = fixed;
  auto &distinct_ops = distinct["roles"][0]["requirement"]["operations"];
  distinct_ops.resize(5);
  distinct_ops.append(is_out);
  distinct_ops.append(operations_fixed[5]);
  append_count(distinct_ops, "distinct");
  auto projection = fixed;
  projection["roles"][0]["requirement"]["operations"] =
      array({reader, op("report_fields", object({{"fields", "4,0,1,2"}}))});
  for (const auto &[arm, spec] :
       {std::pair{"early-out-filter", early},
        std::pair{"count-all-scans", total},
        std::pair{"distinct-parcels", distinct},
        std::pair{"projection-control", projection}}) {
    const auto result = replay(arm, with_tsv, spec);
    check(result["roles"].size() == 1 && complete(result["roles"]["0"]) &&
              !result["program_sha256"].isNull(),
          "SQLite comparison construction did not emit");
  }
  const auto correct = read_sql(db, count_sql);
  check(correct == "East\t2\nWest\t2\nsouth\t1\n",
        "unexpected fresh SQLite oracle result");
  const auto rows =
      read_sql(db, "select seq,parcel,depot,state from scans order by seq;");
  check(rows !=
            read_sql(
                db, "select seq,parcel,depot,state from scans order by rowid;"),
        "fresh witness does not distinguish sequence from rowid");
  for (const auto &[arm, expected] :
       std::vector<std::pair<std::string, std::string>>{
           {"fixed", correct},
           {"early-out-filter",
            "East\t2\nMixed\t1\nSouth\t1\nWest\t2\nsouth\t1\n"},
           {"count-all-scans", "East\t3\nWest\t3\nsouth\t1\n"},
           {"distinct-parcels", "East\t1\nWest\t2\nsouth\t1\n"},
           {"projection-control", rows}}) {
    const auto dir = work / arm / id;
    const auto actual = path_output(dir / "witness", db,
                                    dir / "run/project/converter", with_tsv);
    check(actual == expected, "fresh SQLite result differs for " + arm);
    report["witness"][arm] =
        object({{"output", actual}, {"output_sha256", sha(actual)}});
  }

  check(tools() == report["tools"] &&
            hash(active / "SHA256SUMS") == active_pin && hash(db) == db_pin &&
            hash(sqlite_cli) == cli_pin,
        "changed replay context");
  write(work / ".report.json", report);
  run({"sync", "-f", work / ".report.json"});
  for (const auto &exe : programs)
    fs::remove(exe);
  fs::rename(work / ".report.json", work / "report.json");
  std::cout << (work / "report.json").string() << '\n';
  return 0;
} catch (const std::exception &e) {
  std::cerr << e.what() << '\n';
  return 1;
}
