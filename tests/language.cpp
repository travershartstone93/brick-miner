#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <set>
#include <string>

namespace fs = std::filesystem;
namespace {
std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return s;
}
bool inspect(const fs::path &file) {
  auto ext = lower(file.extension().string()),
       name = lower(file.filename().string());
  const std::set<std::string> forbidden = {".py",  ".pyc", ".pyo",
                                           ".pyw", ".pyz", ".pyi"};
  if (name == "__pycache__" || forbidden.contains(ext))
    return false;
  if (!fs::is_regular_file(file))
    return true;
  std::ifstream stream(file, std::ios::binary);
  if (!stream)
    throw std::runtime_error("cannot inspect " + file.string());
  char prefix[1024];
  stream.read(prefix, sizeof(prefix));
  std::string first(prefix, stream.gcount());
  if (first.find('\0') != first.npos)
    return true;
  std::string shebang = lower(first.substr(0, first.find('\n')));
  if (shebang.starts_with("#!") && (shebang.find("python") != shebang.npos ||
                                    shebang.find("pypy") != shebang.npos))
    return false;
  const std::set<std::string> code = {
      "",    ".sh",   ".bash", ".cmake", ".mk",  ".c",
      ".h",  ".cc",   ".cpp",  ".cxx",   ".hh",  ".hpp",
      ".in", ".yaml", ".yml",  ".toml",  ".json"};
  using P = fs::perms;
  const auto executable = P::owner_exec | P::group_exec | P::others_exec;
  if (!code.contains(ext) && name != "cmakelists.txt" &&
      (fs::status(file).permissions() & executable) == P::none)
    return true;
  static const std::regex invocation(
      R"((^|[^[:alnum:]_])(python([23]([.][0-9]+)*)?|pypy[23]?)([[:space:]]+[-./$"'[:alnum:]]|["'][[:space:]]*[,}]))",
      std::regex::icase);
  static const std::regex api(
      R"((#[[:space:]]*include[[:space:]]*[<"]Python[.]h|\bPy_(Initialize|Run_[[:alnum:]_]+)[[:space:]]*\())");
  static const std::regex syntax(
      R"(^[[:space:]]*(from[[:space:]]+[[:alnum:]_.]+[[:space:]]+import[[:space:]]|def[[:space:]]+[[:alnum:]_]+\(.*\)[[:space:]]*(->.*)?:|if[[:space:]]+__name__[[:space:]]*==))");
  stream.clear();
  stream.seekg(0);
  std::string line;
  while (std::getline(stream, line)) {
    if (std::regex_search(line, invocation) || std::regex_search(line, api) ||
        std::regex_search(line, syntax))
      return false;
  }
  return true;
}
} // namespace
int main(int argc, char **argv) try {
  auto root = argc == 2 ? fs::canonical(argv[1]) : fs::current_path();
  size_t files = 0, violations = 0;
  for (fs::recursive_directory_iterator it(root), end; it != end; ++it) {
    auto file = it->path();
    if (file.filename() == ".git" && it->is_directory()) {
      it.disable_recursion_pending();
      continue;
    }
    if (it->is_regular_file())
      ++files;
    if (!inspect(file)) {
      std::cerr << file.lexically_relative(root)
                << " violates the project language rule\n";
      ++violations;
    }
  }
  std::cout << "Inspected " << files << " files, " << violations
            << " language violations\n";
  return violations ? 1 : 0;
} catch (const std::exception &e) {
  std::cerr << e.what() << '\n';
  return 2;
}
