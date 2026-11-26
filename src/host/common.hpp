#pragma once
#include <filesystem>
#include <json/json.h>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace miner {
namespace fs = std::filesystem;
using Path = fs::path;
using Strings = std::vector<std::string>;
using J = Json::Value;
extern const Path root, main_repo, volume, runs;
void check(bool condition, const std::string &message);
std::string bytes(const Path &file);
void save(const Path &file, const std::string &data);
J parse(const std::string &data);
J load(const Path &file);
J object(std::initializer_list<std::pair<std::string, J>> values = {});
J array(std::initializer_list<J> values = {});
J string_array(const Strings &values);
Strings strings(const J &values);
std::string dump(const J &value, bool pretty = true, bool ascii = false);
void write(const Path &file, const J &value);
std::string sha(const std::string &data);
std::string join(const Strings &parts, const std::string &delimiter);
std::string replace(std::string text, const std::string &from,
                    const std::string &to);
void append(Strings &to, const Strings &values);
Path project_file(const Path &base, const std::string &name);
struct Result {
  int status;
  std::string out, err;
};
struct RunOptions {
  Path cwd;
  std::string input;
  int seconds = 120;
  bool probe_limits = false;
  std::map<std::string, std::string> environment;
  Path output, errors;
};
Result execute(const Strings &args, const RunOptions &options = {});
std::string run(const Strings &args, const RunOptions &options = {});
Strings sandbox(const Path &exe, const Path &writable = {});
Strings command_args(const J &entry);
} // namespace miner

namespace miner {
void check_corpus(const Path &target);
Path fresh_run(const std::string &prefix);
} // namespace miner
