#include "../../src/experiments/original_adapter.hpp"
#include "bundle.hpp"
#include "pilot.hpp"
#include <QByteArray>
#include <sstream>

namespace v5 {
namespace {
const std::string capability = "line_xxh32";
Path xxhash_client() { return root / "tests/client.cpp"; }
Path xxhash_tasks() { return root / "contracts/tasks.json"; }

void check_original_oracle(const J &requirements, const J &original) {
  J cases = array();
  for (const auto &test : original["cases"]) {
    if (test.isMember("error")) {
      auto failure = test["error"].asString();
      cases.append(object(
          {{"failure", failure == "missing" ? "missing-input" : failure}}));
    } else {
      auto input = QByteArray::fromStdString(test["input"].asString());
      cases.append(object(
          {{"input", input.toHex().toStdString()}, {"json", test["output"]}}));
    }
  }
  check(!cases.empty() && cases == requirements["cases"],
        "changed original XXH32 oracle");
}
} // namespace

void extract_xxhash(const Path &out) {
  const auto source = sources()["xxhash"];
  auto found = pilots::discover_xxh32(out);
  check(found["symbol"] == source["symbols"][0] &&
            found["signature"] ==
                "XXH32_hash_t (const void *, size_t, XXH32_hash_t)",
        "wrong xxHash API");
  auto raw = bytes("/usr/include/xxhash.h");
  const std::string example = "return XXH32(string, length, seed);";
  check(raw.find(example) != raw.npos, "single-shot usage example missing");
  auto adapter = pilots::render_original_adapter(capability, found, raw);
  check(sha(adapter) == source["adapter_sha256"].asString(),
        "XXH32 adapter differs from original pilot");
  save(out / (capability + ".hpp"), adapter);
  fs::create_directories(out / "notices");
  fs::copy_file("/usr/share/licenses/xxhash/LICENSE",
                out / "notices/xxhash-LICENSE");
  write(out / "xxhash-source.json",
        object({{"source_id", "xxhash"},
                {"revision", source["revision"]},
                {"archive_sha256", source["sha256"]},
                {"members", source["files"]},
                {"adapter_sha256", sha(adapter)},
                {"discovery", found},
                {"example", example},
                {"pkg_config", array({"libxxhash"})},
                {"flags", string_array(flags({"libxxhash"}))}}));
}

void qualify_xxhash(const Path &extracted, const Path &out) {
  const auto source = sources()["xxhash"];
  check(
      digest(xxhash_client()) == source["tests"]["client_sha256"].asString() &&
          digest(xxhash_tasks()) == source["tests"]["oracle_sha256"].asString(),
      "changed frozen XXH32 client or oracle");
  check_original_oracle(
      load(v5_root() / "fixtures/requirements.json")[capability],
      load(xxhash_tasks())[capability]);
  auto original = bytes(extracted / (capability + ".hpp"));
  auto compile = [&](const Path &header) {
    auto exe = probe_program(header.parent_path() / "client");
    Strings args = {"g++-15",
                    "-std=c++17",
                    "-O1",
                    "-include",
                    header,
                    "-DTEST_XXH32",
                    "-DAPPLY=brick_miner::line_xxh32",
                    xxhash_client(),
                    "-o",
                    exe};
    append(args, flags({"libxxhash"}));
    run(args);
    return exe;
  };
  auto header = out / "candidate.hpp";
  save(header, original);
  auto exe = compile(header);
  auto resolved = fs::canonical("/usr/lib/libxxhash.so.0");
  auto deps = run({"ldd", exe});
  // Compare the loader's actual resolution with the payload verified against
  // the signed package.
  std::istringstream lines(deps);
  bool linked = false;
  for (std::string line; std::getline(lines, line);) {
    std::istringstream fields(line);
    std::string name, arrow, loaded;
    if (fields >> name >> arrow >> loaded && name == "libxxhash.so.0" &&
        arrow == "=>")
      linked = fs::equivalent(loaded, resolved);
  }
  check(linked && source["files"][resolved.string()] == digest(resolved),
        "unexpected runtime xxHash library");
  RunOptions options{.seconds = 8, .probe_limits = true};
  auto argv = sandbox(exe);
  run(argv, options);
  run(argv, options);
  auto binary = digest(exe);
  J rejected = array();
  for (const auto &[wrong, reason] : std::map<std::string, std::string>{
           {"XXH32(s.data(), s.size(), 1)", "wrong seed"},
           {"XXH32(s.data(), std::char_traits<char>::length(s.c_str()), 0)",
            "truncated embedded NUL"}}) {
    auto mutant = replace(original, "XXH32(s.data(), s.size(), 0)", wrong);
    check(mutant != original, "missing mutation site");
    auto mutated_header = out / reason / "candidate.hpp";
    save(mutated_header, mutant);
    check(execute(sandbox(compile(mutated_header)), options).status == 1,
          "XXH32 mutant escaped: " + reason);
    rejected.append(reason);
  }
  write(out / "standalone.json",
        object({{capability,
                 object({{"outcome",
                          "Frozen vector and empty collection matched twice; "
                          "wrong seed and truncated record rejected."},
                         {"client_sha256", digest(xxhash_client())},
                         {"original_tasks_sha256", digest(xxhash_tasks())},
                         {"binary_sha256", binary},
                         {"runtime_library_sha256", digest(resolved)},
                         {"mutants_rejected", rejected}})}}));
}

void check_xxhash_witness(const Path &package, const J &port, const J &c,
                          const J &binding) {
  const auto e = package / "evidence";
  auto source = load(e / "xxhash-source.json"),
       standalone = load(e / "standalone.json")[capability];
  auto original = load(e / "xxhash-tasks.json")[capability];
  check_original_oracle(load(e / "requirements.json")[capability], original);
  check(c["capability_id"] == capability &&
            binding["call"] == "brick_miner::line_xxh32" &&
            binding["from"] == "strings" && binding["to"] == "integers" &&
            binding["args"].empty() &&
            source["source_id"] == c["source"]["id"] &&
            source["revision"] == c["source"]["revision"] &&
            source["archive_sha256"] == c["source"]["archive_sha256"] &&
            source["adapter_sha256"] == c["source"]["implementation_sha256"] &&
            source["discovery"]["symbol"] == "XXH32" &&
            source["pkg_config"] == array({"libxxhash"}) &&
            standalone["client_sha256"] == digest(e / "xxhash-client.cpp") &&
            standalone["original_tasks_sha256"] ==
                digest(e / "xxhash-tasks.json") &&
            standalone["runtime_library_sha256"] ==
                source["members"]["/usr/lib/libxxhash.so.0.8.4"] &&
            standalone["mutants_rejected"] ==
                array({"wrong seed", "truncated embedded NUL"}) &&
            port["evidence"] == array({"evidence/xxhash-source.json",
                                       "evidence/standalone.json#line_xxh32",
                                       "evidence/integration.json#line_xxh32"}),
        "stale XXH32 binding or evidence");
  const bool input = port["direction"] == "input";
  check(port["role"] ==
                (input ? "ordered_text_records" : "record_xxh32_seed_zero") &&
            port["role_status"] == "observed" &&
            port["unit_status"] == "not-applicable" && port["unit"].isNull() &&
            port["cardinality"] == (input ? "sequence" : "per_input_item") &&
            port["cardinality_status"] == "observed",
        "XXH32 semantic claim contradicts frozen witnesses");
}
} // namespace v5
