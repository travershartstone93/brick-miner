#pragma once
#include "pilot.hpp"

namespace xml_fixture {
using namespace v5;

// Experimental exchange only. This fixture has no executable provider table.
inline void write_fixture(const Path &work, const Path &historical,
                          const J &observed) {
  auto control = runs / "v5-queue-XX0JbR2o/stage/package";
  const std::string control_pin =
      "83f1bc1008b1883463749144fd3beabb4d3b1016d4575014a62febbf164a1aa3";
  auto ids = verify(control, control_pin);
  check(ids.size() == 7 &&
            std::find(ids.begin(), ids.end(), "line_xml_escape") == ids.end(),
        "wrong unrestricted control");
  auto rows = load(control / "bundle/providers/contracts.json")["providers"];
  for (const auto &row : rows)
    check(row["effect"] != "line_xml_escape" &&
              row["brick"] != "miner_line_xml_escape",
          "restricted provider visible in default catalog");
  auto raw = load(historical / "providers/contracts.json")["providers"];
  auto chosen = select_binding(raw, "line_xml_escape", "miner_line_xml_escape",
                               "strings", "strings");
  auto include = work / "intact/miner_line_xml_escape.hpp";
  auto header_sha = digest(include);
  check(header_sha == observed["adapter_sha256"].asString() &&
            digest(historical / "headers" / chosen["header"].asString()) ==
                header_sha,
        "wrong restricted implementation");
  auto witness = work / "abi-check.cpp";
  save(witness,
       "#include \"miner_line_xml_escape.hpp\"\n#include <type_traits>\n"
       "using Seq=std::vector<std::string>;\n"
       "static_assert(std::is_same_v<decltype(&brick_miner::line_xml_escape), "
       "Seq (*)(const Seq&)>);\n"
       "static_assert(CHAR_BIT==8 && INT_MAX==2147483647 && sizeof(int)==4);\n"
       "static_assert(__cplusplus==201703L && _GLIBCXX_USE_CXX11_ABI==1);\n");
  run({"g++-15", "-std=c++17", "-I", include.parent_path(), "-fsyntax-only",
       witness});
  auto triple = run({"g++-15", "-dumpmachine"});
  triple.erase(triple.find_last_not_of("\r\n") + 1);
  check(triple == "x86_64-pc-linux-gnu", "unreviewed compiler target");
  const auto input_facts = observed["required_input_facts"];
  check(input_facts["quantifier"] == "each record" &&
            input_facts["nul_byte_absent"] == true &&
            input_facts["max_bytes_inclusive"].asInt64() == 2147483647,
        "incorrect historical guard facts");
  const std::string vocab = "unllm.record-input-facts-fixture/1";
  auto required = object(
      {{"direction", "input"},
       {"argument_index", 0},
       {"quantifier", "each_record"},
       {"all_of",
        array({object({{"id", "nul_byte_absent"}, {"value", true}}),
               object({{"id", "max_bytes_inclusive"},
                       {"value", input_facts["max_bytes_inclusive"]}})})}});
  auto abi = object({{"symbol", chosen["call"]},
                     {"language", "C++17"},
                     {"target", triple},
                     {"library", "libstdc++"},
                     {"_GLIBCXX_USE_CXX11_ABI", 1},
                     {"int_bits", 32},
                     {"char_bits", 8},
                     {"arguments", array({"const std::vector<std::string>&"})},
                     {"result", "std::vector<std::string>"}});
  auto payload = object(
      {{"schema", "unllm.restricted-binding-fixture/1"},
       {"predicate_vocabulary", vocab},
       {"provider_id", chosen["brick"]},
       {"binding", chosen},
       {"binding_sha256", compact_hash(chosen)},
       {"implementation",
        object(
            {{"historical_bundle_sha256", observed["historical_bundle_sha256"]},
             {"header", "headers/" + chosen["header"].asString()},
             {"sha256", header_sha},
             {"source_archive_sha256", observed["source"]["archive_sha256"]}})},
       {"abi", abi},
       {"requires", required},
       {"mining_evidence",
        object({{"receipt_sha256", digest(work / "receipt.json")},
                {"fixture_sha256", observed["fixture_sha256"]},
                {"abi_witness_sha256", digest(witness)},
                {"compiler_sha256", observed["compiler_sha256"]},
                {"predicate_origin", "declared in extracted adapter guard"},
                {"size_evidence",
                 "observed predicate on synthetic sizes, no large allocation"},
                {"component_failure",
                 "std::invalid_argument on tested NUL records"},
                {"cli_observations", observed["cli_observations"]},
                {"unknown", observed["unassessed"]}})}});
  const auto interface =
      object({{"id", "unllm.restricted-binding-fixture"}, {"major", 1}});
  auto dir = work / ".restricted", final = work / "restricted-fixture";
  write(dir / "payload.json", payload);
  fs::copy_file(witness, dir / "abi-check.cpp");
  fs::copy_file(work / "receipt.json", dir / "evidence.json");
  auto files = tree_hashes(dir);
  auto m = object({{"schema", "unllm.restricted-fixture-manifest/1"},
                   {"interface", interface},
                   {"predicate_vocabulary", vocab},
                   {"payload", "payload.json"},
                   {"files", files},
                   {"unrestricted_control_sha256", control_pin},
                   {"purpose", "isolated opt-in consumer experiment"}});
  // Producer readback checks only identity and linkage. Independent
  // consumer parsing and logical discharge are evaluated in the main project.
  auto supported = [&](const J &manifest, const J &row) {
    return manifest["interface"] == interface &&
           manifest["predicate_vocabulary"] == vocab &&
           manifest["files"]["payload.json"] == sha(dump(row)) &&
           row["schema"] == "unllm.restricted-binding-fixture/1" &&
           row["binding"] == chosen &&
           row["binding_sha256"] == compact_hash(chosen) &&
           row["provider_id"] == chosen["brick"] &&
           row["implementation"]["sha256"] == header_sha && row["abi"] == abi &&
           dump(row["requires"]) == dump(required) &&
           row["mining_evidence"]["receipt_sha256"] == files["evidence.json"] &&
           row["mining_evidence"]["abi_witness_sha256"] ==
               files["abi-check.cpp"];
  };
  auto readback = load(dir / "payload.json");
  check(supported(m, readback), "fixture failed producer identity checks");
  auto bad = m;
  bad["interface"]["major"] = 2;
  check(!supported(bad, readback), "unrecognized interface accepted");
  bad = m;
  bad["files"]["payload.json"] = std::string(64, '0');
  check(!supported(bad, readback), "stale payload accepted");
  J controls = array({"unknown interface major", "stale payload digest"});
  for (const std::string change :
       {"provider", "abi", "argument", "quantifier", "missing predicate",
        "extra predicate", "upper bound", "implementation"}) {
    auto changed = readback;
    if (change == "provider")
      changed["provider_id"] = "x09";
    if (change == "abi")
      changed["abi"]["arguments"][0] = "std::vector<std::string>";
    if (change == "argument")
      changed["requires"]["argument_index"] = 1;
    if (change == "quantifier")
      changed["requires"]["quantifier"] = "some_record";
    if (change == "missing predicate")
      changed["requires"]["all_of"].resize(1);
    if (change == "extra predicate")
      changed["requires"]["all_of"].append(
          object({{"id", "nonempty"}, {"value", true}}));
    if (change == "upper bound")
      changed["requires"]["all_of"][1]["value"] = 2147483646;
    if (change == "implementation")
      changed["implementation"]["sha256"] = std::string(64, '0');
    auto resealed = m;
    resealed["files"]["payload.json"] = sha(dump(changed));
    check(!supported(resealed, changed),
          "changed producer descriptor accepted: " + change);
    controls.append(change);
  }
  write(work / "interchange-controls.json", controls);
  write(dir / "manifest.json", m);
  fs::rename(dir, final);
  run({"sync", "-f", final});
}
} // namespace xml_fixture
