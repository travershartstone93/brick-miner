#include "build.hpp"
#include "duckdb.hpp"
#include "pilots.hpp"
#include <QRegularExpression>
#include <algorithm>
#include <future>
#include <regex>
namespace shared {
namespace {
const Path tree = "sources/debug/usr/src/debug/duckdb/duckdb",
           sdk = "sources/package/usr";
std::string leaf(const std::string &name) {
  auto pos = name.rfind("::");
  return pos == name.npos ? name : name.substr(pos + 2);
}
void compile_units(const std::vector<Path> &units, const Path &dest,
                   const Strings &flags, bool deps, const std::string &prefix) {
  for (size_t i = 0; i < units.size(); i += 3) {
    std::vector<std::future<std::string>> jobs;
    for (size_t j = i; j < std::min(i + 3, units.size()); ++j) {
      auto obj = dest / (prefix + std::to_string(j) + ".o");
      Strings cmd = {"clang++"};
      append(cmd, flags);
      if (deps)
        append(cmd, {"-MMD", "-MF", Path(obj).replace_extension(".d")});
      append(cmd, {"-c", units[j], "-o", obj});
      jobs.push_back(std::async(std::launch::async,
                                [cmd] { return run(cmd, {.seconds = 240}); }));
    }
    for (auto &j : jobs)
      j.get();
  }
}
void link(const Build &b, const Path &dir, bool profile, const Strings &flags) {
  auto lib = "lib" + b.name + ".so";
  Strings cmd = {"clang++",
                 "-shared",
                 "-Wl,-z,defs",
                 "-Wl,-Bsymbolic-functions",
                 "-Wl,-rpath,$ORIGIN",
                 "-Wl,-soname," + lib};
  if (profile)
    cmd.push_back("-fprofile-instr-generate");
  std::set<Path> objs;
  for (const auto &e : fs::directory_iterator(dir))
    if (e.path().extension() == ".o")
      objs.insert(e.path());
  for (auto &obj : objs)
    cmd.push_back(obj);
  append(cmd, flags);
  append(cmd, {"-o", dir / lib});
  run(cmd);
}
void record(Build &b, const std::string &batch) {
  auto &m = b.metadata;
  m["headers"] = object();
  for (auto &[name, file] : b.originals)
    m["headers"][name] = sha(bytes(file));
  m["kernel_sha256"] = duckdb_miner::hash_file(Path(batch) / "release" /
                                               ("lib" + b.name + ".so"));
  m["adapter_sha256"] = sha(bytes(here() / batch / "adapter.cpp"));
}
} // namespace
J definition(const std::string &symbol, const Path &source,
             const Strings &flags) {
  Strings cmd = {"clang++"};
  append(cmd, flags);
  append(cmd, {"-fsyntax-only", "-Xclang", "-ast-dump=json", "-Xclang",
               "-ast-dump-filter=" + symbol, source});
  J matches = array();
  std::regex qualified("(^|[^A-Za-z_0-9])" + symbol + "(\\(|$)");
  for (const auto &node : duckdb_miner::ast_nodes(cmd)) {
    if (node["name"] != leaf(symbol))
      continue;
    auto &children = node["inner"];
    if (std::none_of(children.begin(), children.end(),
                     [](const auto &n) { return n["kind"] == "CompoundStmt"; }))
      continue;
    auto name = node.get("mangledName", symbol).asString();
    if (std::regex_search(pilots::first_line(run({"c++filt", name})),
                          qualified))
      matches.append(
          object({{"name", name}, {"type", node["type"]["qualType"]}}));
  }
  check(matches.size() == 1, "expected one compiled definition for " + symbol +
                                 " in " + source.string());
  return matches[0];
}
Build build_duckdb(const std::string &batch, const J &contracts) {
  Build b;
  b.name = "brick_shared_" + batch;
  Path work = batch;
  fs::create_directory(work);
  auto &m = b.metadata;
  std::map<std::string, QRegularExpression> patterns;
  for (const auto &c : contracts)
    for (const auto &s : c["symbols"]) {
      auto name = s.asString();
      patterns.emplace(
          name,
          QRegularExpression(
              "\\b" + QRegularExpression::escape(QString::fromStdString(name)) +
              R"(\s*\([^;{}]*\)\s*(?:const\s*)?\{)"));
    }
  for (const auto &e : fs::recursive_directory_iterator(tree))
    if (e.path().extension() == ".cpp") {
      auto raw = bytes(e.path());
      for (const auto &[name, regex] : patterns)
        if (raw.find(name) != raw.npos &&
            regex.match(QString::fromUtf8(raw)).hasMatch()) {
          check(!b.symbols.contains(name), "multiple definitions of " + name);
          b.symbols[name] = e.path();
        }
    }
  check(b.symbols.size() == patterns.size(), "missing source definition");
  std::set<Path> original;
  for (const auto &[sym, file] : b.symbols) {
    original.insert(file);
    m["source_units"][sym] = file.lexically_relative(tree).string();
  }
  Strings opts = {"-std=c++17", "-O1", "-g0", "-DNDEBUG", "-fPIC"};
  m["compile_flags"] = string_array(opts);
  opts.push_back("-ffile-prefix-map=" + fs::current_path().string() +
                 "=/mined-build");
  for (const auto &dir : std::vector<Path>{
           sdk / "include", tree / "third_party/utf8proc/include",
           tree / "third_party/yyjson/include", tree / "third_party/fast_float",
           tree / "third_party/fmt/include",
           tree / "extension/core_functions/include",
           first_batch() / "providers", here()})
    opts.push_back("-I" + dir.string());
  m["source_id"] = "069cc9f9b5";
  m["package"] = "extra/duckdb 1.5.6-1";
  m["compiler"] = pilots::first_line(run({"clang++", "--version"}));
  for (const auto &[sym, file] : b.symbols)
    m["definitions"][sym] = definition(sym, file, opts);
  std::vector<Path> units(original.begin(), original.end());
  units.push_back(here() / batch / "adapter.cpp");
  for (bool profile : {false, true}) {
    auto dir = work / (profile ? "coverage" : "release");
    fs::create_directory(dir);
    auto flags = opts;
    if (profile)
      append(flags, {"-fprofile-instr-generate", "-fcoverage-mapping"});
    compile_units(units, dir, flags, true, "unit");
    link(b, dir, profile, {"-L" + (sdk / "lib").string(), "-lduckdb"});
  }
  for (const auto &e : fs::directory_iterator(work / "release"))
    if (e.path().extension() == ".d")
      for (const Path f : duckdb_miner::dependencies(e.path())) {
        for (auto [base, prefix] : std::vector<std::pair<Path, std::string>>{
                 {tree, ""}, {sdk / "include", "sdk/"}}) {
          auto rel = f.lexically_relative(base);
          if (!rel.empty() && *rel.begin() != "..") {
            b.originals[prefix + rel.string()] = f;
            break;
          }
        }
      }
  record(b, batch);
  return b;
}
Build build_inputs(const J &contracts) {
  Build b;
  b.name = "brick_shared_inputs";
  Path work = "inputs", src = work / "source", usr = src / "usr";
  fs::create_directories(src);
  auto &m = b.metadata;
  auto pins = load(here() / work / "pins.json");
  const char *override_cache = getenv("BRICK_INPUT_CACHE");
  Path cache = override_cache ? override_cache : pins["cache"].asString();
  for (const auto &name : pins["files"].getMemberNames())
    check(duckdb_miner::hash_file(cache / name) ==
              pins["files"][name].asString(),
          "format source changed " + name);
  auto ring = fs::absolute(src / "arch.gpg");
  pilots::keyring(ring);
  for (const auto &name : pins["files"].getMemberNames())
    if (name.ends_with(".zst")) {
      auto recipe = name.substr(0, name.find('-')) + ".PKGBUILD";
      auto signer = pilots::signature(cache / name, ring);
      check(run({"bsdtar", "-xOf", cache / name, ".BUILDINFO"})
                    .find("pkgbuild_sha256sum = " +
                          pins["files"][recipe].asString() + "\n") !=
                std::string::npos,
            "unattested " + recipe);
      m["archives"][name] = object({{"signer", signer},
                                    {"sha256", pins["files"][name]},
                                    {"recipe_sha256", pins["files"][recipe]}});
      b.originals[recipe] = cache / recipe;
      run({"bsdtar", "-xf", cache / name, "-C", src, "usr"});
    }
  Path pugi;
  for (const auto *name : {"include", "share/licenses", "src/debug/pugixml"})
    for (const auto &e : fs::recursive_directory_iterator(usr / name))
      if (e.is_regular_file()) {
        b.originals[e.path().lexically_relative(src).string()] = e.path();
        if (e.path().filename() == "pugixml.cpp") {
          check(pugi.empty(), "ambiguous pugixml source");
          pugi = e.path();
        }
      }
  check(!pugi.empty(), "missing pugixml source");
  auto probe = here() / work / "probe.cpp";
  for (const auto &c : contracts)
    for (const auto &sym : c["symbols"]) {
      auto s = sym.asString();
      bool wrap = s.starts_with("__wrap_");
      b.symbols[s] = wrap ? probe : pugi;
      m["source_units"][s] = wrap ? "C API call probe: " + s.substr(7)
                                  : pugi.lexically_relative(src).string();
    }
  Strings flags = {"-std=c++17",
                   "-O1",
                   "-g0",
                   "-fPIC",
                   "-DNDEBUG",
                   "-I" + (usr / "include").string(),
                   "-I" + (first_batch() / "providers").string(),
                   "-I" + (first_batch() / "headers").string()};
  append(flags,
         command_args(object(
             {{"command", run({"pkg-config", "--cflags", "glib-2.0"})}})));
  flags.push_back("-ffile-prefix-map=" + fs::current_path().string() +
                  "=/mined-build");
  std::string xpath = "xpath_query::evaluate_node_set";
  m["definitions"][xpath] = definition(xpath, pugi, flags);
  Strings wraps;
  for (const auto &[sym, header, soname] :
       std::vector<std::tuple<std::string, std::string, std::string>>{
           {"sqlite3_step", "sqlite3.h", "libsqlite3.so.0"},
           {"archive_read_next_header", "archive.h", "libarchive.so.13"},
           {"uriParseSingleUriA", "uriparser/Uri.h", "liburiparser.so.1"}}) {
    auto nodes = duckdb_miner::ast_nodes(
        {"clang", "-x", "c", "-I" + (usr / "include").string(), "-fsyntax-only",
         "-include", header, "-Xclang", "-ast-dump=json", "-Xclang",
         "-ast-dump-filter=" + sym, "/dev/null"});
    auto matches = array();
    for (const auto &n : nodes)
      if (n["kind"] == "FunctionDecl" && n["name"] == sym)
        matches.append(n["type"]["qualType"]);
    auto exported = run({"nm", "-D", "--defined-only", usr / "lib" / soname});
    check(matches.size() == 1 &&
              std::regex_search(exported,
                                std::regex("\\b" + sym + "(?:@@[^\\s]+)?$",
                                           std::regex::multiline)),
          "missing format ABI " + sym);
    m["definitions"][sym] =
        object({{"header", header}, {"type", matches[0]}, {"soname", soname}});
    wraps.push_back("-Wl,--wrap=" + sym);
    duckdb_miner::copy(usr / "lib" / soname, Path("runtime/lib") / soname);
    m["runtime_sha256"][soname] =
        duckdb_miner::hash_file(Path("runtime/lib") / soname);
  }
  for (bool profile : {false, true}) {
    auto d = work / (profile ? "coverage" : "release");
    fs::create_directory(d);
    auto opts = flags;
    std::vector<Path> units = {pugi, here() / work / "adapter.cpp"};
    Strings libs;
    if (profile) {
      units.push_back(probe);
      append(opts, {"-fprofile-instr-generate", "-fcoverage-mapping"});
      libs = wraps;
    }
    compile_units(units, d, opts, false, "");
    append(libs, {"-L" + (usr / "lib").string(), "-lsqlite3", "-larchive",
                  "-luriparser", "-lglib-2.0"});
    link(b, d, profile, libs);
  }
  m["package"] = "extra/pugixml 1.16-1, core/sqlite 3.53.3-1, core/libarchive "
                 "3.8.9-1, extra/uriparser 1.0.2-1";
  m["source_id"] =
      "pugixml v1.16, sqlite-src-3530300, libarchive v3.8.9, uriparser 1.0.2";
  record(b, "inputs");
  return b;
}
} // namespace shared
