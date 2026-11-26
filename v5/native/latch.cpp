#include "pilot.hpp"
#include <algorithm>
#include <sstream>

namespace v5 {
namespace {
const std::string key = "threshold_entries";
J observe(const Path &exe, const J &steps) {
  Strings commands;
  for (const auto &s : steps) {
    check(s.size() == 4, "malformed lifecycle step");
    commands.push_back(s[0].asString() + " " + s[1].asString() + " " +
                       s[2].asString());
  }
  auto raw = run(sandbox(exe), {.input = join(commands, "\n") + "\n",
                                .seconds = 8,
                                .probe_limits = true});
  std::istringstream lines(raw);
  std::string line;
  J responses = array();
  while (std::getline(lines, line))
    responses.append(line);
  check(responses.size() == steps.size(), "incomplete lifecycle observation");
  return responses;
}
Path build_client(const Path &dir, const std::string &implementation) {
  fs::create_directories(dir);
  save(dir / "keyed_latch.hpp", implementation);
  auto exe = probe_program(dir / "client");
  run({"g++-15", "-std=c++17", "-O1", "-I", dir,
       v5_root() / "fixtures/latch-client.cpp", "-o", exe});
  return exe;
}
J answers(const J &steps) {
  auto result = array();
  for (const auto &step : steps)
    result.append(step[3]);
  return result;
}
J bounded_reference(const Path &exe) {
  // Independent reference uses two Boolean cells and enumerates all four-event
  // words.
  J steps = array();
  unsigned words = 0;
  const Strings events = {"trigger", "hold", "rearm"};
  for (unsigned word = 0; word < 6 * 6 * 6 * 6; ++word) {
    bool active[2] = {false, false};
    auto digits = word;
    steps.append(array({"A", "_", "open", "opened"}));
    for (unsigned i = 0; i < 4; ++i) {
      unsigned action = digits % 6;
      digits /= 6;
      unsigned k = action / 3;
      auto e = events[action % 3];
      bool emitted = false;
      if (e == "trigger") {
        emitted = !active[k];
        active[k] = true;
      } else if (e == "rearm")
        active[k] = false;
      steps.append(
          array({"A", k ? "b" : "a", e, emitted ? "emitted" : "silent"}));
    }
    steps.append(array({"A", "_", "close", "closed"}));
    ++words;
  }
  auto actual = observe(exe, steps);
  check(actual == answers(steps), "bounded latch reference mismatch");
  return object({{"keys", 2},
                 {"events_per_word", 4},
                 {"words", words},
                 {"event_alphabet", string_array(events)},
                 {"responses_sha256", sha(dump(actual))}});
}
} // namespace
J retained_source(const Path &dest, const std::string &id,
                  const std::string &notice) {
  auto s = sources()[id];
  Path archive = s["archive"].asString(), base = verified_base();
  auto members = s["members"];
  for (const auto &file : members.getMemberNames()) {
    auto &member = members[file];
    auto raw = run({"bsdtar", "-xOf", archive, member[0].asString()});
    check(sha(raw) == member[1].asString() && raw == bytes(base / file),
          "retained provider differs from pinned source " + file);
    save(dest / "original" / file, raw);
  }
  auto license = run({"bsdtar", "-xOf", archive, "LICENSE"});
  check(sha(license) == s["rights"]["notice_sha256"].asString(),
        "license pin mismatch");
  save(dest / "notices" / notice, license);
  return object({{"source_id", s["id"]},
                 {"revision", s["revision"]},
                 {"archive_sha256", s["sha256"]},
                 {"unchanged_members", members},
                 {"usage", s["examples"]}});
}
void extract_latch(const Path &dest) {
  auto s = retained_source(dest, "retained-keyed-latch", "keyed-latch-LICENSE");
  s["binding"] = "threshold_keys";
  s["legacy_brick"] = "k42";
  s["scope"] = "Local backfill of the existing key-output binding";
  write(dest / "latch-source.json", s);
}
void qualify_latch(const Path &extracted, const Path &dest) {
  auto fixture = v5_root() / "fixtures/latch-traces.json";
  auto traces = load(fixture)["traces"];
  auto raw = bytes(extracted / "original/headers/keyed_latch.hpp");
  auto exe = build_client(dest / "production", raw);
  auto observed = object();
  for (const auto &name : traces.getMemberNames()) {
    auto steps = traces[name], first = observe(exe, steps),
         second = observe(exe, steps);
    check(first == answers(steps) && first == second,
          "lifecycle oracle or repeat failed for " + name);
    auto rows = array();
    for (unsigned i = 0; i < steps.size(); ++i)
      rows.append(array({steps[i][0], steps[i][1], steps[i][2], first[i]}));
    observed[name] = rows;
  }
  auto controls = object();
  auto no_rearm = replace(raw, "active_.erase(key)", "(void)key");
  auto one_key = replace(
      replace(raw, "active_.insert(key)", "active_.insert(\"one-key\")"),
      "active_.erase(key)", "active_.erase(\"one-key\")");
  for (const auto &[id, changed] : std::map<std::string, std::string>{
           {"ignore-rearm", no_rearm}, {"merge-keys", one_key}}) {
    check(changed != raw, "latch mutant failed to modify source");
    auto mutant = build_client(dest / id, changed);
    auto rejected = array();
    for (const auto &name : traces.getMemberNames())
      if (observe(mutant, traces[name]) != answers(traces[name]))
        rejected.append(name);
    check(!rejected.empty(), "lifecycle mutant survived");
    controls[id] =
        object({{"source_sha256", sha(changed)}, {"rejected_by", rejected}});
  }
  auto adapter_client = v5_root() / "fixtures/latch-rows-client.cpp",
       adapter_exe = probe_program(dest / "rows-client");
  run({"g++-15", "-std=c++17", "-O1", "-I", extracted / "original/providers",
       "-I", extracted / "original/headers", adapter_client, "-o",
       adapter_exe});
  auto adapter_result =
      run(sandbox(adapter_exe), {.seconds = 8, .probe_limits = true});
  check(adapter_result == "fresh-per-call interleaved-keys trigger-priority\n",
        "adapter lifecycle mismatch");
  auto receipt = object(
      {{"schema", "unllm.observed-lifecycle/1"},
       {"provider", "k42"},
       {"binding", "threshold_keys"},
       {"adapter_client_sha256", digest(adapter_client)},
       {"adapter_observation", adapter_result},
       {"state_scope",
        "one adapter call; independent keys within encounter order"},
       {"oracle_sha256", digest(fixture)},
       {"client_sha256", digest(v5_root() / "fixtures/latch-client.cpp")},
       {"implementation_sha256", sha(raw)},
       {"columns", array({"instance", "key", "event", "response"})},
       {"traces", observed},
       {"fresh_executions_per_trace", 2},
       {"bounded_reference", bounded_reference(exe)},
       {"mutants", controls},
       {"scope", "Sequential per-key rearm; open/close denote harness instance "
                 "lifetime. No concurrency or unbounded proof."}});
  write(dest / "lifecycle.json", object({{"threshold_keys", receipt}}));
  write(dest / "standalone.json",
        object({{key, object({{"outcome", "all named lifecycle traces matched "
                                          "twice and both mutants rejected"},
                              {"client_sha256", receipt["client_sha256"]},
                              {"evidence",
                               "evidence/lifecycle.json#threshold_keys"}})}}));
}
} // namespace v5
