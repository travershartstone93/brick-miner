#include "database.hpp"
#include "migration_reports.hpp"
#include "pilot.hpp"
#include <algorithm>
#include <iomanip>
#include <sstream>

namespace v5 {
namespace {
struct Usage {
  J row;
  std::string disposition;
  std::set<std::string> requests, complete_slots;
  J steps = array();
};
bool includes(const J &items, const J &item) {
  return std::find(items.begin(), items.end(), item) != items.end();
}
// Match each step against the archived binder's operation-index rule, its
// normalized requirement and the actual corresponding source statement.
bool matches(const J &row, const J &role, const J &ops, const J &step,
             const std::string &expression) {
  if (step["from"] != row["from"] || step["to"] != row["to"] ||
      !includes(role["bricks"], row["brick"]) ||
      !includes(role["headers"], row["header"]))
    return false;
  auto call = row["call"].asString() + "(";
  if (!expression.starts_with(call))
    return false;
  if (row["effect"].isNull())
    return step["provider"] == row["id"];
  if (ops.size() == 1)
    return step["provider"] == row["id"] && ops[0]["outcome"] == row["effect"];
  for (Json::ArrayIndex i = 0; i < ops.size(); ++i)
    if (ops[i]["outcome"] == row["effect"] &&
        step["provider"] ==
            "op_" + std::to_string(i) + "_" + row["id"].asString())
      return true;
  return false;
}
} // namespace

J replay_binding_usage(const Path &snapshot, const std::string &pin) {
  const Path ledger = project_file(snapshot, "ledger.sqlite");
  check_corpus(ledger);
  check(digest(ledger) == pin, "migration ledger pin mismatch");
  const Path base = verified_base();
  auto registry = load(base / "providers/contracts.json")["providers"];
  const std::string canonical_registry = dump(registry);
  Database db(ledger, true);
  db.script("PRAGMA trusted_schema=OFF; BEGIN");
  check(db.query("SELECT path,legacy_manifest_sha256 FROM generation WHERE "
                 "role='active legacy'") ==
            std::vector<Strings>{{base, base_hash}},
        "replay catalog generation mismatch");
  auto rows = db.query(
      "SELECT i.id,r.legacy_row,i.disposition FROM inventory i "
      "JOIN revision r ON r.key=i.revision WHERE i.role='active legacy' "
      "AND i.kind='binding' ORDER BY i.ordinal");
  check(rows.size() == registry.size(), "ledger binding count mismatch");
  std::vector<Usage> bindings;
  for (const auto &r : rows) {
    const J &row = registry[Json::ArrayIndex(bindings.size())];
    check(row["id"] == r[0] && r[1] == dump(row),
          "ledger binding row mismatch");
    bindings.push_back({row, r[2], {}, {}});
  }
  Path archive = runs / "english-audit-bcsgf8pi/replay-staged";
  J pins = object(), slots = object();
  slots["complete"] = slots["partial"] = 0;
  std::set<std::string> ids;
  for (const auto &entry : fs::directory_iterator(archive))
    if (entry.path().filename().string().starts_with("H"))
      ids.insert(entry.path().filename());
  unsigned holes = 0, resolved = 0, selected_requests = 0;
  for (unsigned n = 1; n <= 100; ++n) {
    std::ostringstream name;
    name << 'H' << std::setfill('0') << std::setw(3) << n;
    auto id = name.str();
    check(ids.erase(id), "missing replay " + id);
    auto read = [&](const std::string &path) {
      auto raw = bytes(archive / id / path);
      pins[id][path] = sha(raw);
      return parse(raw);
    };
    auto receipt = read("receipt.json"),
         requirements = read("run/requirements.json"),
         emission = read("run/components/emission.json");
    check(receipt["arm"] == "replay-staged" && receipt["request"] == id &&
              receipt["interpreted"] == true && receipt["editor"] == false,
          "unexpected replay context " + id);
    check(emission["providers"].isArray() &&
              dump(read("run/components/contracts.json")["providers"]) ==
                  canonical_registry,
          "different archived binding registry " + id);
    unsigned before = resolved;
    for (const auto &role : emission["providers"]) {
      auto status = role["status"].asString();
      check(slots.isMember(status) && role["slot"].isUInt(),
            "unknown replay slot");
      slots[status] = slots[status].asInt() + 1;
      auto slot = role["slot"].asUInt();
      auto ops = requirements["r" + std::to_string(slot + 1)]["operations"];
      auto steps = role["plan"]["steps"];
      Strings statements;
      std::istringstream source(role["source"].asString());
      for (std::string line; std::getline(source, line);)
        if (line.starts_with("    ") && line.ends_with(");"))
          statements.push_back(line.substr(4));
      check(ops.isArray() && steps.isArray() &&
                statements.size() == steps.size(),
            "unrecognized emitted body " + id);
      for (Json::ArrayIndex i = 0; i < steps.size(); ++i) {
        auto &step = steps[i];
        auto expression = statements[i];
        const auto assignment = "auto value_" + std::to_string(i + 1) + " = ";
        if (expression.starts_with(assignment))
          expression.erase(0, assignment.size());
        if (!step.isMember("provider")) {
          check(expression.starts_with("gap_" + std::to_string(slot) + "_" +
                                       std::to_string(i) + "("),
                "unrecognized missing step " + id);
          ++holes;
          continue;
        }
        Usage *found = nullptr;
        for (auto &b : bindings)
          if (matches(b.row, role, ops, step, expression)) {
            check(!found, "ambiguous archived binding " + id);
            found = &b;
          }
        check(found, "unresolved replay step " + id + " " +
                         step["provider"].asString());
        found->requests.insert(id);
        if (status == "complete")
          found->complete_slots.insert(id);
        found->steps.append(object({{"request", id},
                                    {"slot", slot},
                                    {"step", i},
                                    {"emitted_id", step["provider"]},
                                    {"slot_status", status}}));
        ++resolved;
      }
    }
    selected_requests += resolved > before;
  }
  check(ids.empty(), "unexpected extra replay request");
  std::sort(bindings.begin(), bindings.end(), [](const auto &a, const auto &b) {
    if (a.requests.size() != b.requests.size())
      return a.requests.size() > b.requests.size();
    return std::tie(a.row["id"], a.row["from"], a.row["to"]) <
           std::tie(b.row["id"], b.row["from"], b.row["to"]);
  });
  J ranked = array();
  for (const auto &b : bindings)
    ranked.append(object(
        {{"binding_id", b.row["id"]},
         {"component_id", b.row["brick"]},
         {"from", b.row["from"]},
         {"to", b.row["to"]},
         {"binding_sha256", compact_hash(b.row)},
         {"disposition", b.disposition},
         {"request_count", unsigned(b.requests.size())},
         {"requests", string_array({b.requests.begin(), b.requests.end()})},
         {"requests_with_complete_slot",
          string_array({b.complete_slots.begin(), b.complete_slots.end()})},
         {"steps", b.steps}}));
  return object(
      {{"schema", "unllm.h1b-replay-usage/1"},
       {"active_manifest_sha256", base_hash},
       {"ledger_sha256", pin},
       {"archive", archive.string()},
       {"inputs", pins},
       {"requests", 100},
       {"requests_with_binding", selected_requests},
       {"slots", slots},
       {"resolved_steps", resolved},
       {"typed_holes", holes},
       {"bindings", ranked},
       {"scope",
        "Distinct requests with a selected step in the saved developer "
        "interpretations. Slot completion measures only that role. This report "
        "grants no acquisition or qualification status."}});
}
} // namespace v5
