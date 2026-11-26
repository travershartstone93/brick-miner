#include "qualification.hpp"
#include <chrono>

namespace qualification {
double seconds() {
  return std::chrono::duration<double>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}
J argument_order(const Path &contracts, const std::string &name) {
  return parse(run({"jq", "-c", "--arg", "name", name,
                    ".[$name].args | keys_unsorted", contracts}));
}
J parameters(const J &keys, const J &values) {
  check(keys.size() == values.size(), "contract parameter count mismatch");
  auto args = object();
  for (unsigned i = 0; i < keys.size(); ++i)
    args[keys[i].asString()] = values[i];
  return args;
}
void plan_inputs(const Path &dir, const Files &files) {
  for (const auto &[name, data] : files)
    if (!name.starts_with("qualifications/") && !name.starts_with("runtime/") &&
        name != "SHA256SUMS")
      save(dir / name, data);
}
void reject(const J &emission) {
  check(emission["interpretation_errors"].empty(),
        "control interpretation error");
  for (const auto &p : emission["providers"])
    check(p["status"] != "complete", "control unexpectedly solved");
}
void publish(Files &stage, const Path &directory) {
  auto draft = directory / ".qualifying";
  check(!fs::exists(directory / "bundle") && fs::create_directory(draft),
        "existing staged generation");
  seal(stage);
  write_tree(draft, stage);
  run({"sha256sum", "--strict", "--quiet", "--check", "SHA256SUMS"},
      {.cwd = draft});
  fs::rename(draft, directory / "bundle");
}
} // namespace qualification
