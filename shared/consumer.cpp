#include "qualification.hpp"
#include <QByteArray>
#include <archive.h>
#include <archive_entry.h>
#include <cstdio>
#include <sqlite3.h>

namespace shared {
std::string string_literal(const std::string &s) {
  std::string escaped;
  for (unsigned char c : s) {
    char digits[5];
    std::snprintf(digits, sizeof(digits), "\\%03o", unsigned(c));
    escaped += digits;
  }
  return "std::string(\"" + escaped + "\"," + std::to_string(s.size()) + ")";
}
void materialize(const Path &file, const J &data) {
  if (data.isString()) {
    save(file, data.asString());
    return;
  }
  auto decode = [](const J &value) {
    auto hex = QByteArray::fromStdString(value.asString()),
         out = QByteArray::fromHex(hex);
    check(out.toHex() == hex.toLower(), "invalid fixture byte string");
    return out.toStdString();
  };
  if (data.isMember("tree")) {
    fs::create_directory(file);
    auto child = [&](const std::string &name) {
      Path p = name;
      check(!p.empty() && !p.is_absolute() && p == p.lexically_normal() &&
                *p.begin() != "..",
            "unsafe tree fixture path");
      return file / p;
    };
    for (const auto &row : data["tree"])
      save(child(row[0].asString()), decode(row[1]));
    for (const auto &name : data["symlinks"].getMemberNames())
      fs::create_symlink(data["symlinks"][name].asString(), child(name));
    return;
  }
  if (data.isMember("sqlite")) {
    sqlite3 *handle = nullptr;
    int rc = sqlite3_open(file.c_str(), &handle);
    std::unique_ptr<sqlite3, decltype(&sqlite3_close)> db(handle,
                                                          sqlite3_close);
    check(rc == SQLITE_OK, "cannot open fixture database");
    rc = sqlite3_exec(handle, data["sqlite"].asCString(), nullptr, nullptr,
                      nullptr);
    check(rc == SQLITE_OK, sqlite3_errmsg(handle));
    return;
  }
  check(data["zip"].isArray(), "unknown fixture type");
  std::unique_ptr<archive, decltype(&archive_write_free)> writer(
      archive_write_new(), archive_write_free);
  auto *arc = writer.get();
  check(archive_write_set_format_zip(arc) == ARCHIVE_OK &&
            archive_write_set_options(arc, "zip:compression=deflate") ==
                ARCHIVE_OK &&
            archive_write_open_filename(arc, file.c_str()) == ARCHIVE_OK,
        "cannot create ZIP fixture");
  for (const auto &row : data["zip"]) {
    std::unique_ptr<archive_entry, decltype(&archive_entry_free)> e(
        archive_entry_new(), archive_entry_free);
    auto raw = decode(row[1]);
    archive_entry_set_pathname(e.get(), row[0].asCString());
    archive_entry_set_filetype(e.get(), AE_IFREG);
    archive_entry_set_perm(e.get(), 0600);
    archive_entry_set_size(e.get(), raw.size());
    check(archive_write_header(arc, e.get()) == ARCHIVE_OK &&
              archive_write_data(arc, raw.data(), raw.size()) ==
                  la_ssize_t(raw.size()),
          "cannot write ZIP fixture");
  }
  check(archive_write_close(arc) == ARCHIVE_OK, "cannot finish ZIP fixture");
}
void consumer(const J &c, const J &contract, const Path &dest) {
  auto name = c["brick"].asString();
  bool reader = contract["from"].asString().starts_with("in:"),
       rows = contract["to"] == "rows";
  Strings args = {reader ? "file" : "brick_data::tab_records(file)"};
  for (const auto &a : c["args"])
    args.push_back(a.isString() ? string_literal(a.asString()) : a.asString());
  auto call = "brick_miner::shared::" + name + "(" + join(args, ", ") + ")";
  std::string header =
                  "#include \"miner_" + name +
                  ".hpp\"\n#include \"tab_provider.hpp\"\n#include <cstdio>\n",
              body;
  Strings expected;
  if (rows) {
    header +=
        "using "
        "Output=std::vector<std::pair<std::int64_t,brick_data::Fields>>;\n";
    const auto &positions = c["positions"];
    check(positions.isNull() || positions.size() == c["rows"].size(),
          "wrong fixture position count");
    for (unsigned i = 0; i < c["rows"].size(); ++i) {
      Strings fields;
      for (const auto &f : c["rows"][i])
        fields.push_back(string_literal(f.asString()));
      auto pos =
          positions.isNull() ? std::to_string(i + 1) : positions[i].asString();
      expected.push_back("{" + pos + ",{" + join(fields, ",") + "}}");
    }
    body = "Output out; " + call +
           "([&](std::int64_t i,const brick_data::Fields& "
           "f){out.emplace_back(i,f);});return out;";
  } else {
    for (const auto &f : c["strings"])
      expected.push_back(string_literal(f.asString()));
    header += "using Output=std::vector<std::string>;\n";
    body = "return " + call + ";";
  }
  save(dest / "case.hpp",
       header + "const Output expected={" + join(expected, ",") +
           "};\nOutput collect(const char* file){" + body + "}\n");
}
void compare(const Path &pair, const Path &bundle, const Path &work) {
  auto extras = load(here() / "tests/additional-cases.json");
  auto result = object();
  for (const auto &batch : batches) {
    if (!fs::is_directory(bundle / "qualifications/shared" / batch))
      continue;
    auto started = std::chrono::steady_clock::now();
    auto mined = stage_owner(pair, batch),
         frozen = mined.parent_path() / "miner-inputs",
         dest = work / "comparison" / batch;
    fs::create_directories(dest);
    Strings cmd = {"clang++",
                   "-std=c++17",
                   "-O1",
                   "-fPIC",
                   "-DNDEBUG",
                   "-shared",
                   "-Wl,-z,defs",
                   "-Wl,-rpath,$ORIGIN",
                   "-I" + frozen.string(),
                   "-I" + (bundle / "providers").string(),
                   "-I" + (bundle / "headers").string()};
    Strings libs;
    auto lib = "libbrick_shared_" + batch + ".so";
    if (batch == "inputs") {
      auto sdk = mined / batch / "source/usr";
      cmd.push_back("-I" + (sdk / "include").string());
      append(cmd,
             command_args(object(
                 {{"command", run({"pkg-config", "--cflags", "glib-2.0"})}})));
      libs = {"-L" + (sdk / "lib").string(),
              "-lpugixml",
              "-larchive",
              "-lsqlite3",
              "-luriparser",
              "-lglib-2.0"};
      fs::copy_file(sdk / "lib/libpugixml.so.1", dest / "libpugixml.so.1");
    } else {
      auto sdk = mined / "sources/package/usr",
           source = mined / "sources/debug/usr/src/debug/duckdb/duckdb";
      append(cmd, {"-I" + (sdk / "include").string(),
                   "-I" + (source / "third_party/yyjson/include").string()});
      libs = {"-L" + (sdk / "lib").string(), "-lduckdb"};
    }
    append(cmd, {"-Wl,-soname," + lib, frozen / batch / "adapter.cpp"});
    append(cmd, libs);
    append(cmd, {"-o", dest / lib});
    run(cmd);
    auto seconds = std::chrono::duration<double>(
                       std::chrono::steady_clock::now() - started)
                       .count();
    auto runtime = bundle / "runtime/duckdb/lib";
    auto cases = load(frozen / batch / "cases.json"),
         contracts = load(frozen / batch / "contracts.json");
    J original = array(), additional = array();
    std::map<std::string, std::set<std::string>> inputs;
    for (const auto &c : cases) {
      auto id = c["id"].asString();
      auto dir = mined / batch / "checks" / id;
      original.append(id);
      inputs[c["brick"].asString()].insert(dump(c["input"]));
      for (const auto *path : {"input", "invalid", "nonexistent"})
        run({dir / "consumer", dir / path,
             std::string(path) == "input" ? "pass" : "fail"},
            {.environment = {
                 {"LD_LIBRARY_PATH", dest.string() + ":" + runtime.string()}}});
    }
    for (const auto &c : extras) {
      if (c["batch"] != batch)
        continue;
      auto key = c["brick"].asString(), id = c["id"].asString();
      additional.append(id);
      inputs[key].insert(dump(c["input"]));
      auto d = dest / id, exe = d / "consumer";
      fs::create_directory(d);
      consumer(c, contracts[key], d);
      run({"clang++", "-std=c++17", "-I", bundle / "headers", "-I",
           bundle / "providers", "-I", d, here() / "tests/consumer.cpp", "-o",
           exe, "-L" + runtime.string(), "-lbrick_shared_" + batch,
           "-lduckdb"});
      for (std::string label : {"input", "invalid", "missing"}) {
        auto path = d / label;
        if (label != "missing")
          materialize(path, c[label]);
        for (const auto &library :
             {dest, mined / batch / "coverage", runtime}) {
          run({exe, path, label == "input" ? "pass" : "fail"},
              {.environment = {{"LD_LIBRARY_PATH",
                                library.string() + ":" + runtime.string()},
                               {"LLVM_PROFILE_FILE",
                                (d / (label + ".profraw")).string()}}});
        }
      }
      run({"llvm-profdata", "merge", "-sparse", d / "input.profraw", "-o",
           d / "input.profdata"});
      auto exported =
          parse(run({"llvm-cov", "export", mined / batch / "coverage" / lib,
                     "-instr-profile=" + (d / "input.profdata").string()}));
      J hits = array();
      for (const auto &f : exported["data"][0]["functions"]) {
        auto fn = f["name"].asString();
        if (f["count"].asUInt64() &&
            fn.find("WindowAggregateExecutor") != fn.npos &&
            fn.find("EvaluateInternal") != fn.npos)
          hits.append(fn);
      }
      check(!hits.empty(), "supplemental test missed extracted function");
      write(d / "evidence.json", object({{"case", c}, {"executed", hits}}));
    }
    for (auto k : contracts.getMemberNames())
      check(inputs[k].size() >= 2, "insufficient distinct inputs for " + k);
    result[batch] =
        object({{"cases", original},
                {"additional_cases", additional},
                {"checks", 3 * (original.size() + additional.size())},
                {"binding_build_seconds", seconds},
                {"kernel_route", load(mined / batch / "timing.json")},
                {"direct_binding_sha256", sha(bytes(dest / lib))}});
  }
  write(work / "comparison.json", result);
}
} // namespace shared
