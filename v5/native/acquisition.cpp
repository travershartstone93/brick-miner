#include "link.hpp"
#include "pilot.hpp"
#include "probes.hpp"
#include "project.hpp"
#include <QCryptographicHash>
#include <QFile>
#include <algorithm>
#include <regex>
#include <sstream>

namespace v5 {
std::string compact_hash(const J &j) {
  Json::StreamWriterBuilder builder;
  builder["indentation"] = "";
  builder["emitUTF8"] = true;
  builder["commentStyle"] = "None";
  return sha(Json::writeString(builder, j));
}
std::string digest(const Path &file) {
  QFile in(QString::fromStdString(file.string()));
  check(in.open(QIODevice::ReadOnly), "cannot open " + file.string());
  QCryptographicHash hasher(QCryptographicHash::Sha256);
  check(hasher.addData(&in), "cannot hash " + file.string());
  return hasher.result().toHex().toStdString();
}
J tree_hashes(const Path &base) {
  J hashes = object();
  for (const auto &e : fs::recursive_directory_iterator(base)) {
    check(!e.is_symlink(), "unexpected symlink in staged package");
    if (e.is_regular_file())
      hashes[e.path().lexically_relative(base).generic_string()] =
          digest(e.path());
  }
  return hashes;
}
Path verified_base() {
  auto p = fs::canonical(volume / "unllm/boilerplate/active");
  check(digest(p / "SHA256SUMS") == base_hash, "active pilot baseline changed");
  run({"sha256sum", "--quiet", "--strict", "--check", "SHA256SUMS"},
      {.cwd = p});
  return p;
}
J sources() {
  auto result = object();
  auto registry = load(v5_root() / "sources.json");
  for (const auto &s : registry["sources"]) {
    auto id = s["id"].asString();
    check(!result.isMember(id), "duplicate source ID");
    result[id] = s;
  }
  return result;
}
void inventory(const Path &dest) {
  auto base = verified_base();
  J seen = object();
  auto registry = sources();
  for (const auto &id : registry.getMemberNames()) {
    const auto &s = registry[id];
    if (s["approval"] == "local-pilot") {
      check(!s["revision"].asString().empty() &&
                digest(s["archive"].asString()) == s["sha256"].asString(),
            "unverified pilot archive: " + id);
      for (const auto &file : s["files"].getMemberNames())
        check(digest(file) == s["files"][file].asString(),
              "changed source " + file);
    } else if (s.isMember("local_snapshot"))
      check(digest(s["local_snapshot"].asString()) == s["sha256"].asString(),
            "changed discovery metadata: " + id);
    seen[id] = object();
    for (const auto *k : {"approval", "revision", "sha256", "method"})
      seen[id][k] = s[k];
  }
  auto ring = dest / "archlinux.gpg";
  run({"gpg", "--batch", "--dearmor", "--output", ring,
       "/usr/share/pacman/keyrings/archlinux.gpg"});
  J signers = object();
  for (const auto &s : registry) {
    if (s["method"] != "signed-local-package")
      continue;
    auto arc = s["archive"].asString();
    auto status =
        run({"gpgv", "--keyring", ring, "--status-fd", "1", arc + ".sig", arc});
    std::regex signature("^\\[GNUPG:\\] VALIDSIG (\\w+) ",
                         std::regex::multiline);
    std::smatch sig;
    check(std::regex_search(status, sig, signature),
          "missing valid package signature: " + arc);
    signers[s["id"].asString()] = sig[1].str();
    for (const auto &file : s["files"].getMemberNames())
      check(sha(run({"bsdtar", "-xOf", arc, file.substr(1)})) ==
                s["files"][file].asString(),
            "package member mismatch: " + file);
  }
  auto lzip = registry["gnu-lzip"];
  for (const auto &file : lzip["files"].getMemberNames())
    check(sha(run({"bsdtar", "-xOf", lzip["archive"].asString(),
                   "lzip-1.26/" + Path(file).filename().string()})) ==
              lzip["files"][file].asString(),
          "lzip member mismatch");
  write(
      dest / "inputs.json",
      object({{"sources", seen},
              {"signer", signers["gcc-libstdcxx"]},
              {"package_signers", signers},
              {"base", base.string()},
              {"base_sha256", base_hash},
              {"compiler", digest("/usr/bin/g++-15")},
              {"compilation_database",
               digest(sample_application() / "build/compile_commands.json")}}));
}
namespace {
void names(const J &node, std::set<std::string> &out) {
  out.insert(node["name"].asString());
  out.insert(node["referencedDecl"]["name"].asString());
  for (const auto &c : node["inner"])
    names(c, out);
}
void calls(const J &node, const std::string &raw, J &hits) {
  if (node["kind"] == "CXXMemberCallExpr" || node["kind"] == "CallExpr") {
    std::set<std::string> used;
    names(node, used);
    std::string action;
    if (used.contains("member_vector")) {
      if (used.contains("reverse") && used.contains("begin") &&
          used.contains("end"))
        action = "reverse";
      else if (used.contains("push_back"))
        action = "push_back";
    }
    if (!action.empty()) {
      auto first = node["range"]["begin"]["offset"].asUInt(),
           last = node["range"]["end"]["offset"].asUInt() +
                  node["range"]["end"]["tokLen"].asUInt();
      check(last >= first && last <= raw.size(), "invalid caller extent");
      hits.append(
          object({{"operation", action},
                  {"offset", first},
                  {"length", last - first},
                  {"excerpt_sha256", sha(raw.substr(first, last - first))}}));
    }
  }
  for (const auto &c : node["inner"])
    calls(c, raw, hits);
}
J sketch(const Path &unit, const J &cmd) {
  auto args = command_args(cmd);
  auto output = std::find(args.begin(), args.end(), "-o");
  check(output != args.end() && std::next(output) != args.end(),
        "missing compile output argument");
  args.erase(output, std::next(output, 2));
  std::erase(args, "-c");
  append(args, {"-fsyntax-only", "-Xclang", "-ast-dump=json", "-Xclang",
                "-ast-dump-filter=Lzip_index::Lzip_index"});
  auto docs = run(args, {.cwd = cmd["directory"].asString()});
  // Clang emits a stream of JSON objects for overloads. jq slurps them without
  // interpreting source text.
  auto nodes = parse(run({"jq", "-s", "."}, {.input = docs}));
  auto raw = bytes(unit);
  J hits = array();
  for (const auto &n : nodes)
    calls(n, raw, hits);
  check(hits.size() == 2 && hits[0]["operation"] == "push_back" &&
            hits[1]["operation"] == "reverse",
        "caller pattern changed");
  return object(
      {{"schema", "unllm.usage-sketch/1"},
       {"id", "append-backward-then-reverse"},
       {"source", "gnu-lzip"},
       {"file", unit.filename().string()},
       {"file_sha256", sha(raw)},
       {"callable", "Lzip_index::Lzip_index"},
       {"observed", hits},
       {"interpretation", "Accumulate in backward traversal order and reverse "
                          "the completed range."},
       {"scope", "observed same-receiver sequence in one constructor; "
                 "exceptional paths and full index semantics unknown"}});
}
} // namespace
void extract(const Path &dest, const std::string &route) {
  auto notices = dest / "notices";
  fs::create_directories(notices);
  auto source = sample_application() / "source";
  if (route == "api") {
    auto gcc = sources()["gcc-libstdcxx"];
    auto symbol = gcc["symbols"][0].asString();
    std::string raw;
    for (const auto &f : gcc["files"].getMemberNames())
      if (f.ends_with("/stl_algo.h"))
        raw = bytes(f);
    check(raw.find("reverse(_BidirectionalIterator __first") != raw.npos,
          "missing public reverse API");
    save(dest / (api + ".hpp"),
         replace(bytes(v5_root() / "fixtures/reverse.hpp.in"), "@symbol@",
                 symbol));
    save(notices / "stl_algo.txt", raw.substr(0, raw.find("#ifndef")));
    fs::copy_file("/usr/share/licenses/gcc/RUNTIME.LIBRARY.EXCEPTION",
                  notices / "COPYING.RUNTIME");
    fs::copy_file("/usr/share/licenses/spdx/GPL-3.0-or-later.txt",
                  notices / "COPYING3");
    write(dest / "api.json",
          object({{"symbol", symbol},
                  {"template_sha256",
                   digest(v5_root() / "fixtures/reverse.hpp.in")}}));
    return;
  }
  Strings skipped;
  auto units =
      compilation_units(source, sample_application() / "build", skipped);
  if (route == "composition") {
    write(dest / "sketch.json",
          sketch(source / "lzip_index.cc", units["lzip_index.cc"]));
    return;
  }
  check(route == "direct", "unknown reviewed route");
  auto records =
      scan(source, sample_application() / "build", {"arg_parser.cc"});
  J chosen;
  for (auto &r : records)
    if (r["status"] != "rejected" &&
        source_id(r) == "piece_936f6c9a9708840c0fb1372c") {
      check(chosen.isNull(), "ambiguous source");
      chosen = r;
    }
  check(!chosen.isNull(), "pinned Lzip candidate missing");
  chosen["id"] = source_id(chosen);
  auto provenance =
      source_pins(source, sample_application() / "provenance.json");
  auto header =
      row_header(chosen, direct, verified_header(source, chosen, provenance));
  check(sha(header) ==
            "44f3fb45a148317346af936b53539288560f278c0343a38b54d27262cd372a57",
        "legacy provider changed");
  save(dest / (direct + ".hpp"), header);
  save(notices / "arg_parser.txt",
       provenance["file_evidence"]["arg_parser.cc"]["notice"].asString());
  write(dest / "extraction.json",
        object({{"direct_slices", chosen["slices"]},
                {"closure", chosen["closure"]},
                {"clang_candidates", records.size()},
                {"selected", chosen["id"]},
                {"direct_command", units["arg_parser.cc"]["arguments"]}}));
}
namespace {
std::string trimmed(std::string s) {
  auto pos = s.find_last_not_of(" \r\n\t");
  return pos == s.npos ? "" : s.substr(0, pos + 1);
}
} // namespace
void standalone(const Path &extracted, const Path &dest,
                const std::string &route) {
  J results = object();
  if (route == "composition") {
    auto client = v5_root() / "fixtures/sketch-client.cpp";
    auto exe = probe_program(dest / "sketch-client");
    run({"g++-15", "-std=c++17", client, "-o", exe});
    auto output = run(sandbox(exe), {.seconds = 8, .probe_limits = true});
    results["sketch"] = object(
        {{"observation", load(extracted / "sketch.json")["id"]},
         {"client_sha256", digest(client)},
         {"result", trimmed(output)},
         {"scope", "one authored transfer probe, no planner consumption"}});
  } else {
    check(route == "api" || route == "direct", "unknown standalone route");
    bool is_api = route == "api";
    auto key = is_api ? api : direct;
    auto work = dest / key;
    fs::create_directories(work);
    auto alias = work / (is_api ? "provider.hpp" : "subject.hpp"),
         dep = work / "dependencies.d";
    auto client = is_api ? v5_root() / "fixtures/reverse-client.cpp"
                         : root / "one-brick/check.cpp";
    auto exe = probe_program(work / "client");
    fs::copy_file(extracted / (key + ".hpp"), alias);
    run({"g++-15", "-std=c++17", "-O1", "-MD", "-MF", dep, "-I", work, "-I",
         verified_base() / "providers", client, "-o", exe});
    auto out = run(sandbox(exe), {.seconds = 8, .probe_limits = true});
    auto raw = replace(bytes(dep), "\\\n", " ");
    auto pos = raw.find(": ");
    check(pos != raw.npos, "malformed dependencies");
    auto deps = command_args(object({{"command", raw.substr(pos + 2)}}));
    J system = object();
    bool uses_reverse = false;
    for (const auto &path : deps)
      if (path.starts_with("/usr/")) {
        system[path] = digest(path);
        uses_reverse |= path.ends_with("/bits/stl_algo.h");
      }
    if (is_api) {
      check(uses_reverse, "public header not used");
      auto correct = bytes(alias);
      auto mutated_header = work / "identity/provider.hpp";
      save(mutated_header,
           replace(correct, "std::reverse(record.begin(), record.end());",
                   "(void)record;"));
      auto mutant = probe_program(work / "mutant");
      run({"g++-15", "-std=c++17", "-I", mutated_header.parent_path(), client,
           "-o", mutant});
      auto mutated =
          execute(sandbox(mutant), {.seconds = 8, .probe_limits = true});
      check(mutated.status == 1, "identity mutant escaped independent oracle");
    }
    results[key] =
        object({{"client_sha256", digest(client)},
                {"outcome", trimmed(out)},
                {"system_dependencies", system},
                {"mutation", is_api ? "identity rejected" : "not exercised"}});
  }
  write(dest / "standalone.json", results);
}
} // namespace v5
