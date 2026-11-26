#include "pilot.hpp"

namespace v5 {
namespace {
const std::string component = "u84", source_id = "retained-unique-fifo";
J descriptor(const J &library) {
  J found;
  for (const auto &candidate : library["classes"])
    if (candidate["brick"] == component) {
      check(found.isNull(), "ambiguous retained class");
      found = candidate;
    }
  check(!found.isNull() && !found["methods"].empty() &&
            !library["value_types"].empty(),
        "incomplete retained library descriptor");
  return found;
}
Result client(const Path &dir, const Path &include, const std::string &type,
              bool original) {
  fs::create_directories(dir);
  Strings cmd = {"g++-15", "-std=c++17", "-O1", "-I", include};
  if (original)
    cmd.push_back("-DFROM_RETAINED_SOURCE");
  check(type == "integer" || type == "string", "unsupported FIFO probe type");
  if (type == "string")
    cmd.push_back("-DFIFO_STRING");
  auto exe = probe_program(dir / "client");
  append(cmd, {v5_root() / "fixtures/fifo-client.cpp", "-o", exe});
  run(cmd);
  return execute(sandbox(exe), {.seconds = 8, .probe_limits = true});
}
J positive(const Path &dir, const Path &include, const std::string &type,
           bool original) {
  auto result = client(dir, include, type, original);
  check(!result.status && !result.out.empty(), "FIFO behavioral check failed " +
                                                   type + ": " + result.err +
                                                   result.out);
  auto exe = probe_program(dir / "client");
  auto repeat = run(sandbox(exe), {.seconds = 8, .probe_limits = true});
  check(repeat == result.out, "FIFO observations changed across processes");
  return object({{"observations", result.out},
                 {"binary_sha256", digest(exe)},
                 {"executions", 2}});
}
} // namespace
void extract_fifo(const Path &dest) {
  auto source = retained_source(dest, source_id, "unique-fifo-LICENSE");
  source["legacy_brick"] = component;
  auto library = load(dest / "original/providers/library.json");
  source["class"] = descriptor(library);
  source["value_types"] = library["value_types"];
  write(dest / "fifo-source.json", source);
}
void qualify_fifo(const Path &extracted, const Path &dest) {
  auto source = load(extracted / "fifo-source.json");
  const auto include = extracted / "original/headers";
  J report = object();
  for (const auto &type : source["value_types"].getMemberNames())
    report["types"][type] = positive(dest / type, include, type, true);
  auto header = source["class"]["header"].asString(),
       raw = bytes(include / header);
  auto changed = replace(replace(raw, "entries_.front()", "entries_.back()"),
                         "entries_.pop_front()", "entries_.pop_back()");
  check(changed != raw, "FIFO mutant unchanged");
  auto mutant = dest / "mutant";
  save(mutant / header, changed);
  auto rejected = client(mutant, mutant, "integer", true);
  check(rejected.status == 1 &&
            rejected.out.find("oldest-A fail") != std::string::npos,
        "LIFO substitution survived or failed for unrelated reason");
  report["lifo_control"] =
      object({{"source_sha256", sha(changed)}, {"observations", rejected.out}});
  report["outcome"] =
      "Matched independent two-receiver expectations for both value types";
  report["client_sha256"] = digest(v5_root() / "fixtures/fifo-client.cpp");
  write(dest / "standalone.json", object({{component, report}}));
}
void emit_fifo(const Path &base, const Path &bundle, const Path &dest) {
  auto library = load(bundle / "providers/library.json"),
       entry = descriptor(library);
  auto emitter = main_repo / "build/native/brick-header-emit";
  write(dest / "picks.json", array({object({{"id", component}})}));
  write(dest / "eligible.json", array({component}));
  write(dest / "empty.json", array());
  auto fixture = load(v5_root() / "fixtures/fifo-spec.json"), report = object();
  const auto &methods = fixture["artifact"]["methods"];
  std::set<std::string> exercised;
  for (const auto &name : methods.getMemberNames())
    exercised.insert(methods[name].asString());
  auto all = entry["methods"].getMemberNames();
  check(exercised == std::set<std::string>(all.begin(), all.end()),
        "fixture does not cover all class methods");
  for (const auto &type : library["value_types"].getMemberNames()) {
    auto spec = fixture;
    spec["artifact"]["value_type"] = type;
    auto file = dest / (type + ".json");
    write(file, spec);
    for (const std::string arm : {"baseline", "staged", "removed"}) {
      auto dir = dest / type / arm;
      auto result =
          execute({emitter, arm == "baseline" ? base : bundle, file,
                   dest / "picks.json",
                   dest / (arm == "removed" ? "empty.json" : "eligible.json")});
      if (arm == "removed") {
        check(result.status != 0 &&
                  result.err.find("No registered class implements") !=
                      std::string::npos,
              "ineligible class emitted");
        report[type][arm] = "no eligible class";
        continue;
      }
      check(result.status == 0,
            "ordinary header emission failed: " + result.err);
      auto plan = parse(result.out);
      check(plan["brick"] == entry["brick"] &&
                plan["filename"] == "delivery.hpp" && plan["uncovered"].empty(),
            "unexpected class emission");
      save(dir / "delivery.hpp", plan["source"].asString());
      auto trial = positive(dir, dir, type, false);
      trial["header_sha256"] = digest(dir / "delivery.hpp");
      trial["operations"] = string_array(all);
      report[type][arm] = trial;
    }
  }
  write(dest / "fifo-integration.json", report);
}
J class_schema(J common, const J &interface) {
  common["properties"]["schema"]["const"] = "unllm.library-class/1";
  common["properties"]["vocabulary"]["const"] =
      "unllm.library-class-vocabulary/1";
  common["properties"].removeMember("capability_id");
  auto required = array();
  for (const auto &name : common["required"])
    if (name != "capability_id")
      required.append(name);
  common["required"] = required;
  common["properties"]["interface"] = interface;
  common["properties"]["qualification"]["properties"]["level"] =
      object({{"const", "ordinary-header-tested"}});
  return common;
}
J class_contract(const Path &p) {
  const auto a = load(v5_root() / "fixtures/fifo-annotations.json");
  auto source = load(p / "evidence/fifo-source.json"),
       proof = load(p / "evidence/fifo-integration.json"),
       standalone = load(p / "evidence/standalone.json")[component],
       registry = sources()[source_id];
  auto entry = descriptor(load(p / "bundle/providers/library.json")),
       iface = entry;
  check(entry == source["class"] &&
            source["value_types"] ==
                load(p / "bundle/providers/library.json")["value_types"],
        "class binding differs from extracted source");
  iface.removeMember("brick");
  iface["kind"] = "library-class";
  iface["value_types"] = source["value_types"];
  for (const auto &type : iface["value_types"].getMemberNames()) {
    auto expected = standalone["types"][type]["observations"].asString();
    check(!expected.empty() &&
              proof[type]["baseline"]["observations"] == expected &&
              proof[type]["staged"]["observations"] == expected &&
              proof[type]["removed"] == "no eligible class",
          "class lacks ordinary controls");
  }
  auto facets = a["facets"];
  facets["dependencies"] = registry["dependencies"];
  facets["license"] = array(
      {registry["rights"]["upstream_files"], registry["rights"]["scope"]});
  facets["qualification"] = array({"ordinary-header-tested"});
  auto claims = object(), signature = array();
  auto fields = load(p / "vocabulary.json")["claims"];
  for (const auto &field : fields) {
    auto name = field.asString(), status = a["details"][name][0].asString();
    auto refs = array();
    if (status == "observed") {
      signature.append(name);
      refs = array(
          {"evidence/standalone.json#u84", "evidence/fifo-integration.json"});
    } else if (status == "declared")
      refs.append(name == "specification_question"
                      ? "evidence/fifo-spec.json"
                      : "evidence/fifo-source.json");
    claims[name] = object({{"status", status},
                           {"detail", a["details"][name][1]},
                           {"evidence", refs}});
  }
  auto header = "headers/" + iface["header"].asString();
  return object(
      {{"schema", "unllm.library-class/1"},
       {"provider_id", component},
       {"legacy_brick", component},
       {"description", a["description"]},
       {"vocabulary", "unllm.library-class-vocabulary/1"},
       {"primary_family", a["primary_family"]},
       {"facets", facets},
       {"interface", iface},
       {"claims", claims},
       {"property_signature", signature},
       {"source",
        object({{"id", source_id},
                {"revision", source["revision"]},
                {"archive_sha256", source["archive_sha256"]},
                {"implementation_sha256", digest(p / "bundle" / header)},
                {"header", header},
                {"source_registry_sha256", digest(p / "sources.json")}})},
       {"fingerprint",
        object({{"scope", "Two concrete value specializations; sequential "
                          "ownership and method checks"},
                {"suite_sha256", standalone["client_sha256"]},
                {"observations_sha256", sha(dump(proof))}})},
       {"qualification",
        object({{"level", "ordinary-header-tested"},
                {"scope", "Local ordinary header emitter, two value types; "
                          "strict eligibility"},
                {"independent", "evidence/standalone.json#u84"},
                {"integration", "evidence/fifo-integration.json"}})}});
}
} // namespace v5
