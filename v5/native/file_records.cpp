#include "bundle.hpp"
#include "migration_reports.hpp"
#include "pilot.hpp"
#include <QScopeGuard>
#include <QStandardPaths>
#include <algorithm>
#include <functional>
#include <sstream>
#include <sys/stat.h>
#include <utility>

namespace v5 {
namespace {
const std::string source_id = "retained-zip-records";
const std::string library = "libbrick_shared_inputs.so";
const Path history = "qualifications/shared/inputs";
const Path runtime = "runtime/duckdb/lib";
const Path readers_history = "qualifications/shared/readers";
const std::string readers_library = "libbrick_shared_readers.so";

struct Projection {
  std::string binding, input_type, fields, request, fixture;
  J chain;
  bool directory() const { return input_type == "inode/directory"; }
  bool json() const { return input_type == "application/json"; }
};
} // namespace

Path arch_keyring(const Path &stage) {
  auto ring = stage.parent_path() / "arch.gpg";
  run({"gpg", "--batch", "--dearmor", "--output", ring,
       "/usr/share/pacman/keyrings/archlinux.gpg"});
  return ring;
}

std::string signed_build_info(const J &src, const Path &recipe,
                              const Path &ring) {
  Path arc = src["archive"].asString(), sig = arc.string() + ".sig";
  check(digest(arc) == src["sha256"].asString() &&
            digest(sig) == src["signature_sha256"].asString() &&
            digest(recipe) == src["recipe_sha256"].asString(),
        "changed retained Arch inputs");
  const auto signature =
      run({"gpgv", "--keyring", ring, "--status-fd", "1", sig, arc});
  check(signature.find("[GNUPG:] VALIDSIG " + src["signer"].asString() + " ") !=
            std::string::npos,
        "unexpected Arch archive signer");
  auto build = archive_member(src, ".BUILDINFO");
  check(build.find("\npkgbuild_sha256sum = " + digest(recipe) + "\n") !=
            std::string::npos,
        "unexpected Arch build attestation");
  return build;
}

std::string package_info(const J &src, const std::string &name) {
  auto info = archive_member(src, ".PKGINFO");
  check(info.find("\npkgname = " + name + "\n") != std::string::npos &&
            info.find("\npkgver = " + src["release"].asString() + "\n") !=
                std::string::npos &&
            info.find("\nurl = " + src["upstream"].asString() + "\n") !=
                std::string::npos,
        "unexpected Arch package identity");
  return info;
}

J runtime_files(const Path &bundle, const Path &base, const Strings &files) {
  auto link_files = array();
  for (const auto &file : files) {
    auto hash = digest(bundle / runtime / file);
    check(hash == digest(base / runtime / file), "retained runtime changed");
    link_files.append(object({{"path", file}, {"sha256", hash}}));
  }
  return link_files;
}

void retain_provider_abi(const Path &stage, const Path &bundle, const J &src) {
  const auto sibling_revision = src["provider_abi_revision"].asString();
  auto abi = run({"git", "-C", main_repo, "show",
                  sibling_revision + ":experiments/boilerplate/component-trial/"
                                     "providers/provider_types.hpp"});
  auto notice =
      run({"git", "-C", main_repo, "show", sibling_revision + ":LICENSE"});
  check(abi == bytes(bundle / "providers/provider_types.hpp") &&
            sha(notice) == src["provider_abi_notice_sha256"].asString(),
        "retained provider ABI or notice changed");
  save(stage / "original/provider_types.hpp", abi);
  save(stage / "notices/provider-abi-MIT", notice);
}

void retain_adapter(const Path &stage, const Path &bundle, const Path &origin,
                    const J &src, const J &binding, const J &provenance,
                    const std::string &git_adapter) {
  auto adapter = bytes(bundle / origin / "adapter.cpp");
  check(sha(adapter) == provenance["adapter_sha256"].asString() &&
            adapter ==
                run({"git", "-C", root, "show",
                     src["adapter_revision"].asString() + ":" + git_adapter}),
        "retained adapter differs from recorded source");
  save(stage / "original/adapter.cpp", adapter);
  retain_provider_abi(stage, bundle, src);
  fs::copy_file(bundle / origin / "source.json", stage / "legacy-source.json");
  fs::copy_file(bundle / origin / "contracts.json",
                stage / "legacy-contracts.json");
  fs::copy_file(bundle / "headers" / binding["header"].asString(),
                stage / "original" / binding["header"].asString());
}

J file_argument_scope(const J &arguments, const J &parameters) {
  check(arguments.isArray() && parameters.isObject() &&
            arguments.size() == parameters.size(),
        "file selector tuple differs from binding arity");
  auto predicates = array();
  for (Json::ArrayIndex i = 0; i < arguments.size(); ++i) {
    const auto parameter = arguments[i].asString();
    check(parameters[parameter].isString(),
          "file selector argument is missing or non-text: " + parameter);
    predicates.append(object({{"id", "exact_text"}, {"parameter", parameter},
                               {"argument_index", static_cast<int>(i) + 1},
                               {"argument_type", "const std::string&"},
                               {"value", parameters[parameter]}}));
  }
  return predicates;
}

namespace {
void verify_archive_materials(const Path &stage, const Path &bundle,
                              const Path &base, const J &src,
                              const J &binding) {
  const Path arc = src["archive"].asString();
  auto recipe = arc.parent_path() / "libarchive.PKGBUILD";
  auto ring = arch_keyring(stage);
  auto build = signed_build_info(src, recipe, ring);
  save(stage / "arch-pkginfo.txt", package_info(src, "libarchive"));
  save(stage / "arch-pkgbuild.txt", bytes(recipe));
  const auto provenance = load(bundle / history / "source.json");
  auto kernel = digest(bundle / runtime / library);
  for (const auto *soname :
       {"libarchive.so.13", "libsqlite3.so.0", "liburiparser.so.1"})
    check(digest(bundle / runtime / soname) ==
              provenance["runtime_sha256"][soname].asString(),
          "legacy runtime provenance mismatch");
  check(sha(archive_member(src, "usr/lib/libarchive.so.13.8.9")) ==
                digest(bundle / runtime / "libarchive.so.13") &&
            provenance["kernel_sha256"] == kernel &&
            provenance["archives"][arc.filename().string()]["signer"] ==
                src["signer"],
        "kernel or signed archive provenance mismatch");
  auto source =
      object({{"binding", binding},
              {"source_id", src["id"]},
              {"kernel_sha256", kernel},
              {"arch_signature_sha256", src["signature_sha256"]},
              {"arch_signer", src["signer"]},
              {"arch_keyring_sha256", digest(ring)},
              {"arch_buildinfo_sha256", sha(build)},
              {"archive_entrypoint", "archive_read_next_header"},
              {"runtime_files",
               runtime_files(bundle, base,
                             {library, "libarchive.so.13", "libsqlite3.so.0",
                              "liburiparser.so.1", "libduckdb.so",
                              "pkgconfig/brick_shared_inputs.pc"})}});
  retain_adapter(stage, bundle, history, src, binding, provenance,
                 "shared/inputs/adapter.cpp");
  for (const auto *name : {"BSD-2-Clause.txt", "BSD-4-Clause-UC.txt"}) {
    std::string path = "usr/share/licenses/libarchive/" + std::string(name);
    auto content = archive_member(src, path);
    check(sha(content) == provenance["headers"][path].asString(),
          "archived libarchive notice changed");
    save(stage / "notices" / name, content);
  }
  const std::string pugi = "usr/share/licenses/pugixml/LICENSE.md";
  const auto pugi_notice =
      bytes(bundle / "qualifications/shared/upstream/formats" / pugi);
  check(sha(pugi_notice) == provenance["headers"][pugi].asString(),
        "bundled pugixml notice changed");
  save(stage / "notices/pugixml-LICENSE.md", pugi_notice);
  auto header = archive_member(src, "usr/include/archive.h");
  check(sha(header) ==
            provenance["headers"]["usr/include/archive.h"].asString(),
        "archived libarchive header changed");
  save(stage / "original/archive.h", header);
  write(stage / "source.json", source);
}

void verify_sqlite_materials(const Path &stage, const Path &bundle,
                             const Path &base, const J &src, const J &row) {
  const Path archive = src["archive"].asString();
  const auto recipe = archive.parent_path() / "sqlite.PKGBUILD";
  const auto ring = arch_keyring(stage);
  const auto build = signed_build_info(src, recipe, ring);
  const auto info = package_info(src, "sqlite");
  check(info.find("\nlicense = LicenseRef-Sqlite\n") != std::string::npos,
        "signed SQLite license changed");
  save(stage / "arch-pkginfo.txt", info);
  save(stage / "arch-pkgbuild.txt", bytes(recipe));

  const auto provenance = load(bundle / history / "source.json");
  const auto kernel = digest(bundle / runtime / library);
  const auto soname = "libsqlite3.so.0";
  const auto runtime_pin = digest(bundle / runtime / soname);
  const auto sealed = provenance["archives"][archive.filename().string()];
  const auto &definition = provenance["definitions"]["sqlite3_step"];
  check(sealed["signer"] == src["signer"] && sealed["sha256"] == src["sha256"] &&
            sealed["recipe_sha256"] == src["recipe_sha256"] &&
            provenance["kernel_sha256"] == kernel &&
            provenance["runtime_sha256"][soname] == runtime_pin &&
            sha(archive_member(src, src["runtime_member"].asString())) == runtime_pin &&
            definition["soname"] == soname &&
            definition["header"] == "sqlite3.h" &&
            run({"nm", "-D", "--undefined-only", bundle / runtime / library})
                    .find(" U sqlite3_step\n") != std::string::npos &&
            run({"nm", "-D", "--defined-only", bundle / runtime / soname})
                    .find(" T sqlite3_step\n") != std::string::npos,
        "retained SQLite linkage or signed runtime differs");
  retain_adapter(stage, bundle, history, src, row, provenance,
                 "shared/inputs/adapter.cpp");
  for (const auto *path : {"usr/include/sqlite3.h",
                           "usr/share/licenses/sqlite/license.txt"}) {
    const auto data = archive_member(src, path);
    check(sha(data) == provenance["headers"][path].asString() &&
              data == bytes(bundle / "qualifications/shared/upstream/formats" / path),
          "signed SQLite header or notice differs");
    const auto dest = Path(path).filename() == "sqlite3.h"
                          ? stage / "original/sqlite3.h"
                          : stage / "notices/sqlite-license.txt";
    save(dest, data);
  }
  write(stage / "source.json",
        object({{"binding", row},
                {"source_id", src["id"]},
                {"kernel_sha256", kernel},
                {"arch_signature_sha256", src["signature_sha256"]},
                {"arch_signer", src["signer"]},
                {"arch_keyring_sha256", digest(ring)},
                {"arch_buildinfo_sha256", sha(build)},
                {"sqlite_entrypoint", definition},
                {"runtime_files",
                 runtime_files(bundle, base,
                               {library, soname, "libarchive.so.13", "liburiparser.so.1",
                                "libduckdb.so", "pkgconfig/brick_shared_inputs.pc"})}}));
}

void verify_uri_materials(const Path &stage, const Path &bundle,
                          const Path &base, const J &src, const J &row) {
  const Path archive = src["archive"].asString();
  const auto recipe = archive.parent_path() / "uriparser.PKGBUILD";
  const auto ring = arch_keyring(stage);
  const auto build = signed_build_info(src, recipe, ring);
  const auto info = package_info(src, "uriparser");
  check(info.find("\nlicense = custom\n") != std::string::npos,
        "signed uriparser license metadata changed");
  save(stage / "arch-pkginfo.txt", info);
  save(stage / "arch-pkgbuild.txt", bytes(recipe));
  const auto provenance = load(bundle / history / "source.json");
  const auto kernel = digest(bundle / runtime / library);
  const auto soname = "liburiparser.so.1";
  const auto runtime_pin = digest(bundle / runtime / soname);
  const auto sealed = provenance["archives"][archive.filename().string()];
  const auto &definition = provenance["definitions"]["uriParseSingleUriA"];
  const auto imports = run({"nm", "-D", "--undefined-only", bundle / runtime / library});
  check(sealed["signer"] == src["signer"] && sealed["sha256"] == src["sha256"] &&
            sealed["recipe_sha256"] == src["recipe_sha256"] &&
            provenance["kernel_sha256"] == kernel &&
            provenance["runtime_sha256"][soname] == runtime_pin &&
            sha(archive_member(src, src["runtime_member"].asString())) == runtime_pin &&
            definition["soname"] == soname && definition["header"] == "uriparser/Uri.h" &&
            imports.find(" U uriParseSingleUriA\n") != std::string::npos &&
            imports.find(" U g_uri_list_extract_uris\n") != std::string::npos &&
            run({"nm", "-D", "--defined-only", bundle / runtime / soname})
                    .find(" T uriParseSingleUriA\n") != std::string::npos,
        "retained URI linkage or signed parser runtime differs");
  retain_adapter(stage, bundle, history, src, row, provenance,
                 "shared/inputs/adapter.cpp");
  const auto upstream = bundle / "qualifications/shared/upstream/formats";
  for (const auto &member : provenance["headers"].getMemberNames()) {
    if (member.rfind("usr/include/uriparser/", 0) != 0 &&
        member != "usr/share/licenses/uriparser/COPYING.BSD-3-Clause")
      continue;
    const auto data = archive_member(src, member);
    check(sha(data) == provenance["headers"][member].asString() &&
              data == bytes(upstream / member),
          "signed uriparser header or notice differs");
    const auto dest = member.rfind("usr/include/", 0) == 0
                          ? stage / "original/uriparser" / Path(member).filename()
                          : stage / "notices/uriparser-BSD-3-Clause";
    save(dest, data);
  }
  const auto dep_id = src["input_reader"].asString();
  const auto gir = sources()[dep_id];
  check(gir["approval"] == "local-pilot" &&
            digest(gir["archive"].asString()) == gir["sha256"].asString(),
        "URI list dependency differs from its reviewed source");
  const auto reader = retained_source(stage, dep_id, "uri-list-project-MIT");
  const Path gir_header = "headers/gir_g_uri_list_extract_uris.hpp";
  check(bytes(stage / "original" / gir_header) == bytes(bundle / gir_header),
        "retained URI list dependency differs from the parent");
  const auto dep = gir["runtime"];
  const auto glib_info = verified_runtime_package(stage.parent_path(), dep);
  check(glib_info.find("\npkgname = glib2\n") != std::string::npos &&
            glib_info.find("\nlicense = LGPL-2.1-or-later\n") != std::string::npos &&
            glib_info.find("\npkgver = " + dep["pkgver"].asString() + "\n") != std::string::npos,
        "signed GLib runtime identity differs");
  save(stage / "glib-pkginfo.txt", glib_info);
  const auto glib_header = archive_member(dep, "usr/include/glib-2.0/glib/gconvert.h");
  save(stage / "original/glib/gconvert.h", glib_header);
  write(stage / "source.json",
        object({{"binding", row},
                {"source_id", src["id"]},
                {"kernel_sha256", kernel},
                {"arch_signature_sha256", src["signature_sha256"]},
                {"arch_signer", src["signer"]},
                {"arch_keyring_sha256", digest(ring)},
                {"arch_buildinfo_sha256", sha(build)},
                {"uri_entrypoint", definition},
                {"uri_list_reader", reader},
                {"glib_archive_sha256", dep["sha256"]},
                {"glib_signature_sha256", dep["signature_sha256"]},
                {"glib_keyring_sha256", digest(stage.parent_path() / "runtime-keyring.gpg")},
                {"host_runtime", object({{"libglib-2.0.so.0",
                    dep["files"]["usr/lib/libglib-2.0.so.0.9000.1"]}})},
                {"runtime_files",
                 runtime_files(bundle, base,
                               {library, soname, "libarchive.so.13", "libsqlite3.so.0",
                                "libduckdb.so", "pkgconfig/brick_shared_inputs.pc"})}}));
}

void verify_xml_materials(const Path &stage, const Path &bundle,
                          const Path &base, const J &src, const J &row) {
  const Path archive = src["archive"].asString();
  const auto recipe = archive.parent_path() / "pugixml.PKGBUILD";
  const auto ring = arch_keyring(stage);
  const auto build = signed_build_info(src, recipe, ring);
  auto debug = src;
  const auto debug_file = src["debug_archive"].asString();
  debug["archive"] = (archive.parent_path() / debug_file).string();
  debug["sha256"] = src["debug_sha256"];
  debug["signature_sha256"] = src["debug_signature_sha256"];
  const auto debug_build = signed_build_info(debug, recipe, ring);
  const auto info = package_info(src, "pugixml");
  check(info.find("\nlicense = MIT\n") != std::string::npos,
        "signed pugixml license changed");
  save(stage / "arch-pkginfo.txt", info);
  save(stage / "arch-pkgbuild.txt", bytes(recipe));

  const auto provenance = load(bundle / history / "source.json");
  const auto kernel = digest(bundle / runtime / library);
  const auto symbol = "xpath_query::evaluate_node_set";
  const auto unit = provenance["source_units"][symbol].asString();
  const auto &definition = provenance["definitions"][symbol];
  const auto binary_pin = provenance["archives"][archive.filename().string()];
  const auto debug_pin = provenance["archives"][debug_file];
  check(binary_pin["signer"] == src["signer"] && binary_pin["sha256"] == src["sha256"] &&
            binary_pin["recipe_sha256"] == src["recipe_sha256"] &&
            debug_pin["signer"] == src["signer"] && debug_pin["sha256"] == debug["sha256"] &&
            debug_pin["recipe_sha256"] == src["recipe_sha256"] &&
            provenance["kernel_sha256"] == kernel && unit == src["source_unit"].asString() &&
            definition["name"].isString() &&
            run({"nm", "-D", "--defined-only", bundle / runtime / library})
                    .find(" T " + definition["name"].asString() + "\n") != std::string::npos,
        "pinned XPath source or extracted symbol differs");
  retain_adapter(stage, bundle, history, src, row, provenance,
                 "shared/inputs/adapter.cpp");
  const auto upstream = bundle / "qualifications/shared/upstream/formats";
  auto retain = [&](const J &package, const std::string &member, const Path &dest) {
    const auto data = archive_member(package, member);
    check(sha(data) == provenance["headers"][member].asString() &&
              data == bytes(upstream / member),
          "signed pugixml file differs from the retained origin: " + member);
    save(stage / dest, data);
  };
  retain(src, "usr/include/pugixml.hpp", "original/pugixml.hpp");
  retain(src, "usr/include/pugiconfig.hpp", "original/pugiconfig.hpp");
  retain(src, "usr/share/licenses/pugixml/LICENSE.md", "notices/pugixml-LICENSE.md");
  retain(debug, unit, "original/pugixml.cpp");
  retain(debug, (Path(unit).parent_path() / "pugixml.hpp").string(),
         "original/debug-pugixml.hpp");
  check(digest(stage / "original/pugixml.cpp") == src["source_unit_sha256"].asString(),
        "signed XPath source pin differs from the registry");
  write(stage / "source.json",
        object({{"binding", row},
                {"source_id", src["id"]},
                {"kernel_sha256", kernel},
                {"arch_signature_sha256", src["signature_sha256"]},
                {"arch_debug_signature_sha256", debug["signature_sha256"]},
                {"arch_signer", src["signer"]},
                {"arch_keyring_sha256", digest(ring)},
                {"arch_buildinfo_sha256", sha(build)},
                {"arch_debug_buildinfo_sha256", sha(debug_build)},
                {"xpath_symbol", definition},
                {"source_unit", unit},
                {"source_unit_sha256", src["source_unit_sha256"]},
                {"runtime_files",
                 runtime_files(bundle, base,
                               {library, "libsqlite3.so.0", "libarchive.so.13",
                                "liburiparser.so.1", "libduckdb.so",
                                "pkgconfig/brick_shared_inputs.pc"})}}));
}

void verify_reader_materials(const Path &stage, const Path &bundle,
                             const Path &base, const J &src, const J &binding,
                             const Strings &members) {
  const Path arc = src["archive"].asString();
  auto recipe = arc.parent_path() / "PKGBUILD";
  auto ring = arch_keyring(stage);
  auto build = signed_build_info(src, recipe, ring);
  auto debug = src;
  debug["archive"] =
      (arc.parent_path() / src["debug_archive"].asString()).string();
  debug["sha256"] = src["debug_sha256"];
  debug["signature_sha256"] = src["debug_signature_sha256"];
  auto debug_build = signed_build_info(debug, recipe, ring);
  const auto info = package_info(src, "duckdb");
  check(info.find("\nlicense = MIT\n") != std::string::npos,
        "signed DuckDB package license changed");
  save(stage / "arch-pkginfo.txt", info);
  save(stage / "arch-pkgbuild.txt", bytes(recipe));
  const auto provenance = load(bundle / readers_history / "source.json");
  const auto kernel = digest(bundle / runtime / readers_library);
  const auto symbol = src["symbols"][0].asString();
  const auto definition = provenance["definitions"][symbol];
  check(provenance["source_id"] == src["upstream_source_id"] &&
            provenance["kernel_sha256"] == kernel &&
            provenance["source_units"][symbol] == members.front() &&
            definition["name"].isString() &&
            run({"nm", "-D", "--defined-only",
                 bundle / runtime / readers_library})
                    .find(" " + definition["name"].asString() + "\n") !=
                std::string::npos,
        "retained reader implementation identity changed");

  auto packed_library = stage.parent_path() / "signed-libduckdb.so";
  run({"bsdtar", "-xOf", arc, "usr/lib/libduckdb.so"},
      {.output = packed_library});
  check(digest(packed_library) == digest(bundle / runtime / "libduckdb.so"),
        "DuckDB runtime differs from signed binary package");
  fs::remove(packed_library);
  auto extracted = stage.parent_path() / "signed-debug";
  fs::create_directory(extracted);
  const Path prefix = "usr/src/debug/duckdb/duckdb";
  Strings command{"bsdtar", "-xf", debug["archive"].asString(), "-C",
                  extracted};
  for (const auto &name : members)
    command.push_back((prefix / name).string());
  run(command);
  for (const auto &name : members) {
    auto data = bytes(extracted / prefix / name);
    auto retained = bundle / "qualifications/shared/upstream" / name;
    const auto pin = sha(data);
    check(pin == provenance["headers"][name].asString() &&
              (!src.isMember("source_unit_sha256") ||
               pin == src["source_unit_sha256"].asString()) &&
              data == bytes(retained),
          "retained reader source differs from signed debug package");
    save(stage / "original" / Path(name).filename(), data);
  }
  fs::remove_all(extracted);
  save(stage / "notices/duckdb-LICENSE",
       archive_member(src, "usr/share/licenses/duckdb/LICENSE"));
  retain_adapter(stage, bundle, readers_history, src, binding, provenance,
                 "shared/readers/adapter.cpp");
  write(stage / "source.json",
        object({{"binding", binding},
                {"source_id", src["id"]},
                {"kernel_sha256", kernel},
                {"arch_signature_sha256", src["signature_sha256"]},
                {"arch_debug_signature_sha256", debug["signature_sha256"]},
                {"arch_signer", src["signer"]},
                {"arch_keyring_sha256", digest(ring)},
                {"arch_buildinfo_sha256", sha(build)},
                {"arch_debug_buildinfo_sha256", sha(debug_build)},
                {"parser_symbol", definition},
                {"runtime_files",
                 runtime_files(bundle, base,
                               {readers_library, "libduckdb.so",
                                "pkgconfig/brick_shared_readers.pc"})}}));
}

void make_zip_inputs(const Path &work, const Path &stage, const J &spec) {
  auto members = work / "members", tar = work / "input.tar";
  save(members / "d/UP.CsV", "data");
  save(members / "TOP.csv", "");
  fs::create_directories(members / "links");
  fs::create_symlink("../TOP.csv", members / "links/alias");
  fs::create_directory(members / "dir.csv");
  for (const auto *name : {"d/archive.tar.GZ", "notes/.env", "p/x.", "README"})
    save(members / name, "data");
  auto c = spec["cases"][0];
  check(c["format"] == "mixed", "expected a fixture member order");
  Strings command{"tar",
                  "--format=ustar",
                  "--mtime=@946684800",
                  "--mode=u+rwX,go=rX",
                  "--owner=0",
                  "--group=0",
                  "--numeric-owner",
                  "--no-recursion",
                  "--hard-dereference",
                  "-cf",
                  tar,
                  "--"};
  append(command, strings(c["members"]));
  run(command, {.cwd = members});
  auto fixtures = stage / "fixtures";
  fs::create_directories(fixtures);
  run({"bsdtar", "--format", "zip", "--options", "zip:compression=store", "-cf",
       fixtures / "mixed", "@" + tar.string()});
  check(run({"unzip", "-Z1", fixtures / "mixed"}) ==
            join(strings(c["members"]), "\n") + "\n",
        "Info-ZIP member list differs from authored fixture");
  run({"unzip", "-tqq", fixtures / "mixed"});
  fs::copy_file(tar, fixtures / "tar");
  save(fixtures / "empty",
       std::string("PK\005\006", 4) + std::string(18, '\0'));
  save(fixtures / "invalid", "unrecognized archive");
  const auto path = volume / "unllm/experiments/v5-parent-transfer-QCn3p5ZQ";
  const auto pins = spec["pinned_fixtures"];
  for (const auto &name : pins.getMemberNames()) {
    auto source = path / (name + ".zip");
    check(digest(source) == pins[name].asString(), "prior ZIP fixture changed");
    fs::copy_file(source, fixtures / name);
  }
}

void make_tree_input(const Path &dir, const J &fixture) {
  auto input = dir / "in";
  const auto kind = fixture["kind"].asString();
  if (kind == "missing")
    return;
  if (kind == "file") {
    save(input, "regular file");
    return;
  }
  if (kind == "symlink") {
    save(dir / "target/hidden.TXT", "regular file behind link");
    fs::create_symlink("target", input);
    return;
  }
  check(kind == "directory", "unknown directory fixture kind");
  fs::create_directories(input);
  for (const auto &folder : fixture["directories"])
    fs::create_directories(input / folder.asString());
  for (const auto &name : fixture["files"])
    save(input / name.asString(), "data");
  const auto links = fixture.get("symlinks", object());
  for (const auto &name : links.getMemberNames())
    fs::create_symlink(links[name].asString(), input / name);
  for (const auto &name : fixture["fifos"])
    check(::mkfifo((input / name.asString()).c_str(), 0600) == 0,
          "cannot create tree fixture FIFO");
}

J fixture_inventory(const Path &dir, bool directory) {
  if (!directory)
    return tree_hashes(dir);
  J files = object();
  for (const auto &entry : fs::recursive_directory_iterator(dir)) {
    const auto key = entry.path().lexically_relative(dir).generic_string();
    const auto type = entry.symlink_status().type();
    switch (type) {
    case fs::file_type::regular:
      files[key] = digest(entry.path());
      break;
    case fs::file_type::directory:
      files[key] = object({{"directory", true}});
      break;
    case fs::file_type::symlink:
      files[key] =
          object({{"symlink", fs::read_symlink(entry.path()).string()}});
      break;
    case fs::file_type::fifo:
      files[key] = object({{"fifo", true}});
      break;
    default:
      throw std::runtime_error("unexpected tree fixture entry: " + key);
    }
  }
  return files;
}

Strings invoke(const Path &exe, const Path &dir, const Path &bundle) {
  auto cmd = sandbox(exe, dir);
  cmd.pop_back();
  append(cmd, {"--ro-bind", bundle / runtime, "/retained-runtime", "--setenv",
               "LD_LIBRARY_PATH", "/retained-runtime", "/probe"});
  return cmd;
}

J probe_record_rows(const Path &exe, const Path &fixtures, const Path &bundle,
                    const std::string &path, const std::string &selector) {
  auto cmd = invoke(exe, fixtures, bundle);
  append(cmd, {path, selector});
  auto first = parse(run(cmd, {.seconds = 8, .probe_limits = true}));
  check(first == parse(run(cmd, {.seconds = 8, .probe_limits = true})),
        "different runtime or process result");
  return first;
}

void check_record_rows(const J &test, const J &seen, const std::string &binding,
                       bool repeats) {
  J rows = array();
  int pos = 0;
  for (const auto &r : test["rows"])
    rows.append(object({{"position", ++pos}, {"fields", r}}));
  check(seen["rows"] == rows &&
            seen["phase"].asString() == test.get("error", "").asString() &&
            seen["exception"] ==
                test.get("exception",
                         test.isMember("error") ? J("runtime_error") : J()) &&
            (!repeats || test.isMember("error") || seen["same_instance_repeat"] == true),
        binding + " row observation differs for " +
            test["name"].asString() + ": " + dump(seen));
}

J repeated_calls(const Path &exe, const Path &fixtures, const Path &bundle,
                 const J &spec, const Projection &projection) {
  const auto before = fixture_inventory(fixtures, projection.directory());
  J observed = array();
  auto probe = [&](const std::string &path, const std::string &fields) {
    auto seen = probe_record_rows(exe, fixtures, bundle, path, fields);
    check(projection.json() || seen["libarchive"] == 3008009,
          "different archive runtime");
    return seen;
  };
  for (const auto &c : spec["cases"]) {
    auto name = c["name"].asString();
    auto input = projection.directory() ? name + "/in"
                 : projection.json()    ? name
                                        : c["format"].asString();
    auto seen = probe(input, projection.fields);
    check_record_rows(c, seen, projection.binding, projection.directory() || projection.json());
    // Preserve the explicit null field in historical projection receipts.
    seen["exception"] = seen.get("exception", J());
    observed.append(object({{"name", c["name"]}, {"observation", seen}}));
  }
  if (projection.json()) {
    auto controls = array();
    for (const auto &c : spec["selector_controls"]) {
      auto observed_control =
          probe(c["input_case"].asString(), c["fields"].asString());
      check_record_rows(c, observed_control, projection.binding, true);
      observed_control["exception"] = observed_control.get("exception", J());
      controls.append(
          object({{"name", c["name"]}, {"observation", observed_control}}));
    }
    check(fixture_inventory(fixtures, false) == before,
          "JSON reader changed a fixture");
    return object({{"processes_per_case", 2},
                   {"cases", observed},
                   {"input_sha256", before},
                   {"selector_controls", controls}});
  }
  auto control_input = projection.directory() ? "bare-root/in" : "top-level";
  auto reorder = probe(control_input,
                       projection.directory() ? "ext,parent" : "parent,ext");
  const auto expected_control =
      projection.directory()
          ? array({object({{"position", 1}, {"fields", array({"txt", "."})}}),
                   object({{"position", 2}, {"fields", array({"", "."})}})})
          : array({object({{"position", 1}, {"fields", array({".", "csv"})}})});
  check(reorder["phase"] == "" && reorder["rows"] == expected_control,
        "reordered projection did not preserve field semantics");
  auto invalid = probe(control_input, "ext,unknown");
  check(invalid["phase"] == "select" &&
            invalid["exception"] == "invalid_argument" &&
            invalid["rows"] == array() &&
            fixture_inventory(fixtures, projection.directory()) == before,
        "unexpected projection rejection or fixture mutation");
  return object({{"processes_per_case", 2},
                 {"cases", observed},
                 {"input_sha256", before},
                 {"reordered_control", reorder},
                 {"invalid_projection", invalid}});
}

J file_record_trial(const Path &work, const Path &bundle, const Path &base,
                    const J &row, const J &spec, const J &chain, const J &cases,
                    bool directory,
                    const std::function<void(const Path &, const J &)> &prepare,
                    const std::string &label = "") {
  auto report = object({{"spec", spec}, {"chain", chain}});
  for (const std::string arm : {"baseline", "staged"}) {
    auto dest = work / arm;
    if (!label.empty())
      dest /= label;
    auto exe = work / ".test-programs" / (label.empty() ? arm : arm + "-" + label);
    auto p = complete_file_plan(arm == "baseline" ? base : bundle, dest, spec, chain);
    build_emission(p, dest, exe, "g++-15");
    J observations = array();
    for (const auto &c : cases) {
      if (c.get("standalone_only", false).asBool())
        continue;
      for (const bool exists : {false, true}) {
        auto dir =
            dest / c["name"].asString() / (exists ? "existing" : "absent");
        fs::create_directories(dir);
        prepare(dir, c);
        if (exists)
          save(dir / "out", "sentinel");
        auto before = fixture_inventory(dir, directory), after = before;
        auto cmd = invoke(exe, dir, bundle);
        append(cmd, {"in", "out"});
        auto result = execute(cmd, {.seconds = 8, .probe_limits = true});
        const bool fails = c.isMember("error");
        if (!fails)
          after["out"] = sha(c["output"].asString());
        check(result.status == (fails ? 1 : 0) &&
                  fixture_inventory(dir, directory) == after,
              "unexpected CLI file record result for " + c["name"].asString() +
                  " " + result.err);
        observations.append(object({{"case", c["name"]},
                                    {"exit", result.status},
                                    {"destination_existed", exists},
                                    {"before", before},
                                    {"after", after}}));
      }
    }
    report[arm] = object({{"execution", observations},
                          {"program_sha256", digest(exe)},
                          {"emitted_sha256", digest(dest / "component.hpp")}});
  }
  check(report["baseline"] == report["staged"],
        "retained file record recipe differs between arms");
  auto cut = work / "control-bundle";
  if (!fs::exists(cut))
    write_tree(cut, remove_binding(read_tree(bundle), row));
  auto removed = work / "removed";
  if (!label.empty())
    removed /= label;
  auto absent = plan(cut, spec, removed);
  check(absent["providers"].isArray() && absent["providers"].empty() &&
            absent["interpretation_errors"].empty() &&
            absent["clarifications"].empty() &&
            absent["gaps"]["r"].asString().find(row["id"].asString()) !=
                std::string::npos,
        "removal did not isolate the file record binding");
  report["removed"] = object({{"gaps", absent["gaps"]},
                              {"bundle_sha256", digest(cut / "SHA256SUMS")}});
  return report;
}

J ordinary(const Path &work, const Path &stage, const Path &bundle,
           const Path &base, const J &row, const J &suite,
           const Projection &projection) {
  auto recipes = root / "shared/request-recipes.json";
  check(digest(recipes) ==
            "768676cbc34ef7db9d869520bd3d26d86123f49a3c9803901eb5653de3a6f49b",
        "changed retained operation recipes");
  const auto tasks = load(recipes);
  J ops = array();
  for (const auto &step : tasks[projection.request][0])
    ops.append(object({{"outcome", step[0]}, {"parameters", step[1]}}));
  check(ops[0]["outcome"] == projection.binding &&
            ops[0]["parameters"] == suite["parameters"],
        "reference recipe does not request the qualified projection");
  const auto spec =
      spec_for(projection.input_type, "text/plain",
               object({{"operations", ops}, {"uncovered", array()}}));
  auto prepare = [&](const Path &dir, const J &c) {
    if (projection.directory()) {
      make_tree_input(dir, c);
      return;
    }
    const auto fixture = stage / "fixtures" /
                         c[projection.json() ? "name" : "format"].asString();
    if (fs::exists(fixture))
      fs::copy_file(fixture, dir / "in");
  };
  auto report = file_record_trial(work, bundle, base, row, spec,
                                  projection.chain, suite["cases"],
                                  projection.directory(), prepare);
  report["recipe_sha256"] = digest(recipes);
  return report;
}

auto pin_runtime_path(const Path &bundle) {
  const auto prior = qgetenv("PKG_CONFIG_PATH");
  const auto restore = [prior] {
    if (prior.isNull())
      qunsetenv("PKG_CONFIG_PATH");
    else
      qputenv("PKG_CONFIG_PATH", prior);
  };
  check(qputenv("PKG_CONFIG_PATH", (bundle / runtime / "pkgconfig").c_str()),
        "failed to set pinned runtime package path");
  return qScopeGuard(restore);
}

Path build_record_client(const Path &stage, const Path &scratch,
                         const Path &bundle, const J &source, const J &tools,
                         bool readers, bool archive_tools,
                         const Strings &extra_libraries = {},
                         bool inspect_symbols = false) {
  auto options = flags({"jsoncpp", readers ? "brick_shared_readers"
                                           : "brick_shared_inputs"});
  auto exe = scratch / "client";
  Strings cmd{"g++-15",           "-std=c++17",         "-O1", "-I",
              stage / "original", stage / "client.cpp", "-o",  exe};
  append(cmd, options);
  if (!readers)
    cmd.push_back((bundle / runtime / "libarchive.so.13").string());
  for (const auto &name : extra_libraries)
    cmd.push_back((bundle / runtime / name).string());
  cmd.push_back("-Wl,-rpath-link," + (bundle / runtime).string());
  run(cmd);
  auto env = tools;
  auto tool_names = Strings{"bsdtar", "gpgv", "ldd"};
  if (readers || !extra_libraries.empty() || inspect_symbols)
    tool_names.push_back("nm");
  else if (archive_tools)
    append(tool_names, {"tar", "unzip"});
  for (const auto &name : tool_names)
    env["tool_sha256"][name] =
        digest(QStandardPaths::findExecutable(QString::fromStdString(name))
                   .toStdString());
  env["client_flags"] = string_array(options);
  env["target"] = run({"g++-15", "-dumpmachine"});
  env["runtime_files"] = source["runtime_files"];
  std::istringstream link(
      run({"ldd", exe},
          {.environment = {{"LD_LIBRARY_PATH", (bundle / runtime).string()}}}));
  for (std::string line; std::getline(link, line);) {
    std::istringstream fields(line);
    std::string name, arrow, path;
    if (fields >> name >> arrow >> path && arrow == "=>")
      env["resolved_client_libraries"][name] = object(
          {{"path", fs::canonical(path).string()}, {"sha256", digest(path)}});
  }
  for (const auto &f : source["runtime_files"]) {
    auto name = f["path"].asString();
    const bool required = readers ? (name == readers_library || name == "libduckdb.so")
                                  : (name == library || name == "libarchive.so.13");
    if (required || std::find(extra_libraries.begin(), extra_libraries.end(), name) !=
                        extra_libraries.end())
      check(env["resolved_client_libraries"][name]["sha256"] == f["sha256"] &&
                env["resolved_client_libraries"][name]["path"] ==
                    fs::canonical(bundle / runtime / name).string(),
            "standalone client linked the wrong retained runtime");
  }
  for (const auto &name : source["host_runtime"].getMemberNames())
    check(env["resolved_client_libraries"][name]["sha256"] == source["host_runtime"][name],
          "standalone client linked the wrong installed runtime: " + name);
  write(stage / "environment.json", env);
  return exe;
}

Path qualify_records(const Path &package, const std::string &pin,
                     const Path &registry, const std::string &registry_pin,
                     const Projection &projection) {
  verify(package, pin);
  const auto selected_source =
      projection.json() ? "retained-json-records" : source_id;
  auto src = sources()[selected_source];
  const auto job =
      retained_binding_job(v5_root() / "sources.json", projection.binding);
  check(src["approval"] == "local-pilot" &&
            src["method"] == "signed-local-package" &&
            job["source"] == selected_source && job["route"] == "api" &&
            job["stream"] == "foundational" &&
            job["tests"] == "v5/fixtures/" + projection.fixture + ".json" &&
            job["parameter"] == object({{"fields", projection.fields}}) &&
            (!projection.directory() ||
             !job["approval_reference"].asString().empty()),
        "no reviewed local file record projection job");
  const auto work = fresh_run("v5-" + projection.fixture),
             stage = work / ".stage", scratch = work / ".test-programs",
             bundle = package / "bundle", base = verified_base();
  fs::create_directory(scratch);
  save_file_registry(stage, src, registry, registry_pin);
  check(retained_binding_job(stage / "sources.json", projection.binding) == job,
        projection.directory() ? "changed tree projection job"
        : projection.json()    ? "changed JSON projection job"
                               : "changed ZIP projection job");
  auto defs = load(bundle / "providers/contracts.json");
  auto raw = select_binding(defs["providers"], projection.binding,
                            "miner_" + projection.binding,
                            "in:" + projection.input_type, "rows");
  auto record = file_binding_identity(raw, bundle);
  const auto origin = projection.json() ? readers_history : history;
  auto legacy = load(bundle / origin / "contracts.json")[projection.binding];
  check(raw["id"] == projection.binding &&
            raw["call"] == "brick_miner::shared::" + projection.binding &&
            raw["args"] == array({"fields"}) && raw["from"] == legacy["from"] &&
            raw["to"] == legacy["to"] &&
            legacy["args"] == object({{"fields", "text"}}) &&
            defs["outcomes"][projection.binding]["parameters"] ==
                legacy["args"],
        "file record binding differs from retained declaration");
  if (projection.json())
    verify_reader_materials(stage, bundle, base, src, raw,
                            {"third_party/yyjson/yyjson.cpp",
                             "third_party/yyjson/include/yyjson.hpp"});
  else
    verify_archive_materials(stage, bundle, base, src, raw);
  const auto source = load(stage / "source.json");
  for (const auto &[from, to] :
       std::vector<std::pair<std::string, std::string>>{
           {projection.fixture + ".json", "spec.json"},
           {projection.fixture + "-client.cpp", "client.cpp"},
           {projection.fixture + "-annotation.json", "annotation.json"}})
    fs::copy_file(v5_root() / "fixtures" / from, stage / to);
  const auto spec = load(stage / "spec.json");
  check(spec["binding"] == record["key"] &&
            spec["parameters"] == job["parameter"],
        "qualification scope differs from reviewed binding or parameter");
  auto fixtures = stage / "fixtures";
  if (projection.directory()) {
    fixtures = work / "directories";
    for (const auto &fixture : spec["cases"]) {
      const auto dir = fixtures / fixture["name"].asString();
      fs::create_directories(dir);
      make_tree_input(dir, fixture);
    }
  } else if (projection.json()) {
    for (const auto &c : spec["cases"])
      if (c.isMember("input"))
        save(fixtures / c["name"].asString(), c["input"].asString());
  } else {
    make_zip_inputs(work, stage, spec);
  }

  const auto restore = pin_runtime_path(bundle);
  const auto tools = file_qualification_tools();
  const auto exe = build_record_client(stage, scratch, bundle, source, tools,
                                       projection.json(), !projection.directory());
  write(stage / "standalone.json",
        repeated_calls(exe, fixtures, bundle, spec, projection));
  write(stage / "integration.json",
        ordinary(work, stage, bundle, base, raw, spec, projection));
  check(file_qualification_tools() == tools, "qualification tools changed");
  auto contract =
      file_contract(stage, package, record, src, stage / "sources.json");
  contract["source"]["implementation_sha256"] = source["kernel_sha256"];
  contract["qualification"]["scope"] =
      "Detached local evidence for one exact fields selector, " +
      projection.fields + ", on the pinned archived runtime and recorded " +
      (projection.directory() ? "directory"
       : projection.json()    ? "JSON"
                              : "ZIP") +
      " fixtures.";
  validate(contract, load(package / "contract.schema.json"));
  write(stage / "contract.json", contract);
  write(
      stage / "domain.json",
      object({{"schema", "unllm.scoped-file-evidence-domain/1"},
              {"binding_key", record["key"]},
              {"raw_binding_sha256", record["binding_sha256"]},
              {"logical_id", record["logical_id"]},
              {"implementation_sha256", source["kernel_sha256"]},
              {"header_sha256", record["header_sha256"]},
              {"contract_sha256", digest(stage / "contract.json")},
              {"environment_sha256", digest(stage / "environment.json")},
              {"call", raw["call"]},
              {"argument_index", 1},
              {"argument_type", "const std::string&"},
              {"quantifier", "argument"},
              {"all_of", array({object({{"id", "exact_text"},
                                        {"value", projection.fields}})})},
              {"scope", "Evidence applies to the named parameter projection. "
                        "Use this detached domain record for review only."}}));
  return finish_file_stage(work, package, pin, record["logical_id"].asString(),
                           "unllm.scoped-file-evidence/1");
}
} // namespace

Path qualify_zip_records(const Path &package, const std::string &pin,
                         const Path &registry,
                         const std::string &registry_pin) {
  const Projection projection{
      "zip_file_records",
      "application/zip",
      "ext,parent",
      "H058",
      "zip-records",
      array({"op_0_zip_file_records", "op_1_filter_records", "op_2_column",
             "op_3_unique_lines", "op_4_sort_lines", "write_lines"})};
  return qualify_records(package, pin, registry, registry_pin, projection);
}

Path qualify_tree_records(const Path &package, const std::string &pin,
                          const Path &registry,
                          const std::string &registry_pin) {
  const Projection projection{
      "tree_file_records",
      "inode/directory",
      "parent,ext",
      "H070",
      "tree-records",
      array({"op_0_tree_file_records", "op_1_annotate_group_measure",
             "op_2_annotate_group_measure", "op_3_filter_records",
             "op_4_filter_records", "op_5_column", "op_6_unique_lines",
             "op_7_sort_lines", "write_lines"})};
  return qualify_records(package, pin, registry, registry_pin, projection);
}

Path qualify_json_records(const Path &package, const std::string &pin,
                          const Path &registry,
                          const std::string &registry_pin) {
  const Projection projection{
      "read_json_records",
      "application/json",
      R"(["sensor", "room", "minute", "temp"])",
      "H035",
      "json-records",
      array({"op_0_read_json_records", "op_1_derive_integer",
             "op_2_annotate_group_measure", "op_3_annotate_group_measure",
             "op_4_derive_integer", "op_5_group_rows", "op_6_select_group_row",
             "op_7_order_records", "op_8_report_fields", "write_lines"})};
  return qualify_records(package, pin, registry, registry_pin, projection);
}

namespace {
struct FiniteReader {
  std::string operation, fixture, mime, parameter;
  J values;
  std::string qualification_scope, domain_scope;
  bool readers_library;
  Strings client_libraries;
  void (*verify_materials)(const Path &, const Path &, const Path &, const J &,
                           const J &);
};

J uri_projection_cases(J cases, const std::string &projection) {
  Strings fields;
  std::istringstream names(projection);
  for (std::string name; std::getline(names, name, ',');)
    fields.push_back(name);
  for (auto &test : cases) {
    auto projected = array();
    std::string output;
    int position = 0;
    for (const auto &record : std::as_const(test)["rows"]) {
      auto row = array();
      output += std::to_string(++position);
      for (const auto &field : fields) {
        check(record[field].isString(), "missing expected URI field: " + field);
        row.append(record[field]);
        output += '\t' + record[field].asString();
      }
      output += '\n';
      projected.append(row);
    }
    test["rows"] = projected;
    if (!test.isMember("error"))
      test["output"] = output;
  }
  return cases;
}

Path qualify_finite_reader(const Path &package, const std::string &pin,
                           const Path &registry, const std::string &registry_pin,
                           const FiniteReader &reader) {
  verify(package, pin);
  const auto &operation = reader.operation;
  const auto &modes = reader.values;
  const bool uri = operation == "uri_records";
  const auto src = sources()["retained-" + reader.fixture];
  const auto job = retained_binding_job(v5_root() / "sources.json", operation);
  const auto selector = object({{"parameter", reader.parameter}, {"argument_index", 1},
                                {"argument_type", "const std::string&"},
                                {"values", modes}});
  check(src["approval"] == "local-pilot" && src["method"] == "signed-local-package" &&
            job["source"] == src["id"] && job["route"] == "api" &&
            job["stream"] == "foundational" &&
            job["tests"] == "v5/fixtures/" + reader.fixture + ".json" &&
            job["selector"] == selector,
        "no reviewed local file reader selector job");
  const auto priority = rank_supplied_usage(
      project_file(runs, job["coverage_audit"].asString()),
      job["coverage_sha256"].asString(),
      project_file(runs, job["migration_snapshot"].asString() + "/ledger.sqlite")
          .parent_path(),
      job["ledger_sha256"].asString());
  const auto work = fresh_run("v5-" + reader.fixture), stage = work / ".stage",
             scratch = work / ".test-programs", fixtures = stage / "fixtures",
             bundle = package / "bundle", base = verified_base();
  fs::create_directory(scratch);
  save_file_registry(stage, src, registry, registry_pin);
  check(retained_binding_job(stage / "sources.json", operation) == job,
        "changed file reader selector job");
  const auto defs = load(bundle / "providers/contracts.json");
  const auto row = select_binding(defs["providers"], operation,
                                  "miner_" + operation, "in:" + reader.mime, "rows");
  const auto record = file_binding_identity(row, bundle);
  const auto selection = select_ranked_scope(package, pin, priority, job);
  const auto &selected = selection["selected"];
  const auto legacy = load(bundle / (reader.readers_library ? readers_history : history) /
                           "contracts.json")[operation];
  check(selected["binding_id"] == operation &&
            selected["binding_sha256"] == record["binding_sha256"] &&
            selected["header_sha256"] == record["header_sha256"] &&
            row["call"] == job["callable"] && row["args"] == array({reader.parameter}) &&
            row["from"] == legacy["from"] && row["to"] == legacy["to"] &&
            legacy["args"] == object({{reader.parameter, "text"}}) &&
            defs["outcomes"][operation]["parameters"] == legacy["args"],
        "file reader qualification differs from ranked ledger or retained ABI");
  write(stage / "priority.json", priority);
  write(stage / "selection.json", selection);
  reader.verify_materials(stage, bundle, base, src, row);
  const auto source = load(stage / "source.json");
  for (const auto &[from, to] : std::map<std::string, std::string>{
           {reader.fixture + ".json", "spec.json"},
           {reader.fixture + "-client.cpp", "client.cpp"},
           {reader.fixture + "-annotation.json", "annotation.json"}})
    fs::copy_file(v5_root() / "fixtures" / from, stage / to);
  const auto spec = load(stage / "spec.json");
  check(spec["binding"] == record["key"] &&
            spec["parameters"] == object({{reader.parameter, modes}}) &&
            (uri ? spec["cases"].isArray()
                 : spec["cases"].getMemberNames() == strings(modes)),
        "file fixture differs from the reviewed selector values");
  for (const auto &name : spec["inputs"].getMemberNames()) {
    const auto &input = spec["inputs"][name];
    if (input.isObject()) {
      const auto hex = QByteArray::fromStdString(input["hex"].asString());
      const auto data = QByteArray::fromHex(hex);
      check(data.toHex() == hex, "noncanonical file fixture hex");
      save(fixtures / name, data.toStdString());
    } else if (input.isString()) {
      save(fixtures / name, input.asString());
    }
  }
  const auto before = tree_hashes(fixtures);
  const auto restore = pin_runtime_path(bundle);
  const auto tools = file_qualification_tools();
  const auto exe = build_record_client(stage, scratch, bundle, source, tools,
                                       reader.readers_library, false, reader.client_libraries);
  auto observe = [&](const J &test, const std::string &mode) {
    const auto name = test["name"].asString();
    const auto input = test.get("input_case", test["name"]).asString();
    check(spec["inputs"].isMember(input), "file test has no input declaration");
    const auto seen = probe_record_rows(exe, fixtures, bundle, input, mode);
    check_record_rows(test, seen, operation, true);
    return object({{"name", name}, {"observation", seen}});
  };
  auto direct = object(), trials = object();
  for (const auto &mode : modes) {
    const auto value = mode.asString();
    auto cases = uri ? uri_projection_cases(spec["cases"], value) : spec["cases"][value];
    auto observations = array();
    for (const auto &test : cases)
      observations.append(observe(test, value));
    direct[value] = observations;
    trials[value] = cases;
  }
  auto standalone = object({{"processes_per_case", 2}, {"cases", direct},
                              {"input_sha256", before}});
  if (uri) {
    auto controls = array();
    for (const auto &test : spec["selector_controls"])
      controls.append(observe(test, test["selector"].asString()));
    standalone["selector_controls"] = controls;
  } else {
    auto invalid = array();
    for (const auto &test : spec["invalid_header"])
      invalid.append(observe(test, "auto"));
    standalone["invalid_header"] = invalid;
  }
  check(tree_hashes(fixtures) == before, "file reader changed a fixture");
  write(stage / "standalone.json", standalone);

  auto integration = object();
  auto conditions = array();
  const auto chain = array({"op_0_" + operation, "op_1_report_fields", "write_lines"});
  auto prepare = [&](const Path &dir, const J &test) {
    const auto fixture = fixtures / test["name"].asString();
    if (fs::exists(fixture))
      fs::copy_file(fixture, dir / "in");
  };
  for (const auto &mode : modes) {
    const auto value = mode.asString();
    std::string fields = "pos,0,1,2";
    if (uri) {
      fields = "pos";
      const auto count = std::count(value.begin(), value.end(), ',') + 1;
      for (std::ptrdiff_t i = 0; i < count; ++i)
        fields += "," + std::to_string(i);
    }
    const auto ops = array(
        {object({{"outcome", operation}, {"parameters", object({{reader.parameter, value}})}}),
         object({{"outcome", "report_fields"},
                 {"parameters", object({{"fields", fields}})}})});
    const auto goal = spec_for(
        reader.mime, "text/plain", object({{"operations", ops}, {"uncovered", array()}}));
    integration[value] = file_record_trial(
        work, bundle, base, row, goal, chain, trials[value], false, prepare, value);
    conditions.append(object({{"id", "exact_text"}, {"value", value}}));
  }
  check(file_qualification_tools() == tools, "qualification tools changed");
  write(stage / "integration.json", integration);
  auto contract = file_contract(stage, package, record, src, stage / "sources.json");
  contract["source"]["implementation_sha256"] = source["kernel_sha256"];
  contract["qualification"]["scope"] = reader.qualification_scope;
  validate(contract, load(package / "contract.schema.json"));
  write(stage / "contract.json", contract);
  write(stage / "domain.json",
        object({{"schema", "unllm.scoped-file-evidence-domain/1"},
                {"binding_key", record["key"]},
                {"raw_binding_sha256", record["binding_sha256"]},
                {"logical_id", record["logical_id"]},
                {"implementation_sha256", source["kernel_sha256"]},
                {"header_sha256", record["header_sha256"]},
                {"contract_sha256", digest(stage / "contract.json")},
                {"environment_sha256", digest(stage / "environment.json")},
                {"call", row["call"]},
                {"argument_index", selector["argument_index"]},
                {"argument_type", selector["argument_type"]},
                {"quantifier", "argument"},
                {"any_of", conditions},
                {"scope", reader.domain_scope}}));
  return finish_file_stage(work, package, pin, record["logical_id"].asString(),
                           "unllm.scoped-file-evidence/1");
}
} // namespace

Path qualify_csv_records(const Path &package, const std::string &pin,
                         const Path &registry, const std::string &registry_pin) {
  return qualify_finite_reader(
      package, pin, registry, registry_pin,
      {"read_csv_records", "csv-records", "text/csv", "header",
       array({"absent", "present"}),
       "Detached local CSV byte and file-effect evidence for the absent and present header modes.",
       "Each exact header mode has separate direct and file evidence. Input-domain behavior is bounded to the recorded cases.",
       true, {},
       [](const Path &stage, const Path &bundle, const Path &base,
          const J &src, const J &row) {
         verify_reader_materials(stage, bundle, base, src, row,
                                 {"src/execution/operator/csv_scanner/scanner/string_value_scanner.cpp"});
       }});
}

Path qualify_uri_records(const Path &package, const std::string &pin,
                         const Path &registry, const std::string &registry_pin) {
  return qualify_finite_reader(
      package, pin, registry, registry_pin,
      {"uri_records", "uri-records", "text/uri-list", "fields",
       array({"base,original", "host,original,scheme", "original", "original,scheme",
              "original,scheme,has_fragment", "original,scheme,host,base,has_fragment", "scheme"}),
       "Detached local byte and file-effect evidence for seven exact ordered URI field projections.",
       "Each exact fields value has separate direct and file evidence. Input-domain behavior is bounded to the recorded cases.",
       false, {"liburiparser.so.1"}, verify_uri_materials});
}

namespace {
struct TupleReader {
  std::string operation, fixture, mime, output_fields;
  J arguments, parameters;
  std::string qualification_scope, domain_scope;
  Strings client_libraries;
  void (*verify_materials)(const Path &, const Path &, const Path &, const J &,
                           const J &);
};

Path qualify_tuple_reader(const Path &package, const std::string &pin,
                          const Path &registry, const std::string &registry_pin,
                          const TupleReader &reader) {
  verify(package, pin);
  const auto &operation = reader.operation;
  const auto &arguments = reader.arguments;
  const auto &parameters = reader.parameters;
  const auto src = sources()["retained-" + reader.fixture];
  const auto job = retained_binding_job(v5_root() / "sources.json", operation);
  check(src["approval"] == "local-pilot" && src["method"] == "signed-local-package" &&
            job["source"] == src["id"] && job["route"] == "api" &&
            job["stream"] == "foundational" &&
            job["tests"] == "v5/fixtures/" + reader.fixture + ".json" &&
            job["parameter"] == parameters,
        "no reviewed local file reader tuple job");
  const auto priority = rank_supplied_usage(
      project_file(runs, job["coverage_audit"].asString()),
      job["coverage_sha256"].asString(),
      project_file(runs, job["migration_snapshot"].asString() + "/ledger.sqlite")
          .parent_path(),
      job["ledger_sha256"].asString());
  const auto work = fresh_run("v5-" + reader.fixture), stage = work / ".stage",
             scratch = work / ".test-programs", fixtures = stage / "fixtures",
             bundle = package / "bundle", base = verified_base();
  fs::create_directory(scratch);
  fs::create_directories(fixtures);
  save_file_registry(stage, src, registry, registry_pin);
  check(retained_binding_job(stage / "sources.json", operation) == job,
        "changed file reader tuple job");
  const auto defs = load(bundle / "providers/contracts.json");
  const auto row = select_binding(defs["providers"], operation,
                                  "miner_" + operation, "in:" + reader.mime, "rows");
  const auto record = file_binding_identity(row, bundle);
  const auto selection = select_ranked_scope(package, pin, priority, job);
  const auto &selected = selection["selected"];
  const auto legacy = load(bundle / history / "contracts.json")[operation];
  auto expected_parameters = object();
  for (const auto &name : arguments)
    expected_parameters[name.asString()] = "text";
  check(selected["binding_id"] == operation &&
            selected["binding_sha256"] == record["binding_sha256"] &&
            selected["header_sha256"] == record["header_sha256"] &&
            row["call"] == job["callable"] && row["args"] == arguments &&
            row["from"] == legacy["from"] && row["to"] == legacy["to"] &&
            legacy["args"] == expected_parameters &&
            defs["outcomes"][operation]["parameters"] == legacy["args"],
        "file reader qualification differs from ranked ledger or retained ABI");
  write(stage / "priority.json", priority);
  write(stage / "selection.json", selection);
  reader.verify_materials(stage, bundle, base, src, row);
  const auto source = load(stage / "source.json");
  for (const auto &[from, to] : std::map<std::string, std::string>{
           {reader.fixture + ".json", "spec.json"},
           {reader.fixture + "-client.cpp", "client.cpp"},
           {reader.fixture + "-annotation.json", "annotation.json"}})
    fs::copy_file(v5_root() / "fixtures" / from, stage / to);
  const auto spec = load(stage / "spec.json");
  check(spec["binding"] == record["key"] && spec["parameters"] == parameters,
        "file fixture differs from the reviewed selector tuple");

  const auto restore = pin_runtime_path(bundle);
  const auto tools = file_qualification_tools();
  const auto exe = build_record_client(stage, scratch, bundle, source, tools,
                                       false, false, reader.client_libraries, true);
  for (const auto &name : spec["inputs"].getMemberNames()) {
    const auto &input = spec["inputs"][name];
    if (input.isNull())
      continue;
    if (input["sql"].isString()) {
      auto cmd = invoke(exe, fixtures, bundle);
      append(cmd, {"--make", name, input["sql"].asString()});
      run(cmd, {.seconds = 8, .probe_limits = true});
    } else {
      check(input["bytes"].isString(), "file fixture has no input bytes");
      save(fixtures / name, input["bytes"].asString());
    }
    fs::permissions(fixtures / name, fs::perms::owner_read | fs::perms::group_read |
                                         fs::perms::others_read);
  }
  const auto before = tree_hashes(fixtures);
  auto observe = [&](const J &test, const std::string &input, const J &params) {
    check(spec["inputs"].isMember(input), "file test has no input declaration");
    const auto seen = probe_record_rows(exe, fixtures, bundle, input, dump(params));
    check_record_rows(test, seen, operation, true);
    return object({{"name", test["name"]}, {"observation", seen}});
  };
  auto direct = array(), controls = array();
  for (const auto &test : spec["cases"])
    direct.append(observe(test, test["name"].asString(), parameters));
  for (const auto &test : spec["selector_controls"])
    controls.append(observe(test, test["input_case"].asString(), test["params"]));
  check(tree_hashes(fixtures) == before, "file reader changed the fixture inventory");
  write(stage / "standalone.json",
        object({{"processes_per_case", 2}, {"cases", direct},
                {"input_sha256", before}, {"selector_controls", controls}}));

  const auto chain = array({"op_0_" + operation, "op_1_report_fields", "write_lines"});
  const auto ops = array(
      {object({{"outcome", operation}, {"parameters", parameters}}),
       object({{"outcome", "report_fields"},
               {"parameters", object({{"fields", reader.output_fields}})}})});
  const auto goal = spec_for(
      reader.mime, "text/plain", object({{"operations", ops}, {"uncovered", array()}}));
  auto prepare = [&](const Path &dir, const J &test) {
    const auto fixture = fixtures / test["name"].asString();
    if (fs::exists(fixture))
      fs::copy_file(fixture, dir / "in");
  };
  write(stage / "integration.json",
        file_record_trial(work, bundle, base, row, goal, chain, spec["cases"], false, prepare));
  check(file_qualification_tools() == tools, "qualification tools changed");

  auto contract = file_contract(stage, package, record, src, stage / "sources.json");
  contract["source"]["implementation_sha256"] = source["kernel_sha256"];
  contract["qualification"]["scope"] = reader.qualification_scope;
  validate(contract, load(package / "contract.schema.json"));
  write(stage / "contract.json", contract);
  write(stage / "domain.json",
        object({{"schema", "unllm.scoped-file-evidence-domain/1"},
                {"binding_key", record["key"]},
                {"raw_binding_sha256", record["binding_sha256"]},
                {"logical_id", record["logical_id"]},
                {"implementation_sha256", source["kernel_sha256"]},
                {"header_sha256", record["header_sha256"]},
                {"contract_sha256", digest(stage / "contract.json")},
                {"environment_sha256", digest(stage / "environment.json")},
                {"call", row["call"]},
                {"quantifier", "arguments"},
                {"all_of", file_argument_scope(arguments, parameters)},
                {"scope", reader.domain_scope}}));
  return finish_file_stage(work, package, pin, record["logical_id"].asString(),
                           "unllm.scoped-file-evidence/1");
}
} // namespace

Path qualify_sqlite_records(const Path &package, const std::string &pin,
                            const Path &registry, const std::string &registry_pin) {
  return qualify_tuple_reader(
      package, pin, registry, registry_pin,
      {"sqlite_records", "sqlite-records", "application/vnd.sqlite3", "pos,0,1,2,3,4",
       array({"table", "fields", "order_by"}),
       object({{"table", "scans"}, {"fields", "parcel,depot,state,minute,seq"},
               {"order_by", "seq:asc"}}),
       "Detached local evidence for the exact scans table, five selected columns and seq:asc.",
       "The three selector predicates apply together. Input-domain behavior is bounded to the recorded cases.",
       {"libsqlite3.so.0"}, verify_sqlite_materials});
}

Path qualify_xml_records(const Path &package, const std::string &pin,
                         const Path &registry, const std::string &registry_pin) {
  return qualify_tuple_reader(
      package, pin, registry, registry_pin,
      {"xml_records", "xml-records", "application/xml", "pos,0,1,2,3",
       array({"records", "fields"}),
       object({{"records", "/feed/article"}, {"fields", "@editor,@section,@state,title"}}),
       "Detached local evidence for the exact article record and relative field XPath selectors.",
       "The two selector predicates apply together. Input-domain behavior is bounded to the recorded cases.",
       {}, verify_xml_materials});
}
} // namespace v5
