// Offline Track A intake of the main project's versioned typed-goal
// diagnostics.
#include "pilot.hpp"
#include <algorithm>
#include <iostream>

using namespace v5;
namespace {
J pinned(const Path &file, const std::string &pin) {
  check(fs::is_regular_file(file) && fs::file_size(file) <= 1024 * 1024,
        "invalid handoff file");
  auto raw = bytes(file);
  check(sha(raw) == pin, "handoff file pin mismatch");
  return parse(raw);
}
void fields(const J &record, const Strings &required,
            const Strings &optional = {}) {
  check(record.isObject(), "expected handoff object");
  auto allowed = required;
  append(allowed, optional);
  for (const auto &key : record.getMemberNames())
    check(std::find(allowed.begin(), allowed.end(), key) != allowed.end(),
          "unexpected handoff field " + key);
  for (const auto &key : required)
    check(record.isMember(key), "missing handoff field " + key);
}
void identifier(const J &j) {
  auto s = j.asString();
  check(j.isString() && !s.empty() && s.size() <= 128 &&
            s.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrst"
                                "uvwxyz0123456789-_.") == s.npos,
        "invalid goal or obligation identity");
}
void text_field(const J &j) {
  check(j.isString() && !j.asString().empty() && j.asString().size() <= 512,
        "invalid handoff text field");
}
void text_list(const J &j) {
  check(j.isArray(), "expected diagnostic list");
  for (const auto &v : j)
    text_field(v);
}
J admitted_task(const J &g) {
  fields(g,
         {"schema", "goal_id", "obligation_ids", "typed_task", "gap_id",
          "catalog_sha256", "environment", "diagnosis", "mining_authorized"});
  check(g["schema"] == "unllm.capability-gap/1" &&
            g["mining_authorized"].isBool() && !g["mining_authorized"].asBool(),
        "unsupported or executable gap export");
  identifier(g["goal_id"]);
  const auto &ids = g["obligation_ids"];
  check(ids.isArray() && !ids.empty() && ids.size() <= 64,
        "invalid obligations");
  std::string previous;
  for (const auto &id : ids) {
    identifier(id);
    check(previous < id.asString(), "obligation IDs must be sorted and unique");
    previous = id.asString();
  }
  auto task = g["typed_task"];
  fields(task, {"version", "input", "output", "input_type", "output_type",
                "parameters"});
  check(task["version"] == "unllm.semantic-task/1", "unsupported typed task");
  fields(task["parameters"], {});
  text_field(task["input_type"]);
  text_field(task["output_type"]);
  for (const auto *dir : {"input", "output"}) {
    const auto &p = task[dir];
    fields(p, {"port"}, {"role", "unit", "cardinality", "unit_status"});
    for (const auto &name : p.getMemberNames())
      text_field(p[name]);
    if (p.isMember("unit_status"))
      check(!p.isMember("unit") && (p["unit_status"] == "unknown" ||
                                    p["unit_status"] == "not-applicable"),
            "inconsistent requested unit");
  }
  auto identity = object({{"goal_id", g["goal_id"]},
                          {"obligation_ids", ids},
                          {"typed_task", task}});
  check(g["gap_id"] == compact_hash(identity),
        "gap identity differs from admitted obligation");
  fields(g["environment"], {"artifact", "language", "os"});
  for (const auto &k : g["environment"].getMemberNames())
    text_field(g["environment"][k]);
  const auto &d = g["diagnosis"];
  fields(d, {"kind", "route", "scope", "complete_within_scope", "candidates",
             "unrecognized"});
  text_field(d["kind"]);
  text_field(d["route"]);
  text_list(d["unrecognized"]);
  check(d["scope"] == "qualified_direct_bindings" &&
            d["complete_within_scope"].isBool() && d["candidates"].isArray(),
        "unsupported diagnostic scope");
  for (const auto &candidate : d["candidates"]) {
    fields(candidate, {"binding", "provider", "mismatches", "unknown"});
    text_field(candidate["binding"]);
    text_field(candidate["provider"]);
    text_list(candidate["mismatches"]);
    text_list(candidate["unknown"]);
  }
  if (d["route"] == "catalog_review")
    check(d["complete_within_scope"].asBool() && d["unrecognized"].empty() &&
              (d["kind"] == "no_qualified_provider" ||
               d["kind"] == "incompatible"),
          "catalog review requires a complete bounded catalog diagnosis");
  else if (d["route"] == "qualification")
    check(d["kind"] == "missing_evidence" || d["kind"] == "view_unavailable",
          "invalid qualification diagnosis");
  else
    check(d["route"] == "intake" || d["route"] == "construction" ||
              d["route"] == "verification",
          "unknown diagnostic route");
  task["admission"] = object({{"kind", "confirmed-typed-input"},
                              {"goal_id", g["goal_id"]},
                              {"obligation_ids", ids}});
  return task;
}
J summary_for(const Path &pack, const J &row, const J &contracts) {
  J match;
  for (const auto &c : contracts) {
    auto iface = c["interface"];
    if (c["provider_id"] == row["brick"] &&
        c["capability_id"] == row["effect"] &&
        iface["callable"] == row["call"] &&
        iface["structural_input"] == row["from"] &&
        iface["structural_output"] == row["to"]) {
      check(match.isNull(), "ambiguous qualification record");
      match = c;
    }
  }
  J result = object({{"provider_id", row["brick"]},
                     {"binding_id", row["id"]},
                     {"capability_id", row["effect"]},
                     {"input", row["from"]},
                     {"output", row["to"]},
                     {"qualification", "legacy-only"}});
  if (!match.isNull()) {
    result["contract_sha256"] = sha(dump(match));
    result["source"] = match["source"];
    result["qualification"] = match["qualification"]["level"];
    for (const auto *label : {"independent", "integration"}) {
      auto ref = match["qualification"][label].asString();
      auto at = ref.find('#');
      auto file = project_file(pack, ref.substr(0, at));
      if (at != ref.npos)
        check(load(file).isMember(ref.substr(at + 1)),
              "missing qualification evidence");
      result["evidence"][label] =
          object({{"reference", ref}, {"sha256", digest(file)}});
    }
  }
  return result;
}
void check_selected_evidence(const J &ports, const J &task, const J &chosen) {
  check(chosen["args"].empty(),
        "selected binding requires unsupported arguments");
  for (const auto *dir : {"input", "output"}) {
    const auto &want = task[dir];
    J found;
    for (const auto &port : ports)
      if (port["direction"] == dir && port["binding"] == chosen["id"] &&
          port["provider"] == chosen["brick"]) {
        check(found.isNull(), "ambiguous selected port evidence");
        found = port;
      }
    check(!found.isNull(), "selected binding lacks staged semantic evidence");
    for (const auto *field : {"role", "unit", "cardinality"})
      if (want.isMember(field))
        check(found[std::string(field) + "_status"] == "observed" &&
                  found[field] == want[field],
              "selected provider contradicts requested semantics");
    if (want.isMember("unit_status"))
      check(want["unit_status"] == "not-applicable" &&
                found["unit_status"] == "not-applicable" &&
                found["unit"].isNull(),
            "selected provider does not discharge the requested unit status");
  }
}
J review(const Path &pack, const J &g, const J &task, const J &selected) {
  auto rows = load(pack / "bundle/providers/contracts.json")["providers"],
       contracts = load(pack / "contracts.json");
  auto input = task["input"]["port"], output = task["output"]["port"];
  J direct = array(), readers = array(), writers = array(), related = array();
  auto ports = fs::exists(pack / "semantic-ports.json")
                   ? load(pack / "semantic-ports.json")
                   : array();
  for (const auto &row : rows) {
    if (row["from"] == input && row["to"] == output)
      direct.append(summary_for(pack, row, contracts));
    if (row["from"] == "in:" + task["input_type"].asString() &&
        row["to"] == input)
      readers.append(row["id"]);
    if (row["from"] == output &&
        row["to"] == "out:" + task["output_type"].asString())
      writers.append(row["id"]);
    // Related results support source review. A mismatched input port stays
    // visible.
    if (row["to"] != output || row["from"] == input ||
        !task["output"].isMember("role"))
      continue;
    for (const auto &p : ports) {
      if (p["direction"] != "output" || p["binding"] != row["id"] ||
          p["provider"] != row["brick"])
        continue;
      bool compatible = true;
      for (const auto *attr : {"role", "unit", "cardinality"})
        if (task["output"].isMember(attr))
          compatible &= p[std::string(attr) + "_status"] == "observed" &&
                        p[attr] == task["output"][attr];
      if (task["output"].isMember("unit_status"))
        compatible &= task["output"]["unit_status"] == "not-applicable" &&
                      p["unit_status"] == "not-applicable";
      if (compatible)
        related.append(summary_for(pack, row, contracts));
    }
  }
  auto receipt = object(
      {{"schema", "unllm.acquisition-receipt/1"},
       {"gap_id", g["gap_id"]},
       {"catalog_sha256", g["catalog_sha256"]},
       {"diagnosis", g["diagnosis"]},
       {"environment", g["environment"]},
       {"goal_id", g["goal_id"]},
       {"obligation_ids", g["obligation_ids"]},
       {"typed_task_sha256", compact_hash(g["typed_task"])},
       {"proposed_catalog_sha256", J()},
       {"proposed_providers", array()},
       {"review_scope", "Direct bindings in the pinned legacy table and staged "
                        "semantic output evidence. No composition search."},
       {"inventory", object({{"scanned_binding_count", rows.size()},
                             {"direct", direct},
                             {"input_readers", readers},
                             {"output_writers", writers},
                             {"related_outputs", related}})},
       {"mining_authorized", false},
       {"scheduled_jobs", array()},
       {"activated", false}});
  auto route = g["diagnosis"]["route"].asString();
  if (g["environment"] != object({{"artifact", "file-to-file-cli"},
                                  {"language", "C++17"},
                                  {"os", "linux"}})) {
    check(selected.isNull(), "incompatible environment for proposed provider");
    receipt["disposition"] = "incompatible_environment";
    receipt["reason"] = "The pilot qualification context does not cover this "
                        "requested environment.";
  } else if (!selected.isNull()) {
    check(route == "qualification" && selected["status"] == "selected" &&
              selected["search_complete"].isBool() &&
              selected["search_complete"].asBool() &&
              selected["task_sha256"] == compact_hash(task) &&
              selected["eligible"].isArray() &&
              selected["eligible"].size() == 1,
          "consumer decision does not select this admitted task");
    const auto &chosen = selected["eligible"][0];
    check(chosen["from"] == input && chosen["to"] == output &&
              std::count(rows.begin(), rows.end(), chosen) == 1,
          "selected binding differs from reviewed package");
    auto candidate = summary_for(pack, chosen, contracts);
    check_selected_evidence(ports, task, chosen);
    check(candidate["qualification"] == "ordinary-emission-tested",
          "proposed provider lacks staged qualification");
    receipt["proposed_providers"].append(candidate);
    receipt["proposed_catalog_sha256"] = selected["package"];
    receipt["disposition"] = "qualified_provider_proposed";
    receipt["reason"] = "The consumer selected this retained binding in the "
                        "reviewed semantic view. Main owns final gap closure.";
  } else if (route == "catalog_review") {
    receipt["disposition"] = direct.empty()
                                 ? "no_direct_binding_in_reviewed_catalog"
                                 : "direct_bindings_require_contract_review";
    receipt["reason"] =
        direct.empty() ? "The retained planner table has no operation with "
                         "both requested structural ports. Related outputs and "
                         "direct readers are itemized for adapter review."
                       : "Direct structural bindings require semantic and "
                         "qualification review.";
  } else {
    receipt["disposition"] = route == "qualification"
                                 ? "qualification_review_pending"
                                 : "returned_to_" + route;
    receipt["reason"] = "The reported diagnostic belongs to " + route +
                        ". It supplies no authorization for new mining.";
  }
  return receipt;
}
} // namespace
int main(int argc, char **argv) try {
  check(argc == 6 || argc == 8,
        "usage: v5-gap-review GAP GAP_PIN ORIGIN_PACKAGE REVIEW_PACKAGE "
        "REVIEW_PIN [MAIN_DECISION DECISION_PIN]");
  auto g = pinned(argv[1], argv[2]);
  auto task = admitted_task(g);
  auto origin = fs::canonical(argv[3]), pack = fs::canonical(argv[4]);
  verify(origin, g["catalog_sha256"].asString());
  if (pack != origin || g["catalog_sha256"].asString() != argv[5])
    verify(pack, argv[5]);
  J decision;
  if (argc == 8) {
    decision = pinned(argv[6], argv[7]);
    check(decision["package"].asString() == argv[5],
          "consumer decision belongs to a different reviewed package");
  }
  auto r = review(pack, g, task, decision);
  r["gap_sha256"] = argv[2];
  r["reviewed_catalog_sha256"] = argv[5];
  if (argc == 8)
    r["consumer_decision_sha256"] = argv[7];
  auto dir = fresh_run("v5-gap-review");
  write(dir / ".receipt", r);
  fs::rename(dir / ".receipt", dir / "receipt.json");
  std::cout << (dir / "receipt.json").string() << " "
            << digest(dir / "receipt.json") << '\n';
  return 0;
} catch (const std::exception &e) {
  std::cerr << e.what() << '\n';
  return 1;
}
