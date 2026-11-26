#include "probes.hpp"
#include "project.hpp"
#include <QTemporaryFile>
#include <algorithm>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cstdio>

namespace miner {
bool is_text(const std::string &type) {
  return type == "const char*" || type == "std::string" ||
         type == "const std::string&";
}
std::string source_id(const J &r) {
  J parts = array();
  for (const auto *k :
       {"source", "symbol", "signature", "definitions", "includes"})
    parts.append(r[k]);
  return "piece_" + sha(dump(parts, false, true)).substr(0, 24);
}
namespace {
std::string invoke(const J &r, std::string fn, Strings args, bool bind) {
  unsigned fields = bind ? r["receiver_fields"].size() : 0;
  if (fields) {
    check(fields <= args.size(), "missing receiver fields");
    auto pos = fn.rfind("::");
    check(pos != fn.npos, "unqualified receiver method");
    fn = fn.substr(0, pos) + "{" +
         join(Strings(args.begin(), args.begin() + fields), ", ") + "}." +
         fn.substr(pos + 2);
    args.erase(args.begin(), args.begin() + fields);
  }
  return fn + "(" + join(args, ", ") + ")";
}
std::string field(const std::string &type, const std::string &var) {
  if (type == "const char*")
    return "brick_probe::c_string(" + var + ")";
  if (is_text(type))
    return var;
  return "brick_probe::number<" + type + ">(" + var + ")";
}
std::string native_header(const J &r) {
  auto includes = strings(r["includes"]);
  std::set<std::string> sorted(includes.begin(), includes.end());
  sorted.insert("string");
  std::string headers;
  for (const auto &h : sorted)
    headers += "#include <" + h + ">\n";
  Strings params, args;
  for (const auto &t : r["parameters"]) {
    auto arg = "a" + std::to_string(args.size());
    params.push_back(t.asString() + " " + arg);
    args.push_back(arg);
  }
  auto id = r["id"].asString(), guard = "BRICK_MINER_" + id + "_HPP";
  std::transform(guard.begin(), guard.end(), guard.begin(),
                 [](unsigned char c) { return std::toupper(c); });
  return "#ifndef " + guard + "\n#define " + guard + "\n" + headers +
         "namespace brick_mined::" + id +
         " {\n"
         "namespace {\nnamespace upstream {\n" +
         r["definitions"].asString() + "\n}\ninline " +
         r["return_type"].asString() + " invoke(" + join(params, ", ") +
         ") {\n    return " +
         invoke(r, "upstream::" + r["callable"].asString(), args, true) +
         ";\n}\n}\n}\n#endif\n";
}
std::string driver(const J &r, const std::string &include,
                   const std::string &fn, bool original) {
  Strings args;
  std::string reads;
  for (const auto &t : r["parameters"]) {
    auto name = "f" + std::to_string(args.size());
    reads += "        const std::string " + name + " = next_field();\n";
    args.push_back(field(t.asString(), name));
  }
  auto result = invoke(r, fn, args, original);
  if (!is_text(r["return_type"].asString()))
    result = "std::to_string(" + result + ")";
  return include + "\n#include <iostream>\n" +
         bytes(root / "src/project_probe.hpp") + R"cpp(
std::string next_field() {
    std::string line;
    if (!std::getline(std::cin, line)) throw std::runtime_error("missing size");
    std::string field(brick_probe::number<unsigned int>(line), '\0');
    if (!std::cin.read(field.data(), field.size()) || std::cin.get() != '\n')
        throw std::runtime_error("truncated field");
    return field;
}

int main() try {
    std::string n;
    if (!std::getline(std::cin, n)) return 2;
    auto count = brick_probe::number<unsigned int>(n);
    for (unsigned int i = 0; i < count; ++i) {
)cpp" + reads +
         "        std::string result = " + result + R"cpp(;
        std::cout << result.size() << '\n' << result << '\n';
    }
    return 0;
} catch (const std::exception&) { return 2; }
)cpp";
}
Strings oracle_flags(const J &cmd, const Path &unit) {
  auto args = command_args(cmd);
  Strings flags;
  const std::set<std::string> skip = {"-c", "-MMD", "-MD", "-MP", "-MG", "--"};
  const std::set<std::string> pair = {"-MJ", "-o", "-MF", "-MT", "-MQ"};
  for (size_t i = 1; i < args.size(); ++i) {
    const auto &a = args[i];
    if (pair.contains(a)) {
      ++i;
      continue;
    }
    if (skip.contains(a) || a.starts_with("-MJ") || a.starts_with("-MF") ||
        a.starts_with("-MT") || a.starts_with("-MQ"))
      continue;
    if (!a.starts_with("-") &&
        fs::weakly_canonical(Path(cmd["directory"].asString()) / a) == unit)
      continue;
    flags.push_back(a);
  }
  return flags;
}
bool compile(const Strings &args, const Path &cwd, const Path &log) {
  auto p = execute(args, {.cwd = cwd});
  save(log,
       p.out + p.err + (p.status < 0 ? "Compile timed out or crashed\n" : ""));
  return !p.status;
}
std::string clients(const J &r, const Path &base, const J &units,
                    const Path &work) {
  const Strings flags = {"-ffunction-sections", "-fdata-sections",
                         "-fsanitize=undefined", "-fno-sanitize-recover=all"};
  auto source = r["source"].asString();
  auto unit = project_file(base, source);
  const auto &cmd = units[source];
  Strings objects;
  for (unsigned i = 0; i < r["oracle_units"].size(); ++i) {
    auto name = r["oracle_units"][i].asString();
    if (name == source)
      continue;
    const auto &dep = units[name];
    auto path = project_file(base, name);
    auto obj = work / ("original-dependency-" + std::to_string(i) + ".o");
    Strings args = {"clang++"};
    append(args, oracle_flags(dep, path));
    append(args, flags);
    append(args, {"-Dmain=brick_dependency_main_" + std::to_string(i), "-c",
                  path, "-o", obj});
    if (!compile(args, dep["directory"].asString(),
                 Path(obj).replace_extension(".log")))
      return "original dependency did not compile: " + name;
    objects.push_back(obj);
  }
  for (bool original : {false, true}) {
    std::string name = original ? "original" : "extracted";
    auto file = work / (name + ".cpp");
    auto include =
        original ? "#define main brick_original_program_main\n#include " +
                       dump(unit.string(), false, true) + "\n#undef main\n"
                 : "#include \"candidate.hpp\"";
    auto fn = original ? r.get("original_callable", r["callable"]).asString()
                       : "brick_mined::" + r["id"].asString() + "::invoke";
    save(file, driver(r, include, fn, original));
    Strings args = {"clang++"};
    append(args,
           original ? oracle_flags(cmd, unit) : Strings{"-std=c++17", "-O1"});
    append(args, flags);
    append(args, {"-Wl,--gc-sections", file});
    if (original && !objects.empty()) {
      append(args, {"-x", "none"});
      append(args, objects);
    }
    append(args, {"-o", work / (name + "-client")});
    if (!compile(args, cmd["directory"].asString(),
                 work / (name + "-build.log")))
      return name + " client did not compile or link";
  }
  return {};
}
J samples(const J &kinds) {
  if (kinds.empty())
    return array({array()});
  std::vector<J> columns;
  unsigned count = 0;
  for (const auto &type : kinds) {
    auto t = type.asString();
    auto values =
        is_text(t)
            ? array({"", "1", "plain", ".5", "A&b", "xyz", "0", "17", "é"})
        : t == "bool"               ? array({0, 1})
        : t.starts_with("unsigned") ? array({0, 1, 2, 7, 10, 15, 16, 31, 65})
                                    : array({0, 1, -1, 7, -2, 15, 16, 31, 65});
    count = std::max(count, values.size());
    columns.push_back(values);
  }
  J rows = array();
  for (unsigned i = 0; i < count; ++i) {
    J row = array();
    for (unsigned j = 0; j < columns.size(); ++j)
      row.append(columns[j][(i * (2 * j + 1) + j) % columns[j].size()]);
    rows.append(row);
  }
  return rows;
}
std::string frame(const J &value) {
  auto s = value.isBool() ? std::to_string(value.asBool()) : value.asString();
  return std::to_string(s.size()) + "\n" + s + "\n";
}
bool complete(const std::string &out, unsigned count) {
  size_t pos = 0;
  while (count--) {
    auto end = out.find('\n', pos);
    if (end == out.npos || end - pos > 11)
      return false;
    size_t size = 0;
    auto length = std::from_chars(out.data() + pos, out.data() + end, size);
    if (length.ec != std::errc{} || length.ptr != out.data() + end ||
        size >= out.size() - end - 1)
      return false;
    pos = end + 1 + size;
    if (out[pos++] != '\n')
      return false;
  }
  return pos == out.size();
}
std::optional<std::string> probe(const Path &exe, const J &rows) {
  std::string input = std::to_string(rows.size()) + "\n";
  for (const auto &row : rows)
    for (const auto &v : row)
      input += frame(v);
  QTemporaryFile out, err;
  check(out.open() && err.open(), "cannot open bounded probe output");
  auto p = execute(sandbox(exe), {.input = input,
                                  .seconds = 5,
                                  .probe_limits = true,
                                  .output = out.fileName().toStdString(),
                                  .errors = err.fileName().toStdString()});
  if (p.status)
    return {};
  return bytes(out.fileName().toStdString());
}
} // namespace
std::string verified_header(const Path &base, const J &r, const J &provenance) {
  auto defs = r["definitions"].asString();
  Strings notices;
  for (const auto &slice : r["slices"]) {
    auto raw = bytes(project_file(base, slice["source"].asString()));
    auto orig = raw.substr(slice["offset"].asUInt(), slice["length"].asUInt());
    check(!orig.empty() && defs.find(orig) != defs.npos,
          "extracted source slice differs from original");
  }
  if (!provenance.isNull())
    for (const auto &f : r["files"]) {
      auto name = f.asString();
      auto item = provenance["file_evidence"][name];
      check(!item.empty(), "missing source evidence: " + name);
      auto notice = item["notice"].asString();
      if (std::find(notices.begin(), notices.end(), notice) == notices.end())
        notices.push_back(notice);
    }
  return (notices.empty() ? "" : join(notices, "\n") + "\n") + native_header(r);
}
std::string row_header(const J &r, const std::string &id,
                       const std::string &code) {
  std::string cols;
  Strings args;
  for (const auto &type : r["parameters"]) {
    auto col = "column_" + std::to_string(args.size() + 1);
    cols += ", std::size_t " + col;
    args.push_back(field(type.asString(), "fields.at(" + col + ")"));
  }
  auto call = "brick_mined::" + r["id"].asString() + "::invoke(" +
              join(args, ", ") + ")";
  auto t = r["return_type"].asString();
  if (t == "unsigned long" || t == "unsigned long long")
    call = "([&]() -> std::int64_t { auto n = " + call +
           "; if (n > INT64_MAX) throw std::overflow_error(\"output exceeds "
           "INT64_MAX\"); return n; })()";
  std::string type = is_text(t) ? "std::string" : "std::int64_t";
  return "#pragma once\n" + code + "\n#include \"provider_types.hpp\"\n" +
         bytes(root / "src/project_probe.hpp") +
         "\nnamespace brick_mined {\nstatic inline std::vector<" + type + "> " +
         id + "(const brick_data::Rows& input" + cols +
         ") {\n    std::vector<" + type +
         "> result;\n    input([&](std::int64_t, const brick_data::Fields& "
         "fields) { result.push_back(" +
         call + "); });\n    return result;\n}\n}\n";
}
J exercise(const J &r, const Path &base, const J &units, const Path &work,
           const J &contracts) {
  auto issue = clients(r, base, units, work);
  if (!issue.empty())
    return object({{"status", "build failed"}, {"reason", issue}});
  auto rows = samples(r["parameters"]);
  auto original = work / "original-client",
       extracted = work / "extracted-client";
  auto observed = probe(original, rows), candidate = probe(extracted, rows);
  if (!observed || !complete(*observed, rows.size()) || observed != candidate)
    return object(
        {{"status", "probe failed"},
         {"reason",
          "original or isolated client failed, timed out or disagreed"}});
  J matched = array();
  for (const auto &c : contracts) {
    if (c["parameters"] != r["parameters"] ||
        c["return_type"] != r["return_type"])
      continue;
    auto cases = array();
    std::string expected;
    for (const auto &example : c["cases"]) {
      cases.append(example[0]);
      expected += frame(example[1]);
    }
    if (probe(original, cases) == expected &&
        probe(extracted, cases) == expected)
      matched.append(c["id"]);
  }
  return object({{"status", "matches original on probes"},
                 {"probe_count", rows.size()},
                 {"output_sha256", sha(*observed)},
                 {"contracts_matched", matched}});
}
void compare_lzip(const Path &package, const Path &dest) {
  Strings skipped;
  auto units =
      compilation_units(package / "source", package / "build", skipped);
  const std::map<std::string, std::pair<std::string, std::string>> bindings = {
      {"ascii_radix36_digit_value", {"main.cc", "CHECK_RADIX_VALUE"}},
      {"uppercase_hex_digit_byte", {"main.cc", "CHECK_HEX_DIGIT"}},
      {"number_option_prefix", {"arg_parser.cc", "CHECK_NUMBER_PREFIX"}}};
  check(fs::create_directory(dest), "comparison directory exists");
  J results = object();
  auto contracts = load(root / "contracts/project-lzip.json")["contracts"];
  for (const auto &c : contracts) {
    auto name = c["id"].asString();
    const auto &[file, selection] = bindings.at(name);
    auto unit = package / "source" / file;
    auto preamble = "#define main brick_original_program_main\n#include " +
                    dump(unit.string(), false, true) +
                    "\n#undef main\n#define " + selection + "\n#include " +
                    dump((root / "comparison/lzip.hpp").string(), false, true) +
                    "\n";
    auto cpp = dest / (name + ".cpp"), binary = dest / name;
    save(cpp, driver(c, preamble, "reference_binding", false));
    const auto &cmd = units[file];
    Strings args = {"clang++"};
    append(args, oracle_flags(cmd, unit));
    append(args, {"-ffunction-sections", "-fdata-sections",
                  "-fsanitize=undefined", "-fno-sanitize-recover=all",
                  "-Wl,--gc-sections", cpp, "-o", binary});
    auto start = std::chrono::steady_clock::now();
    check(compile(args, cmd["directory"].asString(), dest / (name + ".log")),
          "reference compilation failed");
    auto rows = array();
    std::string expected;
    for (const auto &example : c["cases"]) {
      rows.append(example[0]);
      expected += frame(example[1]);
    }
    check(probe(binary, rows) == expected, "reference mismatch for " + name);
    auto elapsed =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start)
            .count();
    results[name] = object(
        {{"cases", rows.size()}, {"passed", true}, {"seconds", elapsed}});
  }
  write(dest / "report.json", results);
}
} // namespace miner
