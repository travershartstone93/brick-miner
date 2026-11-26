#include "../../src/experiments/original_adapter.hpp"
#include "bundle.hpp"
#include "pilot.hpp"
#include <iostream>
using namespace v5;

namespace {
J discover(const Path &trial, const std::string &route,
           const std::string &unit) {
  const Path mounted = "/tmp/brick-trial";
  return parse(run({"bwrap",
                    "--unshare-all",
                    "--die-with-parent",
                    "--new-session",
                    "--clearenv",
                    "--setenv",
                    "PATH",
                    "/usr/bin",
                    "--setenv",
                    "LC_ALL",
                    "C",
                    "--ro-bind",
                    "/usr",
                    "/usr",
                    "--symlink",
                    "usr/lib",
                    "/lib",
                    "--symlink",
                    "usr/lib",
                    "/lib64",
                    "--symlink",
                    "usr/bin",
                    "/bin",
                    "--tmpfs",
                    "/tmp",
                    "--dev",
                    "/dev",
                    "--proc",
                    "/proc",
                    "--ro-bind",
                    trial / "build",
                    mounted / "build",
                    "--ro-bind",
                    trial / "input",
                    mounted / "input",
                    "--ro-bind",
                    trial / "compile_commands.json",
                    mounted / "compile_commands.json",
                    "--ro-bind",
                    root / ".build/brick-mine",
                    "/opt/mine",
                    "--chdir",
                    mounted,
                    "/opt/mine",
                    route,
                    mounted,
                    mounted / "input/gdbsupport" / unit},
                   {.seconds = 60}));
}

J probe(const Path &exe, const Path &dir, const J &c) {
  fs::create_directories(dir);
  if (c.isMember("input"))
    save(dir / "input", c["input"].asString());
  if (c.isMember("prior"))
    save(dir / "output", c["prior"].asString());
  auto args = sandbox(exe, dir);
  append(args, {"input", "output"});
  auto proc = execute(args, {.seconds = 8, .probe_limits = true});
  check(proc.status == c.get("expected_exit", 0).asInt(),
        "wrong exit: " + c["name"].asString());
  std::string effect;
  if (proc.status == 0) {
    check(load(dir / "output") == c["expected_json"],
          "incorrect escaped output");
    effect = c.isMember("prior") ? "replaced" : "created";
  } else if (c.isMember("prior")) {
    check(bytes(dir / "output") == c["prior"].asString(),
          "existing output changed on failure");
    effect = "preserved";
  } else {
    check(!fs::exists(dir / "output"), "output created on failure");
    effect = "absent";
  }
  return object(
      {{"case", c["name"]}, {"exit", proc.status}, {"destination", effect}});
}
} // namespace

int main() try {
  const std::string key = "line_xml_escape", brick = "miner_line_xml_escape";
  const auto fixture = root / "tests/v5/xml-domain.json",
             guard_client = root / "tests/v5/xml-bound.cpp";
  check(digest(fixture) ==
            "0403898b84e8872d21f32fb5ed4cab6eeb8bb2e18cda98da4ca67d4d97742f69",
        "changed development oracle");
  auto cases = load(fixture);
  auto work = fresh_run("v5-xml-domain"), scratch = work / ".test-programs";
  std::cout << work.string() << std::endl;
  fs::create_directory(scratch);
  auto prior = runs / "trial-6gdh6gpv/run-1", bundle = prior / "bundle",
       input = prior / "input";
  auto old_report = root / "evidence/pilot/run-1.json";
  check(digest(old_report) ==
            "d09799172fb232bdee3b1bea87bd34682600e88ed11b405bcd39241429a85652",
        "changed historical qualification report");
  auto history = load(old_report), provenance = history["provenance"][key];
  auto evidence = bundle / "qualifications" / brick;
  check(digest(bundle / "SHA256SUMS") == history["manifest_sha256"].asString(),
        "changed historical bundle manifest");
  run({"sha256sum", "--quiet", "--strict", "--check", "SHA256SUMS"},
      {.cwd = bundle});
  Path archive =
      Path("/opt/cuda/extras") / provenance["acquired_archive"].asString();
  check(digest(archive) == provenance["archive_sha256"].asString(),
        "changed upstream source archive");
  auto cdb_hash = digest(prior / "compile_commands.json");
  check(cdb_hash ==
            "a0f0f0906f79a2b2334623ea81face5df319997ccbf726bacf46a6cd4312d168",
        "changed Clang database");
  J members = object();
  for (const std::string member :
       {"gdbsupport/xml-utils.cc", "gdbsupport/xml-utils.h",
        "gdbsupport/common-inferior.cc", "COPYING3"}) {
    auto raw = run({"bsdtar", "-xOf", archive, "cuda-gdb-13.3.73/" + member});
    check(raw == bytes(input / member),
          "member differs from archive: " + member);
    members[member] = sha(raw);
  }
  auto source_hash = sha(dump(tree_hashes(input)));
  auto build_hash = sha(dump(tree_hashes(prior / "build")));
  check(source_hash == "ae1ae671d3c3c7659ef11e811c9e0d665fee5e3347294bf507c1f9c"
                       "5ca4f9587" &&
            build_hash == "ce2d6b474da3f44f93656d65fe3462dcc300de35eceac40c469c"
                          "34ec5d22f5e9",
        "changed historical compilation inputs");
  auto found = discover(prior, "application", "xml-utils.cc"),
       rejected = discover(prior, "state", "common-inferior.cc");
  check(found == load(evidence / "discovery.json") &&
            found["closure"] ==
                array({"xml_escape_text_append", "xml_escape_text"}) &&
            rejected == history["rejected"] &&
            rejected["mutable_globals"] == array({"startup_with_shell"}),
        "Clang closure or rejected state differs");
  auto header = pilots::render_original_adapter(
      key, found, bytes(input / "gdbsupport/xml-utils.cc"));
  check(sha(header) == history["header_sha256"][key].asString() &&
            bytes(bundle / "headers" / (brick + ".hpp")) == header,
        "changed adapter bytes");
  auto body = found["body"].asString();
  std::string prefix = "if (", suffix = ")\n";
  auto end = body.find(suffix);
  check(body.starts_with(prefix) && end != body.npos &&
            body.find("throw std::invalid_argument", end) != body.npos,
        "missing adapter guard");
  auto condition = body.substr(prefix.size(), end - prefix.size());
  check(condition == "s.size() > INT_MAX || s.find(char(0)) != s.npos",
        "changed adapter domain");
  J guard = object(), clients = object();
  for (const std::string variant : {"original", "closed-upper-bound"}) {
    auto expr = variant == "original"
                    ? condition
                    : replace(condition, "> INT_MAX", ">= INT_MAX");
    auto exe = scratch / variant;
    run({"g++-15", "-std=c++17", "-O1", "-DBOUND_GUARD=(" + expr + ")",
         guard_client, "-o", exe});
    auto result = execute(sandbox(exe), {.seconds = 8, .probe_limits = true});
    check(result.status == (variant == "original" ? 0 : 1),
          "guard comparison failed");
    guard[variant] = object(
        {{"predicate", expr}, {"exit", result.status}, {"trace", result.out}});
  }
  auto client = root / "tests/client.cpp";
  for (const std::string variant : {"intact", "nul-guard-removed"}) {
    auto source = variant == "intact"
                      ? header
                      : replace(header, condition, "s.size() > INT_MAX");
    auto include = work / variant / (brick + ".hpp");
    save(include, source);
    auto exe = scratch / variant;
    run({"g++-15", "-std=c++17", "-O1", "-include", include,
         "-DAPPLY=brick_miner::line_xml_escape", client, "-o", exe});
    auto result = execute(sandbox(exe), {.seconds = 8, .probe_limits = true});
    check(result.status == (variant == "intact" ? 0 : 2),
          "exception client failed");
    clients[variant] =
        object({{"source_sha256", sha(source)}, {"exit", result.status}});
  }
  auto emitted = work / "historical-emission";
  auto plan_result = plan(bundle, load(evidence / "spec.json"), emitted);
  auto plans = plan_result["providers"];
  check(plans.size() == 1 && plans[0]["status"] == "complete" &&
            plans[0]["source"].asString() ==
                bytes(prior / "checks" / key / "staged/component.hpp") &&
            digest(emitted / "bricks" / (brick + ".hpp")) == sha(header),
        "historical ordinary emission no longer matches reference");
  auto exe = scratch / "file-program";
  build_emission(plans[0], emitted, exe, "g++-15");
  J observations = array();
  for (unsigned i = 0; i < cases["cli_cases"].size(); ++i)
    observations.append(
        probe(exe, work / "cases" / std::to_string(i), cases["cli_cases"][i]));
  J assumptions = cases["required_input_facts"];
  J receipt = object(
      {{"scope", "Isolated H1a study on the historical provider. No V5 "
                 "admission or selection."},
       {"provider", brick},
       {"historical_bundle_sha256", history["manifest_sha256"]},
       {"source", provenance},
       {"source_member_sha256", members},
       {"frozen_input_tree_sha256", source_hash},
       {"frozen_build_tree_sha256", build_hash},
       {"compile_commands_sha256", cdb_hash},
       {"extractor_sha256", digest(root / ".build/brick-mine")},
       {"compiler_sha256", digest("/usr/bin/g++-15")},
       {"discovery", found},
       {"rejected", rejected},
       {"adapter_sha256", sha(header)},
       {"guard_client_sha256", digest(guard_client)},
       {"component_client_sha256", digest(client)},
       {"fixture_sha256", digest(fixture)},
       {"required_input_facts", assumptions},
       {"target", cases["target"]},
       {"guard_origin", "Manually authored C++ adapter for a NUL-terminated "
                        "upstream API with a signed int index."},
       {"bound_evidence",
        "Extracted production guard expression on synthetic lengths, including "
        "INT_MAX and INT_MAX+1. No giant buffer execution."},
       {"guard_probes", guard},
       {"component_exception_checks", clients},
       {"cli_observations", observations},
       {"emitted_source_sha256", digest(emitted / "component.hpp")},
       {"program_sha256", tree_hashes(scratch)},
       {"unassessed", array({"allocation failure",
                             "output-device failure after partial write",
                             "concurrency", "other integer widths"})},
       {"admitted", false},
       {"published_capabilities", 0}});
  write(work / ".receipt.json", receipt);
  fs::rename(work / ".receipt.json", work / "receipt.json");
  run({"sync", "-f", work / "receipt.json"});
  fs::remove_all(scratch);
  std::cout << (work / "receipt.json").string() << " "
            << digest(work / "receipt.json") << "\n";
  return 0;
} catch (const std::exception &error) {
  std::cerr << error.what() << "\n";
  return 1;
}
