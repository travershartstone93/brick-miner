#include "common.hpp"
#include "probes.hpp"
#include <algorithm>
#include <iostream>
#include <regex>
#include <sstream>

using namespace miner;
namespace {
std::string digest_file(const Path &file,
                        const std::string &program = "sha256sum") {
  auto out = run({program, file});
  return out.substr(0, out.find(' '));
}
Path acquire(const Path &dir, const Path &cache, const std::string &name,
             const std::string &expected, const Strings &urls) {
  auto dst = dir / name;
  if (!cache.empty())
    fs::copy_file(cache / name, dst);
  else
    for (const auto &url : urls) {
      auto p = execute({"curl", "-fsSL", "--retry", "1", "--max-time", "45",
                        "--proto", "=https", "--proto-redir", "=https",
                        "--output", dst, url});
      if (!p.status && digest_file(dst) == expected)
        break;
    }
  check(digest_file(dst) == expected,
        "acquired file differs from pinned digest: " + name);
  return dst;
}
Path collect(const Path &work, const Path &cache, const J &pin) {
  auto package = Path(pin["package"].asString()).filename().string();
  auto dir = work / "downloads";
  fs::create_directory(dir);
  auto arc = acquire(
      dir, cache, pin["archive"].asString(), pin["archive_sha256"].asString(),
      {pin["archive_url"].asString(), pin["archive_mirror"].asString()});
  auto recipe = acquire(dir, cache, package + ".PKGBUILD",
                        pin["pkgbuild_sha256"].asString(),
                        {pin["pkgbuild_url"].asString()});
  auto name = pin["binary_archive"].asString();
  Strings mirrors = {"https://geo.mirror.pkgbuild.com/extra/os/x86_64/" + name,
                     "https://archive.archlinux.org/packages/" +
                         package.substr(0, 1) + "/" + package + "/" + name};
  auto binary =
      acquire(dir, cache, name, pin["binary_sha256"].asString(), mirrors);
  for (auto &u : mirrors)
    u += ".sig";
  auto sig = acquire(dir, cache, name + ".sig",
                     pin["signature_sha256"].asString(), mirrors);
  auto ring = dir / "archlinux.gpg";
  run({"gpg", "--batch", "--dearmor", "--output", ring,
       "/usr/share/pacman/keyrings/archlinux.gpg"});
  auto verified =
      run({"gpgv", "--keyring", ring, "--status-fd", "1", sig, binary});
  check(verified.find("[GNUPG:] VALIDSIG " + pin["package_signer"].asString() +
                      " ") != verified.npos,
        "unexpected Arch signer");
  auto info = run({"bsdtar", "-xOf", binary, ".BUILDINFO"});
  check(("\n" + info + "\n")
                .find("\npkgbuild_sha256sum = " +
                      pin["pkgbuild_sha256"].asString() + "\n") != info.npos,
        "signed PKGBUILD hash mismatch");
  bool b2 = pin.isMember("archive_blake2b");
  auto hash = digest_file(arc, b2 ? "b2sum" : "sha256sum");
  auto text = bytes(recipe);
  std::smatch match;
  check(std::regex_search(text, match,
                          std::regex(std::string("^") +
                                         (b2 ? "b2sums" : "sha256sums") +
                                         "=\\(([^)]*)\\)",
                                     std::regex::multiline)),
        "missing source-checksum declaration in PKGBUILD");
  check(hash == pin[b2 ? "archive_blake2b" : "archive_sha256"].asString() &&
            std::regex_search(
                match[1].str(),
                std::regex("(^|[^0-9a-f])" + hash + "([^0-9a-f]|$)")),
        "upstream archive attestation mismatch");
  save(dir / "BUILDINFO", info);
  return arc;
}
void prepare(const Path &work, const Path &cache, const std::string &package) {
  check(package == "lzip" || package == "ocrad", "unknown approved package");
  check_corpus(work);
  fs::create_directories(work);
  check(fs::is_empty(work), "package directory must be empty");
  auto pin = load(root / "project-input" / (package + ".json"));
  auto archive = collect(work, cache, pin);
  run({"bsdtar", "-xf", archive, "-C", work});
  auto source = work / "source";
  fs::rename(work / (package + "-" + pin["upstream_version"].asString()),
             source);
  auto evidence = object();
  for (const auto &f : fs::recursive_directory_iterator(source)) {
    const std::set<std::string> suffixes = {".cc", ".cpp", ".cxx", ".h",
                                            ".hpp"};
    if (!f.is_regular_file() || !suffixes.contains(f.path().extension()))
      continue;
    auto raw = bytes(f.path());
    auto end = raw.find("*/");
    auto notice = raw.substr(0, end + 2);
    check(raw.starts_with("/*") && end != raw.npos &&
              notice.find("Copyright") != notice.npos,
          "missing source notice: " + f.path().string());
    evidence[f.path().lexically_relative(source).string()] =
        object({{"sha256", sha(raw)}, {"notice", notice}});
  }
  auto build = work / "build", fragments = build / "fragments";
  fs::create_directories(fragments);
  std::string log;
  for (const auto &cmd : std::vector<Strings>{
           {"../source/configure", "--prefix=/usr", "CXX=clang++",
            "CXXFLAGS=-O2 -gen-cdb-fragment-path fragments"},
           {"make", "-j4"},
           {"make", "check"}}) {
    auto res = execute(cmd, {.cwd = build, .environment = {{"LC_ALL", "C"}}});
    log += res.out + res.err;
    save(work / "build.log", log);
    check(!res.status, "upstream build or check failed; see " +
                           (work / "build.log").string());
  }
  std::vector<J> commands;
  for (const auto &f : fs::directory_iterator(fragments))
    if (f.path().extension() == ".json") {
      auto raw = bytes(f.path());
      raw.erase(raw.find_last_not_of(" \t\n\r") + 1);
      if (raw.ends_with(','))
        raw.pop_back();
      commands.push_back(parse(raw));
    }
  std::sort(commands.begin(), commands.end(), [](const J &a, const J &b) {
    return std::tie(a["file"], a["output"]) < std::tie(b["file"], b["output"]);
  });
  auto db = array(), units = array();
  std::set<Path> seen;
  for (const auto &cmd : commands) {
    auto dir = Path(cmd["directory"].asString());
    auto f = fs::canonical(dir / cmd["file"].asString());
    auto rel = f.lexically_relative(source);
    check(seen.insert(f).second && project_file(source, rel) == f &&
              fs::exists(dir / cmd["output"].asString()),
          "invalid compilation database");
    db.append(cmd);
    units.append(rel.string());
  }
  check(!db.empty(), "empty compilation database");
  write(build / "compile_commands.json", db);
  pin["file_evidence"] = evidence;
  pin["translation_units"] = units;
  pin["upstream_check"] = "passed";
  auto version = run({"clang++", "--version"});
  pin["compiler"] = version.substr(0, version.find('\n'));
  for (const auto &f : pin["license_files"])
    pin["license_sha256"][f.asString()] =
        sha(bytes(project_file(source, f.asString())));
  write(work / "provenance.json", pin);
}
} // namespace
int main(int argc, char **argv) try {
  check(argc >= 2, "expected prepare, compare or pilot");
  std::string action = argv[1];
  Strings positional;
  std::map<std::string, std::string> options;
  for (int i = 2; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--help") {
      std::cout << "prepare DIR [--package lzip|ocrad] [--cache DIR], compare "
                   "APPLICATION DIR, pilot [--cache DIR] [--baseline DIR "
                   "--manifest-sha256 HASH]\n";
      return 0;
    }
    if (arg.starts_with("--")) {
      check(i + 1 < argc && (arg == "--cache" || arg == "--package" ||
                             arg == "--baseline" || arg == "--manifest-sha256"),
            "unexpected option: " + arg);
      options[arg] = argv[++i];
    } else
      positional.push_back(arg);
  }
  Path cache =
      options.contains("--cache") ? fs::canonical(options["--cache"]) : Path{};
  if (action == "compare" && positional.size() == 2) {
    compare_lzip(fs::canonical(positional[0]), fs::absolute(positional[1]));
    return 0;
  }
  if (action == "prepare" && positional.size() == 1) {
    prepare(fs::weakly_canonical(fs::absolute(positional[0])), cache,
            options.contains("--package") ? options["--package"] : "lzip");
    return 0;
  }
  check(action == "pilot" && positional.empty(), "invalid package command");
  auto pair = fresh_run("project-mining");
  std::cout << pair << std::endl;
  J previous;
  for (int i = 1; i <= 2; ++i) {
    auto work = pair / ("run-" + std::to_string(i)), pkg = work / "application";
    prepare(pkg, cache, "lzip");
    cache = pkg / "downloads";
    Strings args = {root / "mine-project",
                    "--source",
                    pkg / "source",
                    "--database",
                    pkg / "build",
                    "--contracts",
                    root / "contracts/project-lzip.json",
                    "--provenance",
                    pkg / "provenance.json",
                    "--output",
                    work / "mined"};
    for (const auto *key : {"--baseline", "--manifest-sha256"})
      if (options.contains(key))
        append(args, {key, options[key]});
    run(args, {.seconds = 1800});
    compare_lzip(pkg, work / "comparison");
    auto report = load(work / "mined/report.json");
    report.removeMember("seconds");
    check(!report["bundle_manifest_sha256"].isNull() &&
              (i == 1 || previous == report),
          "package clean repeat failed");
    previous = report;
  }
  std::cout << "Verified repeat " << (pair / "run-1/mined/bundle") << " "
            << previous["bundle_manifest_sha256"].asString() << "\n";
  return 0;
} catch (const std::exception &e) {
  std::cerr << e.what() << "\n";
  return 1;
}
