#include "common.hpp"
#include <algorithm>
#include <iostream>

using namespace miner;
namespace {
const Path compiler = "/usr/bin/c++";
const std::string launcher = R"cpp(
#include <iostream>
#include <stdexcept>
void print(std::int64_t n) { std::cout << "int " << n << '\n'; }
void print(const std::vector<std::int64_t>& v) {
  std::cout << "ints";
  for (auto n : v) std::cout << ' ' << n;
  std::cout << '\n';
}
int main() {
  std::size_t count;
  if (!(std::cin >> count) || count > 65) return 3;
  std::vector<std::int64_t> values(count);
  for (auto &value : values)
    if (!(std::cin >> value)) return 3;
  try { print(algorithm(values)); }
  catch (const std::overflow_error&) { std::cout << "overflow\n"; }
  catch (const std::length_error&) { std::cout << "length\n"; }
}
)cpp";

J score_cases(const Path &exe, const J &cases) {
  auto verdicts = array();
  for (const auto &sample : cases) {
    const auto input = std::to_string(sample["input"].size()) + "\n" +
                       join(strings(sample["input"]), " ") + "\n";
    const auto result = execute(sandbox(exe), {.input = input, .seconds = 4,
                                               .probe_limits = true});
    verdicts.append(result.status == 0 && result.out == sample["expected"].asString());
  }
  return verdicts;
}

Result compile_probe(const Path &dir) {
  save(dir / "client.cpp", "#include \"emitted.cpp\"\n" + launcher);
  auto cmd = sandbox(compiler, dir);
  cmd.pop_back();
  append(cmd, {"--setenv", "PATH", "/usr/bin", compiler, "-std=c++20", "-O1",
               "-fsanitize=undefined", "-fno-sanitize-recover=all",
               "/case/client.cpp", "-o", "/case/probe"});
  return execute(cmd, {.seconds = 30});
}

bool all(const J &values) {
  if (values.empty())
    return false;
  for (const auto &value : values)
    if (value != true) return false;
  return true;
}

void verify_runtime(const Path &runtime, const std::string &pin) {
  const auto manifest = bytes(runtime / "SHA256SUMS");
  check(sha(manifest) == pin, "runtime manifest pin differs");
  Strings lines;
  for (const auto &entry : fs::directory_iterator(runtime)) {
    check(entry.symlink_status().type() == fs::file_type::regular,
          "runtime directory contains a non-regular file");
    const auto name = entry.path().filename().string();
    if (name != "SHA256SUMS")
      lines.push_back(sha(bytes(entry.path())) + "  " + name);
  }
  std::sort(lines.begin(), lines.end(), [](const auto &a, const auto &b) {
    return a.substr(66) < b.substr(66);
  });
  check(!lines.empty() && join(lines, "\n") + "\n" == manifest,
        "runtime files differ from manifest");
}

Strings builder_sandbox(const Path &builder, const Path &dir, const Path &runtime) {
  auto args = sandbox(builder, dir);
  args.pop_back();
  append(args, {"--ro-bind", runtime, "/runtime", "--setenv", "LD_LIBRARY_PATH",
                "/runtime", "/probe"});
  return args;
}
} // namespace

int main(int argc, char **argv) try {
  check(argc == 7,
        "usage: rsi-score SUITE SUITE_SHA256 BUILDER BUILDER_SHA256 RUNTIME RUNTIME_SHA256");
  const Path suite_path = fs::canonical(argv[1]), builder = fs::canonical(argv[3]);
  const Path runtime = fs::canonical(argv[5]);
  check_corpus(suite_path);
  check_corpus(builder);
  check_corpus(runtime);
  verify_runtime(runtime, argv[6]);
  const auto raw = bytes(suite_path);
  check(sha(raw) == argv[2] && sha(bytes(builder)) == argv[4], "suite or builder pin differs");
  const auto suite = parse(raw);
  const auto compiler_pin = sha(bytes(compiler));
  check(compiler_pin == suite["compiler_sha256"].asString(), "compiler pin differs");
  const auto catalog = fs::canonical(volume / "unllm/boilerplate/active");
  const auto manifest = catalog / "SHA256SUMS";
  const auto catalog_pin = sha(bytes(manifest));
  check(catalog_pin == suite["catalog_sha256"].asString(), "catalog pin differs");
  run({"sha256sum", "--strict", "--quiet", "-c", "SHA256SUMS"}, {.cwd = catalog});
  const auto work = fresh_run("v5-eval-private-rsi-score");
  std::cout << work.string() << std::endl;
  auto receipt = object({{"builder_sha256", argv[4]}, {"suite_sha256", argv[2]},
                        {"compiler_sha256", compiler_pin},
                        {"catalog_generation", catalog.lexically_relative(volume).string()},
                        {"catalog_sha256", catalog_pin},
                        {"runtime_sha256", argv[6]}});
  std::vector<Path> scratch_programs;
  const auto &seed = suite["seeded_control"];
  const auto control_dir = work / "seeded-control";
  save(control_dir / "emitted.cpp", seed["cpp"].asString());
  const auto control_build = compile_probe(control_dir);
  save(control_dir / "compiler-stderr.txt", control_build.err);
  check(control_build.status == 0, "seeded control failed to compile");
  const auto control = control_dir / "probe";
  const auto control_training = score_cases(control, seed["examples"]);
  const auto control_held = score_cases(control, seed["held"]);
  check(all(control_training) && !control_held.empty() && !all(control_held),
        "seeded control did not pass examples and fail held checks");
  receipt["seeded_control"] = object({{"example_verdicts", control_training},
                                      {"held_verdicts", control_held},
                                      {"program_sha256", sha(bytes(control))}});
  scratch_programs.push_back(control);

  for (unsigned i = 0; i < suite["tasks"].size(); ++i) {
    const auto &task = suite["tasks"][i];
    auto dir = work / ("task-" + std::to_string(i));
    write(dir / "contract.json", task["contract"]);
    auto cmd = builder_sandbox(builder, dir, runtime);
    append(cmd, {"synthesize", "/case/contract.json"});
    const auto result = execute(cmd, {.seconds = 125});
    save(dir / "reply.json", result.out);
    save(dir / "search-stderr.txt", result.err);
    check(result.status != 126 && result.status != 127,
          "could not start builder in sandbox: " + result.err);
    auto &row = receipt["tasks"][std::to_string(i)];
    row["partition"] = task["partition"];
    row["search_exit"] = result.status;
    row["verified"] = false;
    row["wrong_candidate"] = false;
    const auto reply = result.status == 0 ? parse(result.out) : object();
    row["status"] = result.status == 0 ? reply["status"] : J("search_failure");
    row["candidates"] = reply["candidates"];
    row["method"] = reply["method"];
    row["ir_sha256"] = reply["ir_sha256"];
    if (reply["status"] != "candidate")
      continue;

    write(dir / "ir.json", reply["ir"]);
    auto emit = builder_sandbox(builder, dir, runtime);
    append(emit, {"cpp", "/case/ir.json"});
    const auto emitted = execute(emit, {.seconds = 10});
    save(dir / "emitted.cpp", emitted.out);
    row["emitter_exit"] = emitted.status;
    if (emitted.status == 0) {
      auto exe = dir / "probe";
      const auto compiled = compile_probe(dir);
      row["compile_exit"] = compiled.status;
      save(dir / "compiler-stderr.txt", compiled.err);
      if (compiled.status == 0) {
        row["program_sha256"] = sha(bytes(exe));
        row["held_verdicts"] = score_cases(exe, task["held"]);
        row["verified"] = all(row["held_verdicts"]);
        if (row["verified"].asBool()) scratch_programs.push_back(exe);
      }
    }
    row["wrong_candidate"] = row["verified"] == false;
    write(dir / "verdict.json", row);
  }
  check(sha(bytes(builder)) == argv[4] && sha(bytes(compiler)) == compiler_pin &&
            sha(bytes(manifest)) == catalog_pin, "evaluation context changed");
  verify_runtime(runtime, argv[6]);
  write(work / "receipt.json", receipt);
  run({"sync", "-f", work / "receipt.json"});
  for (const auto &program : scratch_programs) fs::remove(program);
  std::cout << sha(bytes(work / "receipt.json")) << '\n';
  return 0;
} catch (const std::exception &e) {
  std::cerr << e.what() << '\n';
  return 1;
}
