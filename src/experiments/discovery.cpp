#include "pilots.hpp"
#include "original_adapter.hpp"
#include <pugixml.hpp>
#include <regex>
#include <sstream>

namespace pilots {
std::string first_line(const std::string &s) {
  return s.substr(0, s.find('\n'));
}
Strings original_keys(const Path &json) {
  return strings(parse(run({"jq", "-c", "keys_unsorted", json})));
}
Path historical_base() {
  auto pin = load(root / "contracts/inputs.json");
  auto base = volume / "unllm/experiments" / pin["base"].asString();
  check(sha(manifest(read_tree(base))) == pin["base_tree_sha256"].asString(),
        "historical release tree changed");
  return base;
}
void keyring(const Path &path) {
  run({"gpg", "--batch", "--dearmor", "--output", path,
       "/usr/share/pacman/keyrings/archlinux.gpg"});
}
std::string signature(const Path &arc, const Path &ring) {
  auto status = run({"gpgv", "--keyring", ring, "--status-fd", "1",
                     arc.string() + ".sig", arc});
  std::regex re("^\\[GNUPG:\\] VALIDSIG ([A-F0-9]+) ", std::regex::multiline);
  std::smatch m;
  check(std::regex_search(status, m, re), "missing package signature");
  return m[1];
}
namespace {
const Path cuda = "/opt/cuda/extras/cuda-gdb-13.3.73.src.tar.gz",
           xxh = "/usr/include/xxhash.h", xxlib = "/usr/lib/libxxhash.so.0.8.4",
           xxlic = "/usr/share/licenses/xxhash/LICENSE";
const std::string cuda_hash =
    "44bd47fab475c83704a9ebaa36d7c849e0783524921176b935ef94319f14537a";
void gdb_input(const Path &work) {
  check(first_line(run({"sha256sum", cuda})).starts_with(cuda_hash + " "),
        "CUDA-GDB pin changed");
  std::set<std::string> keep;
  std::istringstream names(
      R"files(COPYING3 install-sh config.guess config.sub config.rpath missing depcomp compile ar-lib
        bfd/development.sh gnulib/configure gnulib/config.in gnulib/Makefile.in
        gnulib/import/stdio.in.h gnulib/import/c++defs.h gnulib/import/arg-nonnull.h
        gnulib/import/warn-on-use.h gnulib/import/pathmax.h gnulib/import/Makefile.in gnulib/import/memmem.c gnulib/Makefile.gnulib.inc.in
        gdbsupport/configure gdbsupport/config.in gdbsupport/Makefile.in gdbsupport/xml-utils.cc gdbsupport/common-inferior.cc
        include/ansidecl.h include/libiberty.h include/hashtab.h include/partition.h include/safe-ctype.h include/filenames.h
        include/gdb/signals.h include/gdb/signals.def include/dwarf2.h include/dwarf2.def gdb/defs.h)files");
  std::string n;
  while (names >> n)
    keep.insert(n);
  std::istringstream listing(run({"bsdtar", "-tf", cuda}));
  Strings members;
  while (std::getline(listing, n)) {
    const std::string prefix = "cuda-gdb-13.3.73/";
    if (!n.starts_with(prefix))
      continue;
    Path f = n.substr(prefix.size());
    if (keep.contains(f) ||
        (f.parent_path() == "gdbsupport" &&
         (f.extension() == ".h" || f.extension() == ".def")))
      members.push_back(n);
  }
  auto source = work / "input";
  fs::create_directories(source);
  save(work / "gdb-inputs.list", join(members, "\n") + "\n");
  run({"bsdtar", "-xf", cuda, "--strip-components", "1", "-C", source, "-T",
       work / "gdb-inputs.list"});
  auto build = work / "build";
  std::string log;
  auto build_step = [&](const Strings &cmd, const Path &cwd) {
    auto p = execute(cmd, {.cwd = cwd});
    log += p.out + p.err;
    save(work / "gdb-build.log", log);
    check(!p.status, "GDB build failed: " + p.err);
  };
  for (std::string part : {"gnulib", "gdbsupport"}) {
    auto dir = build / part;
    fs::create_directories(dir);
    Strings cmd = {"../../input/" + part + "/configure",
                   "--disable-dependency-tracking"};
    if (part == "gdbsupport")
      append(cmd, {"CXX=clang++", "--disable-werror"});
    build_step(cmd, dir);
  }
  build_step({"make", "stdio.h"}, build / "gnulib/import");
  J db = array();
  for (std::string unit : {"xml-utils", "common-inferior"}) {
    auto dir = build / "gdbsupport";
    build_step(
        {"make", "V=1", "CXXFLAGS=-O2 -MJ " + unit + ".command", unit + ".o"},
        dir);
    auto raw = bytes(dir / (unit + ".command"));
    raw.erase(raw.find_last_not_of("\n\r \t") + 1);
    if (raw.ends_with(','))
      raw.pop_back();
    db.append(parse(raw));
  }
  write(work / "compile_commands.json", db);
}
J ast(const std::string &route, const Path &db, const Path &unit) {
  return parse(run({root / ".build/brick-mine", route, db, unit}));
}
} // namespace
Candidates original_pilot(const Path &work) {
  Candidates out;
  auto pins = load(root / "contracts/inputs.json");
  auto config = root / "contracts/tasks.json";
  auto tasks = load(config);
  Path archive = pins["xxhash_package"].asString();
  for (const auto &[file, pin] :
       std::map<Path, std::string>{{archive, "xxhash_package_sha256"},
                                   {xxh, "xxhash_header_sha256"},
                                   {xxlib, "xxhash_lib_sha256"},
                                   {xxlic, "xxhash_license_sha256"},
                                   {root / "tests/client.cpp", "client_sha256"},
                                   {config, "tasks_sha256"}})
    check(sha(bytes(file)) == pins[pin].asString(),
          "changed pilot input " + file.string());
  for (const auto &path : {xxh, xxlib, xxlic})
    check(run({"bsdtar", "-xOf", archive, path.relative_path()}) == bytes(path),
          "package payload mismatch");
  gdb_input(work);
  auto dir = work / "input/gdbsupport";
  out.info["line_xml_escape"] = ast("application", work, dir / "xml-utils.cc");
  out.rejected = ast("state", work, dir / "common-inferior.cc");
  auto app = out.info["line_xml_escape"];
  check(fs::canonical(work / "build/gdbsupport" / app["header"].asString()) ==
                dir / "xml-utils.h" &&
            app["closure"] ==
                array({"xml_escape_text_append", "xml_escape_text"}),
        "GDB header or closure changed");
  out.info["line_xxh32"] = discover_xxh32(work);
  for (const auto &k : original_keys(config)) {
    bool api = k == "line_xxh32";
    auto task = tasks[k], info = out.info[k];
    check(info["symbol"] == task["symbol"], "unexpected callable");
    J meta =
        api ? object({{"package", "extra/xxhash 0.8.4-1"},
                      {"upstream_release", "v0.8.4"},
                      {"acquired_archive", archive.filename().string()},
                      {"archive_sha256", pins["xxhash_package_sha256"]},
                      {"license", "BSD-2-Clause"},
                      {"pkgbuild_sha256", "22d08718706bc9b4994563c8ac84bc515381"
                                          "9a4d250d468b7b33c0d99481dac2"}})
            : object({{"package", "extra/cuda 13.3.1-1"},
                      {"upstream_release", "cuda-gdb-13.3.73"},
                      {"acquired_archive", cuda.filename().string()},
                      {"archive_sha256", cuda_hash},
                      {"license", "GPL-3.0-or-later"},
                      {"unit", array({"xml-utils.h", "xml-utils.cc"})}});
    for (const auto &file :
         api ? std::vector<Path>{xxh, xxlib, xxlic}
             : std::vector<Path>{dir / "xml-utils.h", dir / "xml-utils.cc"})
      meta["files"][file.filename().string()] = sha(bytes(file));
    meta["symbol"] = info["symbol"];
    meta["signature"] = info["signature"];
    meta["cxx_standard"] = "c++17";
    meta["pkg_config"] = task["packages"];
    meta["flags"] = string_array(flags(strings(task["packages"])));
    auto raw = bytes(api ? xxh : dir / "xml-utils.cc");
    out.headers[k] = render_original_adapter(k, info, raw);
    auto &e = out.evidence[k];
    e["discovery.json"] = dump(info);
    if (api) {
      e["LICENSE"] = bytes(xxlic);
      e["package-info.txt"] = run({"bsdtar", "-xOf", archive, ".PKGINFO"});
    } else {
      for (const auto *file : {"xml-utils.cc", "xml-utils.h"})
        e[std::string("original/") + file] = bytes(dir / file);
      e["COPYING3"] = bytes(work / "input/COPYING3");
      e["compile_command.json"] = dump(load(work / "compile_commands.json")[0]);
    }
    out.meta[k] = meta;
    task["id"] = k;
    out.tasks.append(task);
  }
  return out;
}
Candidates api_batch(const Path &work) {
  Candidates out;
  auto here = root / "batch2", config = here / "tasks.json";
  auto sources = load(here / "sources.json"), tasks = load(config);
  check(
      sha(bytes(config)) ==
          load(root / "evidence/batch2/before.json")["tasks_sha256"].asString(),
      "frozen tasks changed");
  auto ring = work / "archlinux.gpg";
  keyring(ring);
  for (const auto &k : original_keys(config)) {
    auto src = sources[k], task = tasks[k];
    auto pkgs = strings(task["packages"]);
    check(pkgs.size() == 1, "expected one API package");
    auto module = pkgs[0];
    Path archive = Path("/var/cache/pacman/pkg") / src["archive"].asString();
    check(sha(bytes(archive)) == src["archive_sha256"].asString(),
          "archive pin differs");
    src["signing_key"] = signature(archive, ring);
    auto buildinfo = run({"bsdtar", "-xOf", archive, ".BUILDINFO"});
    check(("\n" + buildinfo + "\n")
                  .find("\npkgbuild_sha256sum = " +
                        src["pkgbuild_sha256"].asString() + "\n") !=
              buildinfo.npos,
          "recipe pin mismatch");
    for (const auto &file : src["package_files"].getMemberNames())
      check(sha(bytes(file)) == src["package_files"][file].asString() &&
                run({"bsdtar", "-xOf", archive, Path(file).relative_path()}) ==
                    bytes(file),
            "changed package file " + file);
    Path license = src["license_path"].asString();
    check(sha(bytes(license)) == src["license_sha256"].asString(),
          "license changed");
    if (license.parent_path() ==
        Path("/usr/share/licenses") / src["package"].asString())
      check(run({"bsdtar", "-xOf", archive, license.relative_path()}) ==
                bytes(license),
            "package notice mismatch");
    Strings cmd = {"clang++", "-std=c++17"};
    append(cmd, command_args(object(
                    {{"command", run({"pkg-config", "--cflags", module})}})));
    append(cmd,
           {"-x", "c++", "-fsyntax-only", "-Xclang", "-ast-dump=json",
            "-Xclang", "-ast-dump-filter=" + src["family"].asString(), "-"});
    auto raw =
        run(cmd, {.input = "#include <" + src["include"].asString() + ">\n"});
    auto nodes = parse(run({"jq", "-s", "."}, {.input = raw}));
    J selected = object();
    std::optional<pugi::xml_document> gir;
    if (module == "libsoup-3.0") {
      gir.emplace();
      check(gir->load_file("/usr/share/gir-1.0/Soup-3.0.gir"),
            "cannot read GIR");
    }
    for (const auto &role : src["signatures"].getMemberNames()) {
      J found = array();
      for (const auto &n : nodes)
        if (n["kind"] == "FunctionDecl" && n["loc"]["file"] == src["header"] &&
            n["type"]["qualType"] == src["signatures"][role])
          found.append(object({{"name", n["name"]},
                               {"signature", n["type"]["qualType"]},
                               {"line", n["loc"]["line"]}}));
      check(found.size() == 1, "ambiguous or missing API binding " + role);
      auto f = found[0];
      if (gir) {
        auto matches = gir->select_nodes(
            ("//function[@c:identifier='" + f["name"].asString() + "']")
                .c_str());
        check(matches.size() == 1 &&
                  std::string(matches.first()
                                  .node()
                                  .child("return-value")
                                  .attribute("transfer-ownership")
                                  .value()) == "full",
              "unknown GIR return ownership");
        f["return_ownership"] = "full";
      }
      selected[role] = f;
    }
    auto templ = bytes(here / "adapters" / (k + ".hpp.in")), header = templ;
    std::regex token("@(\\w+)@");
    std::set<std::string> tokens;
    for (std::sregex_iterator it(templ.begin(), templ.end(), token), end;
         it != end; ++it)
      tokens.insert((*it)[1]);
    check(Strings(tokens.begin(), tokens.end()) == selected.getMemberNames(),
          "template role mismatch");
    for (const auto &role : tokens)
      header =
          replace(header, "@" + role + "@", selected[role]["name"].asString());
    src["bindings"] = selected;
    src["cxx_standard"] = "c++17";
    src["pkg_config_version"] =
        first_line(run({"pkg-config", "--modversion", module}));
    src["flags"] = string_array(flags(pkgs));
    src["template_sha256"] = sha(templ);
    auto origin = bytes(src["header"].asString());
    auto begin = origin.find("/*"), end = origin.find("*/", begin);
    check(begin != origin.npos && end != origin.npos, "header notice missing");
    out.evidence[k] = {
        {"LICENSE", bytes(license)},
        {"package-info.txt", run({"bsdtar", "-xOf", archive, ".PKGINFO"})},
        {"header-notice.txt", origin.substr(begin, end - begin + 2) + "\n"}};
    out.meta[k] = src;
    out.headers[k] = header;
    task["id"] = k;
    out.tasks.append(task);
  }
  return out;
}
} // namespace pilots
