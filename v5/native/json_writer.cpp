#include "bundle.hpp"
#include "migration_reports.hpp"
#include "pilot.hpp"
#include <QByteArray>
#include <QString>
#include <algorithm>
#include <sstream>

namespace v5 {
namespace {
QByteArray raw(const J &hex) {
  const auto ascii = QByteArray::fromStdString(hex.asString());
  auto bytes = QByteArray::fromHex(ascii);
  check(hex.isString() && bytes.toHex() == ascii, "invalid hexadecimal oracle");
  return bytes;
}
J observe(const Path &exe, const Path &dir, const J &c, bool ordinary,
          bool existing) {
  fs::create_directories(dir);
  const auto fault = c["fault"].asString();
  std::string target = fault == "missing-parent" ? "missing/out" : "out";
  if (fault == "directory")
    save(dir / "out/member", "sentinel");
  else if (existing)
    save(dir / target, "old");
  auto cmd = sandbox(exe, dir);
  if (fault == "limit-8")
    cmd.insert(cmd.end() - 1, {"/usr/bin/env", "--ignore-signal=XFSZ",
                               "/usr/bin/prlimit", "--fsize=8:8", "--"});
  J expected_records = array();
  Strings records;
  for (const auto &r : c["records"]) {
    records.push_back(raw(r).toStdString());
    expected_records.append(records.back());
  }
  if (ordinary) {
    // The ordinary transform reverses the input. Both arms are scored against
    // the same explicitly listed output records and independent byte oracle.
    std::string input;
    for (auto i = records.rbegin(); i != records.rend(); ++i)
      input += *i + "\n";
    save(dir / "in", input);
    cmd.push_back("in");
  }
  cmd.push_back(target);
  if (!ordinary)
    for (const auto &r : c["records"])
      cmd.push_back(r.asString());
  auto expected_tree = tree_hashes(dir);
  auto result = execute(cmd, {.seconds = 8, .probe_limits = true});
  check(result.status == (fault.empty() ? 0 : 1) &&
            (ordinary || fault.empty() || result.out == "ios_base::failure"),
        "unexpected writer result " + c["name"].asString() + ": " + result.err);
  J obs = object(
      {{"case", c["name"]},
       {"destination_before", fault == "directory" ? "directory"
                              : existing           ? "regular"
                                                   : "absent"},
       {"exit", result.status},
       {"direct_exception", ordinary || fault.empty() ? J() : J(result.out)}});
  if (c.isMember("expected_hex")) {
    auto out = QByteArray::fromStdString(bytes(dir / target));
    check(out == raw(c["expected_hex"]), "writer bytes differ for " +
                                             c["name"].asString() + ": " +
                                             out.toHex().toStdString());
    bool utf8 = QString::fromUtf8(out).toUtf8() == out;
    check(utf8 != c.get("outside_utf8", false).asBool(),
          "unexpected UTF-8 verdict");
    if (fault.empty() && utf8)
      check(parse(out.toStdString()) == expected_records,
            "independent JSON round trip failed");
    expected_tree[target] = sha(out.toStdString());
    obs["output_hex"] = out.toHex().toStdString();
    obs["utf8_valid"] = utf8;
  } else if (fault == "missing-parent") {
    check(!fs::exists(dir / "missing"), "writer created a missing parent");
  } else
    check(fault == "directory" && fs::is_directory(dir / target),
          "wrong directory effect");
  check(tree_hashes(dir) == expected_tree, "unexpected file effects");
  return obs;
}
J suite(const Path &exe, const Path &dir, const J &cases, bool ordinary) {
  J observations = array();
  for (const auto &c : cases) {
    if (ordinary && c.get("direct_only", false).asBool())
      continue;
    for (bool existing : {false, true}) {
      auto fault = c["fault"].asString();
      if (existing && (fault == "directory" || fault == "missing-parent"))
        continue;
      observations.append(observe(
          exe, dir / c["name"].asString() / (existing ? "existing" : "absent"),
          c, ordinary, existing));
    }
  }
  return observations;
}
J trials(const Path &work, const Path &bundle, const J &binding,
         const J &cases) {
  auto task = [](const std::string &outcome) {
    return spec_for("text/plain", "application/json",
                    object({{"outcome", outcome},
                            {"parameters", object()},
                            {"uncovered", array()}}));
  };
  auto spec = task("reverse_order");
  const auto chain = array({"read_lines", "reverse_order", "write_texts"});
  J receipt = object({{"spec", spec}, {"bindings", chain}});
  for (const std::string arm : {"baseline", "staged"}) {
    auto dir = work / arm, exe = work / ".test-programs" / arm;
    auto provider = complete_file_plan(
        arm == "staged" ? bundle : verified_base(), dir, spec, chain);
    build_emission(provider, dir, exe, "g++-15");
    receipt[arm] = object({{"program_sha256", digest(exe)},
                           {"emission_sha256", digest(dir / "component.hpp")},
                           {"input_headers", tree_hashes(dir / "bricks")},
                           {"cases", suite(exe, dir, cases, true)}});
  }
  auto reduced = work / "control-bundle";
  write_tree(reduced, remove_binding(read_tree(bundle), binding));
  const auto emitted = plan(reduced, spec, work / "removed");
  const auto &p = emitted["providers"][0];
  auto holes = p["typed_gaps"], steps = p["plan"]["steps"];
  check(emitted["providers"].size() == 1 &&
            emitted["interpretation_errors"].empty() &&
            emitted["clarifications"].empty() && p["status"] == "partial" &&
            p["uncovered"].empty() && holes.size() == 1 &&
            holes[0]["from"] == binding["from"] &&
            holes[0]["to"] == binding["to"],
        "string sink removal: " + dump(emitted));
  receipt["removed"] =
      object({{"bundle_sha256", digest(reduced / "SHA256SUMS")},
              {"steps", steps},
              {"typed_gaps", holes}});
  auto dir = work / "integer-sibling",
       exe = work / ".test-programs/remaining-integer-writer";
  auto ids = array({"read_lines", "byte_lengths", "write_numbers"});
  auto sibling = complete_file_plan(reduced, dir, task("byte_lengths"), ids);
  build_emission(sibling, dir, exe, "g++-15");
  auto sandbox_dir = dir / "case";
  save(sandbox_dir / "in", "aa\n\nz\n");
  auto cmd = sandbox(exe, sandbox_dir);
  append(cmd, {"in", "out"});
  run(cmd, {.seconds = 8, .probe_limits = true});
  check(bytes(sandbox_dir / "out") == "[2,0,1]",
        "same-header integer sibling failed");
  receipt["remaining_sibling"] =
      object({{"bindings", ids},
              {"program_sha256", digest(exe)},
              {"output", load(sandbox_dir / "out")}});
  return receipt;
}
} // namespace

Path qualify_json_writer(const Path &package, const std::string &pin,
                         const Path &registry,
                         const std::string &registry_pin) {
  verify(package, pin);
  const auto src = sources()["retained-json-text-writer"];
  check(src["approval"] == "local-pilot" &&
            digest(src["archive"].asString()) == src["sha256"].asString(),
        "writer source pin or approval");
  auto work = fresh_run("v5-json-writer"), stage = work / ".stage",
       scratch = work / ".test-programs";
  fs::create_directory(scratch);
  auto source = retained_source(stage, src["id"].asString(), "project-LICENSE");
  save_file_registry(stage, src, registry, registry_pin);
  auto archived = [&](const std::string &key) {
    auto data = archive_member(src, src[key].asString());
    check(sha(data) == src[key + "_sha256"].asString(),
          "changed archived " + key);
    return data;
  };
  auto prior = parse(archived("documentation"))["providers"];
  save(stage / "upstream-example.cpp", archived("examples"));
  auto bundle = package / "bundle",
       header = stage / "original/headers/json_sequence.hpp";
  auto rows = load(bundle / "providers/contracts.json")["providers"];
  auto binding = [&](const std::string &id) {
    J match;
    for (const auto &r : rows)
      if (r["id"] == id) {
        check(match.isNull(), "duplicate legacy row");
        match = r;
      }
    check(!match.isNull() && std::count(prior.begin(), prior.end(), match) == 1,
          "raw row differs from archived source");
    return match;
  };
  auto writer = binding("write_texts"), numbers = binding("write_numbers");
  auto record = file_binding_identity(writer, bundle);
  check(writer["brick"] == "j85" && writer["destination_first"] == true &&
            !writer.isMember("effect") && writer["call"] == src["symbols"][0] &&
            writer["call"] == numbers["call"] &&
            writer["header"] == numbers["header"] &&
            numbers["from"] == "integers" &&
            digest(header) == record["header_sha256"].asString(),
        "writer binding or implementation mismatch");
  source["binding"] = writer;
  source["integer_sibling"] = numbers;
  source["runtime"] = src["runtime"];
  write(stage / "source.json", source);
  const auto dep = src["runtime"];
  auto info = verified_runtime_package(work, dep);
  for (const auto &field : {"pkgver", "arch", "license"})
    check(info.find("\n" + std::string(field) + " = " + dep[field].asString() +
                    "\n") != std::string::npos,
          "unexpected json-c package metadata");
  for (const auto &line :
       Strings{"pkgname = json-c", "url = " + dep["upstream"].asString()})
    check(info.find("\n" + std::string(line) + "\n") != std::string::npos,
          "json-c package identity mismatch");
  fs::copy_file("/usr/share/licenses/json-c/COPYING",
                stage / "notices/json-c-COPYING");
  for (const auto &[from, to] :
       std::vector<std::pair<std::string, std::string>>{
           {"write-texts.json", "spec.json"},
           {"write-texts-client.cpp", "client.cpp"},
           {"write-texts-annotation.json", "annotation.json"}})
    fs::copy_file(v5_root() / "fixtures" / from, stage / to);
  auto fixture = load(stage / "spec.json");
  check(fixture["binding"] == record["key"], "unexpected structural binding");
  auto tools = file_qualification_tools(), env = tools;
  auto exe = scratch / "client";
  Strings build = {"g++-15",
                   "-std=c++17",
                   "-O1",
                   "-I",
                   header.parent_path(),
                   stage / "client.cpp",
                   "-o",
                   exe};
  append(build, flags({"json-c"}));
  run(build);
  auto abi = sandbox(exe);
  abi.push_back("--int-max");
  const int bound = std::stoi(run(abi));
  check(bound == 2147483647, "unsupported signed-int target");
  auto deps = run({"ldd", exe});
  std::istringstream libs(deps);
  for (std::string line; std::getline(libs, line);) {
    std::istringstream tokens(line);
    std::string name, arrow, path;
    if (tokens >> name >> arrow >> path && arrow == "=>" &&
        name == "libjson-c.so.5")
      env["linked_json_c_sha256"] = digest(path);
  }
  check(env["linked_json_c_sha256"] ==
            dep["files"]["usr/lib/libjson-c.so.5.5.0"],
        "different runtime json-c");
  env["target"] = run({"g++-15", "-dumpmachine"});
  env["int_max"] = bound;
  env["runtime_pkginfo_sha256"] = sha(info);
  env["json_c_version"] = run({"pkg-config", "--modversion", "json-c"});
  env["runtime_keyring_sha256"] = digest(work / "runtime-keyring.gpg");
  write(stage / "environment.json", env);
  write(stage / "standalone.json",
        object({{"cases", suite(exe, work / "direct", fixture["cases"], false)},
                {"int_max", bound}}));
  write(stage / "integration.json",
        trials(work, bundle, writer, fixture["cases"]));
  check(tools == file_qualification_tools(), "writer toolchain changed");
  auto contract =
      file_contract(stage, package, record, src, stage / "sources.json");
  contract["qualification"]["scope"] =
      "Detached evidence on the recorded ABI and local filesystem fixtures. "
      "Valid JSON requires UTF-8 input and representable record lengths. No "
      "unrestricted admission.";
  validate(contract, load(package / "contract.schema.json"));
  write(stage / "contract.json", contract);
  write(
      stage / "domain.json",
      object({{"schema", "unllm.scoped-file-evidence-domain/1"},
              {"binding_key", record["key"]},
              {"raw_binding_sha256", record["binding_sha256"]},
              {"logical_id", record["logical_id"]},
              {"implementation_sha256", record["header_sha256"]},
              {"contract_sha256", digest(stage / "contract.json")},
              {"environment_sha256", digest(stage / "environment.json")},
              {"call", writer["call"]},
              {"argument_index", 1},
              {"argument_type", "const std::vector<std::string>&"},
              {"quantifier", "each_record"},
              {"all_of", array({object({{"id", "valid_utf8"}, {"value", true}}),
                                object({{"id", "max_bytes_inclusive"},
                                        {"value", bound}})})},
              {"scope", "Necessary conditions for valid JSON on the tested "
                        "ABI. Size is a source-derived representability limit. "
                        "No large allocation or general success proof."}}));
  return finish_file_stage(work, package, pin, record["logical_id"].asString(),
                           "unllm.scoped-file-evidence/1");
}
} // namespace v5
