#include "duckdb.hpp"
#include <QRegularExpression>
#include <algorithm>
#include <cctype>
#include <future>
#include <regex>
#include <sstream>

namespace duckdb_miner {
namespace {
Path here() { return root / "row-ops/mined/source"; }
const Path tree = "debug/usr/src/debug/duckdb/duckdb", sdk = "package/usr";
} // namespace
void copy(const Path &from, const Path &to) {
  fs::create_directories(to.parent_path().empty() ? "." : to.parent_path());
  fs::copy_file(from, to, fs::copy_options::overwrite_existing);
}
std::string hash_file(const Path &path) {
  auto s = run({"sha256sum", path});
  return s.substr(0, s.find(' '));
}
J ast_nodes(const Strings &args) {
  auto raw = run(args);
  return parse(run({"jq", "-s", "."}, {.input = raw}));
}
Strings dependencies(const Path &file) {
  auto raw = replace(bytes(file), "\\\n", " ");
  auto pos = raw.find(':');
  check(pos != raw.npos, "malformed dependency list");
  return command_args(object({{"command", raw.substr(pos + 1)}}));
}
Path fetch(const Path &directory) {
  auto pin = load(here() / "arch.json");
  fs::create_directories(directory);
  for (const auto &name : pin["files"].getMemberNames()) {
    std::string repo =
        name.starts_with("duckdb-debug-") ? "extra-debug" : "extra";
    auto url = name == "PKGBUILD" ? "https://gitlab.archlinux.org/archlinux/"
                                    "packaging/packages/duckdb/-/raw/" +
                                        pin["version"].asString() + "/PKGBUILD"
                                  : "https://geo.mirror.pkgbuild.com/" + repo +
                                        "/os/x86_64/" + name;
    run({"curl", "-fsSL", "--retry", "2", "--proto", "=https", "--proto-redir",
         "=https", "-o", directory / name, url});
    check(hash_file(directory / name) == pin["files"][name].asString(),
          "download pin mismatch");
  }
  return directory;
}
void acquire(const Path &directory, const Path &cache) {
  auto pin = load(here() / "arch.json");
  fs::create_directories(directory / "original");
  for (const auto &name : pin["files"].getMemberNames())
    check(hash_file(cache / name) == pin["files"][name].asString(),
          "pinned archive mismatch: " + name);
  auto keys = directory / "archlinux.gpg";
  run({"gpg", "--batch", "--dearmor", "--output", keys,
       "/usr/share/pacman/keyrings/archlinux.gpg"});
  for (bool debug : {false, true}) {
    auto name = std::string("duckdb") + (debug ? "-debug-" : "-") +
                pin["version"].asString() + "-x86_64.pkg.tar.zst";
    auto archive = cache / name;
    auto sig = run({"gpgv", "--keyring", keys, "--status-fd", "1",
                    archive.string() + ".sig", archive});
    check(sig.find("[GNUPG:] VALIDSIG " + pin["signer"].asString() + " ") !=
              sig.npos,
          "invalid signing key");
    auto info = run({"bsdtar", "-xOf", archive, ".BUILDINFO"});
    check(("\n" + info + "\n")
                  .find("\npkgbuild_sha256sum = " +
                        pin["files"]["PKGBUILD"].asString() + "\n") !=
              info.npos,
          "unattested build recipe");
    auto dest = directory / (debug ? "debug" : "package");
    fs::create_directory(dest);
    run({"bsdtar", "-xf", archive, "-C", dest,
         debug ? "usr/src/debug/duckdb/duckdb" : "usr"},
        {.seconds = 300});
  }
  auto version =
      parse(run({directory / sdk / "bin/duckdb", "-json", "-c",
                 "SELECT library_version, source_id FROM pragma_version()"}));
  check(version == array({object({{"library_version", pin["tag"]},
                                  {"source_id", pin["source_id"]}})}),
        "wrong DuckDB runtime version");
  duckdb_miner::copy(cache / "PKGBUILD", directory / "original/PKGBUILD");
  duckdb_miner::copy(directory / sdk / "share/licenses/duckdb/LICENSE",
                     directory / "original/LICENSE");
}
namespace {
using Regex = QRegularExpression;
QString str(const std::string &s) { return QString::fromUtf8(s); }
std::string captured(const QRegularExpressionMatch &m, int i) {
  return m.captured(i).toStdString();
}
std::pair<J, J> descriptors() {
  auto factories = object(), aliases = object();
  std::set<std::string> types;
  Regex blocks(R"(^struct (\w+) \{(.*?)^\};)",
               Regex::MultilineOption | Regex::DotMatchesEverythingOption);
  Regex name(R"rx(\*Name = "(\w+)";)rx"),
      factory(R"(static (AggregateFunction(?:Set)?) (GetFunctions?)\(\);)"),
      alias(R"(using ALIAS = (\w+);)");
  for (const auto &e : fs::recursive_directory_iterator(tree)) {
    if (e.path().extension() != ".hpp")
      continue;
    auto raw = bytes(e.path());
    if (raw.find("static constexpr const char *Name") == raw.npos)
      continue;
    auto text = str(raw);
    for (const auto &match : blocks.globalMatch(text)) {
      auto body = match.captured(2);
      auto n = name.match(body);
      if (!n.hasMatch())
        continue;
      auto id = captured(n, 1), cls = captured(match, 1);
      auto f = factory.match(body), a = alias.match(body);
      if (f.hasMatch()) {
        check(!factories.isMember(id), "duplicate factory name");
        factories[id] =
            object({{"name", id},
                    {"cls", cls},
                    {"method", captured(f, 2)},
                    {"return_type", captured(f, 1)},
                    {"header", e.path().lexically_relative(tree).string()}});
        types.insert(cls);
      }
      if (a.hasMatch())
        aliases[id] = captured(a, 1);
    }
  }
  for (const auto &k : aliases.getMemberNames())
    if (!types.contains(aliases[k].asString()))
      aliases.removeMember(k);
  return {factories, aliases};
}
J discover(J &inventory, J &partition) {
  auto [factories, aliases] = descriptors();
  auto contracts = load(here() / "contracts.json"), selected = object(),
       tested = object();
  for (const auto &name : factories.getMemberNames()) {
    auto expr = "\"" + name + "\"(v ORDER BY i)";
    auto query = "WITH input(k,i,v) AS (VALUES " +
                 contracts["values"].asString() + ") SELECT cast(" + expr +
                 " AS VARCHAR) AS v, typeof(" + expr +
                 ") AS t FROM input GROUP BY k ORDER BY k";
    auto res = execute({sdk / "bin/duckdb", "-batch", "-json", "-init",
                        "/dev/null", ":memory:", "-c", query},
                       {.seconds = 10});
    if (res.status) {
      auto msg = res.err.substr(0, res.err.find('\n'));
      tested[name] = object({{"status", "incompatible with probe"},
                             {"error", msg.substr(0, msg.find(" Error:"))}});
      continue;
    }
    auto rows = parse(res.out), values = array(), matches = array();
    check(!rows.empty(), "empty aggregate result");
    for (const auto &row : rows)
      values.append(row["v"]);
    auto result = object({{"type", rows[0]["t"]}, {"values", values}});
    for (const auto &role : contracts["aggregates"].getMemberNames())
      if (result == contracts["aggregates"][role]) {
        check(!selected.isMember(role), "ambiguous aggregate for " + role);
        selected[role] = factories[name];
        matches.append(role);
      }
    tested[name] = object({{"result", result}, {"matched", matches}});
  }
  check(selected.getMemberNames() == contracts["aggregates"].getMemberNames(),
        "unmatched aggregate");
  inventory = object({{"tested", tested}, {"aliases", aliases}});
  std::set<std::tuple<std::string, std::string, std::string>> groups;
  std::set<std::string> sources;
  Regex methods(R"(^\w+\s+(\w+)::(\w+)\s*\()", Regex::MultilineOption);
  for (const auto *dir : {"src", "extension"})
    for (const auto &e : fs::recursive_directory_iterator(tree / dir)) {
      if (e.path().extension() != ".cpp")
        continue;
      auto text = str(bytes(e.path()));
      auto file = e.path().lexically_relative(tree).string();
      for (auto &f : selected) {
        auto cls = f["cls"].asString(), method = f["method"].asString(),
             type = f["return_type"].asString();
        if (text.contains(str(cls)) &&
            Regex(str("\\b" + type + "\\s+" + cls + "::" + method + "\\("))
                .match(text)
                .hasMatch()) {
          check(!f.isMember("source"), "duplicate aggregate definition " + cls);
          f["source"] = file;
        }
      }
      for (const auto &m : methods.globalMatch(text)) {
        auto method = m.captured(2).toLower();
        bool match = true;
        for (const auto &token : contracts["group_method_tokens"])
          match &= method.contains(str(token.asString()));
        if (match) {
          groups.emplace(file, captured(m, 1), captured(m, 2));
          sources.insert(file);
        }
      }
    }
  for (const auto &f : selected)
    check(f.isMember("source"), "missing aggregate definition");
  check(sources.size() == 1, "missing or ambiguous group implementation");
  partition = array();
  for (const auto &[s, c, m] : groups)
    partition.append(array({s, c, m}));
  return selected;
}
int loops(const J &node) {
  const std::set<std::string> kinds = {"ForStmt", "WhileStmt", "DoStmt",
                                       "CXXForRangeStmt"};
  int result = kinds.contains(node["kind"].asString());
  for (const auto &c : node["inner"])
    result += loops(c);
  return result;
}
std::pair<Path, J> partition_method(const J &groups, const Strings &flags) {
  Path src = groups[0][0].asString();
  auto cls = groups[0][1].asString(), prefix = groups[0][2].asString();
  for (const auto &g : groups) {
    auto method = g[2].asString();
    while (!method.starts_with(prefix))
      prefix.pop_back();
  }
  Strings cmd = {"clang++", "-std=c++17", "-DNDEBUG"};
  append(cmd, flags);
  append(cmd, {"-fsyntax-only", "-Xclang", "-ast-dump=json", "-Xclang",
               "-ast-dump-filter=" + cls + "::" + prefix, tree / src});
  auto methods = array();
  for (const auto &node : ast_nodes(cmd))
    if (node["kind"] == "CXXMethodDecl")
      for (const auto &body : node["inner"]) {
        if (body["kind"] == "CompoundStmt" && loops(body) > 0)
          methods.append(object({{"method", node["name"]},
                                 {"symbol", node["mangledName"]},
                                 {"loop_count", loops(body)}}));
      }
  check(methods.size() == 1, "ambiguous physical partition method");
  return {src, methods[0]};
}
Path compile_module(const Path &dir, const std::set<Path> &units,
                    const Strings &compiler, const Strings &includes,
                    bool instrumented) {
  fs::create_directory(dir);
  auto common = compiler;
  append(common, includes);
  auto cxx = common;
  if (instrumented)
    append(cxx, {"-fprofile-instr-generate", "-fcoverage-mapping"});
  Strings objects;
  for (const auto &unit : units) {
    auto obj = dir / (replace(unit.string(), "/", "_") + ".o");
    auto cmd = cxx;
    append(cmd, {"-MMD", "-MF", Path(obj).replace_extension(".d"), "-c", unit,
                 "-o", obj});
    run(cmd, {.seconds = 240});
    objects.push_back(obj);
  }
  auto binding = dir / "register.o";
  append(common, {"-MMD", "-MF", dir / "register.d", "-c", "register.cpp", "-o",
                  binding});
  run(common);
  Strings ld = {"clang++", "-shared"};
  if (instrumented)
    ld.push_back("-fprofile-instr-generate");
  auto lib = dir / "libbrick_row_kernels.so";
  append(ld, {"-Wl,-Bsymbolic-functions", "-Wl,-z,defs", "-Wl,-rpath,$ORIGIN",
              "-Wl,-soname,libbrick_row_kernels.so", "-o", lib});
  append(ld, objects);
  append(ld, {binding, "-L.", "-lduckdb"});
  run(ld);
  return lib;
}
} // namespace
void build_kernels(const Path &directory, const Path &cache) {
  check(!fs::exists(directory), "build directory already exists");
  fs::create_directories(directory);
  fs::current_path(directory);
  acquire(directory, cache);
  J inventory, groups;
  auto selected = discover(inventory, groups);
  std::set<Path> dirs = {sdk / "include", tree / "third_party/fmt/include"};
  for (const auto &f : selected) {
    auto h = f["header"].asString();
    auto i = h.find("/include/");
    check(i != h.npos, "source header outside include");
    if (!h.starts_with("src/include/"))
      dirs.insert(tree / h.substr(0, i + 8));
  }
  Strings inc;
  for (const auto &d : dirs)
    inc.push_back("-I" + d.string());
  auto [source, group] = partition_method(groups, inc);
  std::set<std::string> headers;
  Strings calls;
  std::set<Path> units = {tree / source};
  for (const auto &role : selected.getMemberNames()) {
    auto f = selected[role];
    auto h = f["header"].asString();
    headers.insert("#include \"" + h.substr(h.find("/include/") + 9) + "\"");
    auto factory =
        "duckdb::" + f["cls"].asString() + "::" + f["method"].asString() + "()";
    if (f["return_type"] == "AggregateFunction")
      factory = "duckdb::AggregateFunctionSet(" + factory + ")";
    calls.push_back("        install(context, \"" + role + "\", " + factory +
                    ");");
    units.insert(tree / f["source"].asString());
  }
  auto registration = replace(bytes(here() / "register.cpp.in"), "@HEADERS@",
                              join({headers.begin(), headers.end()}, "\n"));
  save("register.cpp",
       replace(replace(registration, "@INSTALL@", join(calls, "\n")),
               "@PARTITION@", group["symbol"].asString()));
  duckdb_miner::copy(sdk / "include/duckdb.h", "duckdb.h");
  duckdb_miner::copy(sdk / "lib/libduckdb.so", "libduckdb.so");
  duckdb_miner::copy(here() / "native_client.cpp", "native_client.cpp");
  Strings compiler = {"clang++",
                      "-std=c++17",
                      "-DNDEBUG",
                      "-O1",
                      "-fPIC",
                      "-ffile-prefix-map=" + directory.string() +
                          "=/brick-miner-row-build"};
  J originals = object(), coverage = object();
  std::map<std::string, Strings> tracked = {
      {source, {group["symbol"].asString()}}};
  for (const auto &[key, re] : std::map<std::string, std::string>{
           {"brick_count", "Operation|CountFlatLoop"},
           {"brick_first", "FirstFunctionILb0.*(Operation|Finalize)"},
           {"brick_last", "FirstFunctionILb1.*(Operation|Finalize)"},
           {"brick_list", "List(Update|Finalize|Scatter)"},
           {"brick_sum", "AddValues|SumSetOperation.*Combine"}})
    tracked[selected[key]["source"].asString()].push_back(re);
  for (bool profile : {false, true}) {
    Path dir = profile ? "instrumented" : "build";
    auto lib = compile_module(dir, units, compiler, inc, profile);
    if (!profile) {
      duckdb_miner::copy(lib, "libbrick_row_kernels.so");
      for (const auto &e : fs::directory_iterator(dir))
        if (e.path().extension() == ".d")
          for (const auto &f : dependencies(e.path())) {
            auto rel = Path(f).lexically_relative(sdk / "include");
            Path name;
            if (!rel.empty() && *rel.begin() != "..")
              name = Path("src/include") / rel;
            else {
              rel = Path(f).lexically_relative(tree);
              if (rel.empty() || *rel.begin() == "..")
                continue;
              name = rel;
            }
            duckdb_miner::copy(f, Path("original") / name);
            originals[name.string()] = hash_file(f);
          }
      for (const auto &f : selected) {
        Path h = f["header"].asString();
        if (!fs::exists("original" / h))
          duckdb_miner::copy(tree / h, "original" / h);
        check(bytes("original" / h) == bytes(tree / h),
              "SDK definition differs");
      }
    }
    duckdb_miner::copy("libduckdb.so", dir / "libduckdb.so");
    run({"clang++", "-DNDEBUG", "-std=c++17", "-I.", "native_client.cpp",
         "-L" + dir.string(), "-lbrick_row_kernels", "-lduckdb",
         "-Wl,-rpath,$ORIGIN", "-o", dir / "client"});
    RunOptions opt;
    opt.environment["LD_LIBRARY_PATH"] = fs::absolute(dir).string();
    if (profile)
      opt.environment["LLVM_PROFILE_FILE"] =
          fs::absolute(dir / "kernel.profraw").string();
    run({dir / "client"}, opt);
    if (!profile) {
      check(!fs::exists("default.profraw"), "release binary instrumented");
      continue;
    }
    run({"llvm-profdata", "merge", "-sparse", dir / "kernel.profraw", "-o",
         dir / "kernel.profdata"});
    Strings cmd = {"llvm-cov", "export", lib,
                   "-instr-profile=" + (dir / "kernel.profdata").string()};
    for (const auto &[src, t] : tracked)
      cmd.push_back(tree / src);
    auto data = parse(run(cmd))["data"][0];
    for (const auto &[src, patterns] : tracked) {
      std::set<std::string> hits;
      for (const auto &fn : data["functions"])
        if (fn["count"].asUInt64() &&
            fn["filenames"][0].asString().ends_with("/" + src))
          hits.insert(fn["name"].asString());
      for (const auto &pat : patterns) {
        std::regex expression(pat);
        check(std::any_of(hits.begin(), hits.end(),
                          [&](const auto &h) {
                            return std::regex_search(h, expression);
                          }),
              "missing native coverage for " + src + " " + pat);
      }
      coverage[src] = string_array({hits.begin(), hits.end()});
    }
  }
  J paths = array(), outputs = object();
  for (const auto &unit : units) {
    auto rel = unit.lexically_relative(tree);
    check(fs::is_regular_file("original" / rel), "original module missing");
    paths.append(rel.string());
  }
  for (const auto *f : {"duckdb.h", "libduckdb.so", "libbrick_row_kernels.so",
                        "register.cpp", "native_client.cpp"})
    outputs[f] = hash_file(f);
  compiler.back() = "-ffile-prefix-map=<build>=/brick-miner-row-build";
  append(compiler, inc);
  group["source"] = source.string();
  auto version = run({"clang++", "--version"});
  write(
      "provenance.json",
      object({{"source", load(here() / "arch.json")},
              {"license", object({{"file", "original/LICENSE"},
                                  {"sha256", hash_file("original/LICENSE")}})},
              {"upstream_files", originals},
              {"factories", selected},
              {"aggregate_inventory", inventory},
              {"physical_partition", group},
              {"native_checks",
               object({{"all_names_absent_before_registration", true},
                       {"production_query_passed", true},
                       {"instrumented_query_passed", true},
                       {"executed_source_functions", coverage}})},
              {"commands",
               object({{"compiler", version.substr(0, version.find('\n'))},
                       {"module_flags", string_array(compiler)},
                       {"translation_units", paths}})},
              {"outputs", outputs}}));
}
} // namespace duckdb_miner
