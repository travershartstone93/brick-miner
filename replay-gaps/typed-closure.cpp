#include "row-trial.hpp"
#include <algorithm>
#include <charconv>
#include <cstdio>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <tuple>

using namespace miner;
using namespace row_replay;
namespace {
const std::string tsv = "text/tab-separated-values";
const Strings splits{"ordinary", "boundary", "empty", "transfer", "sparse"};
const Path response = "run/calls/interpret/response.json";
const Path emission = "run/components/emission.json";
const Strings remaining{"H016", "H065"};

std::string lines(const auto &values) {
  std::string result;
  for (const auto &value : values)
    result += value + '\n';
  return result;
}

// This oracle covers fixture references that fit on one TSV line.
Strings csv_fields(const std::string &line) {
  std::istringstream row(line);
  Strings fields;
  do {
    std::string field;
    if (row.peek() == '"') {
      row.get();
      for (;;) {
        const auto c = row.get();
        check(c != EOF, "unclosed fixture CSV quote");
        if (c == '"') {
          if (row.peek() != '"')
            break;
          row.get();
        }
        field += static_cast<char>(c);
      }
    } else {
      while (row.peek() != EOF && row.peek() != ',')
        field += static_cast<char>(row.get());
    }
    fields.push_back(std::move(field));
    if (row.eof())
      break;
    check(row.get() == ',', "expected fixture CSV delimiter");
  } while (!row.eof());
  return fields;
}

J finance(const std::string &input, bool any_negative_bank_entry) {
  std::istringstream file(input);
  std::string line;
  check(bool(std::getline(file, line)) &&
            (line == "side,ref,cents" || line == "side,ref,cents\r"),
        "unexpected ledger header");
  std::set<std::string> book, bank;
  std::map<std::string, long long> totals;
  while (std::getline(file, line)) {
    if (line.ends_with('\r'))
      line.pop_back();
    const auto fields = csv_fields(line);
    check(fields.size() == 3, "unexpected fixture CSV width");
    const auto &[side, ref, cents] = std::tie(fields[0], fields[1], fields[2]);
    long long amount = 0;
    const auto end = cents.data() + cents.size();
    const auto number = std::from_chars(cents.data(), end, amount);
    check(number.ec == std::errc{} && number.ptr == end,
          "expected fixture integer cents");
    if (side == "book") {
      if (amount < 0)
        book.insert(ref);
    } else {
      check(side == "bank", "unexpected fixture ledger side");
      if (amount < 0)
        bank.insert(ref);
      auto &total = totals[ref];
      check(amount < 0
                ? total >= std::numeric_limits<long long>::min() - amount
                : total <= std::numeric_limits<long long>::max() - amount,
            "fixture bank total exceeds oracle range");
      total += amount;
    }
  }
  if (!any_negative_bank_entry) {
    bank.clear();
    for (const auto &[ref, total] : totals)
      if (total < 0)
        bank.insert(ref);
  }
  return array({lines(book), lines(bank)});
}

struct Tree {
  J entries = object();
  Strings include_build, exclude_build;
};

Tree scan(const Path &root) {
  Tree result;
  for (const auto &entry : fs::recursive_directory_iterator(root)) {
    const auto rel = entry.path().lexically_relative(root);
    const auto name = rel.generic_string();
    const auto kind = entry.symlink_status();
    if (fs::is_directory(kind)) {
      result.entries[name] = object({{"kind", "directory"}});
      continue;
    }
    check(fs::is_regular_file(kind),
          "unexpected manifest fixture entry " + name);
    result.entries[name] =
        object({{"kind", "file"}, {"sha256", hash(entry.path())}});
    auto ext = rel.extension().string();
    for (auto &c : ext)
      if (c >= 'A' && c <= 'Z')
        c += 'a' - 'A';
    if (ext != ".cpp" && ext != ".hpp")
      continue;
    result.include_build.push_back(name);
    if (std::find(rel.begin(), rel.end(), Path("build")) == rel.end())
      result.exclude_build.push_back(name);
  }
  std::sort(result.include_build.begin(), result.include_build.end());
  std::sort(result.exclude_build.begin(), result.exclude_build.end());
  return result;
}

J manifest(const Tree &tree, bool include_build) {
  return array(
      {lines(include_build ? tree.include_build : tree.exclude_build)});
}

J all_passed() {
  auto result = object();
  for (const auto &split : splits)
    result[split] = true;
  return result;
}

void validate_prior(const Path &dir, const J &proof) {
  check(hash(dir / "receipt.json") == proof["receipt_sha256"].asString() &&
            hash(dir / emission) == proof["plan_sha256"].asString() &&
            scores(load(dir / "receipt.json")) == all_passed() &&
            proof["scores"] == all_passed(),
        "changed prior execution " + dir.string());
}

J tool_pins() {
  auto pins = tools();
  pins["unllm_logic_worker"] =
      hash(main_repo / "build/native/unllm_logic_worker");
  return pins;
}

const std::string mixed_csv =
    "side,ref,cents\r\nbank,offset,-5\r\nbook,z,-4\r\nbank,zero,-4\r\n"
    "book,\"A,ref\",-2\r\nbank,\"Q\"\"R\",-4\r\nbook,z,20\r\n"
    "bank,offset,8\r\nbank,zero,4\r\nbook,z,-1\r\nbank,\"Q\"\"R\",2\r\n"
    "book,offset,-0\r\nbank,n,0\r\nbank,n,-1";
} // namespace

int main() try {
  const auto previous = runs / "v5-typed-reconciliation-XXVKXZxL/report.json";
  const auto uri = runs / "v5-typed-uri-replay-XXUfWrWF";
  const auto previous_pin = hash(previous), uri_pin = hash(uri / "report.json");
  check(previous_pin == "3f6359bb7836418e0f3d6356e12275cfd600d57078869db528741c"
                        "adb6cb2f8e" &&
            uri_pin == "5672e6e4980c2cb1a2b56626209e3fd720101fcdf48b3fa1c90b2d1"
                       "6c8575f80",
        "changed typed cohort checkpoints");
  const auto ledger = load(previous), uri_report = load(uri / "report.json");
  const auto parent = runs / "v5-queue-XXH5XMi9/stage/package";
  const auto stage = runs / "v5-tsv-writer-XXAf0x0y/stage";
  const auto base = parent / "bundle", staged = stage / "bundle";
  const auto active = fs::canonical(volume / "unllm/boilerplate/active");
  const auto pins = tool_pins();
  check(uri_report["reconciliation_sha256"] == previous_pin.c_str() &&
            uri_report["remaining"] == string_array(remaining) &&
            uri_report["tools"] == pins &&
            hash(parent / "manifest.json") ==
                ledger["parent_manifest_sha256"].asString() &&
            hash(stage / "manifest.json") ==
                ledger["writer_manifest_sha256"].asString() &&
            active.string() == ledger["active_path"].asString() &&
            hash(active / "SHA256SUMS") == ledger["active_sha256"].asString(),
        "changed replay context");
  verify_bundle(
      base, "436db92629463daee559de2cbbc1c81519e12306d5bf01cc6d2883f3140e779c");
  verify_bundle(
      staged,
      "b2f8a40d44b1589779fc82fcfa23995df5cb2b2432167e9383f6f22e9c828d54");
  const auto writer =
      exact_row(staged, "write_tsv_lines", "strings", "out:" + tsv);
  check(writer == ledger["writer"] &&
            remove_binding(read_tree(staged), writer).at("SHA256SUMS") ==
                bytes(base / "SHA256SUMS"),
        "parent differs outside the exact writer registration");

  const auto work = fresh_run("v5-typed-closure-replay");
  std::cout << work.string() << std::endl;
  auto report =
      object({{"reconciliation_sha256", previous_pin},
              {"uri_report_sha256", uri_pin},
              {"active_path", active.string()},
              {"active_sha256", ledger["active_sha256"]},
              {"parent_manifest_sha256", ledger["parent_manifest_sha256"]},
              {"writer_manifest_sha256", ledger["writer_manifest_sha256"]},
              {"writer", writer},
              {"tools", pins}});
  const auto fixture_tree = work / "trees/main";
  for (const auto &file :
       Strings{".cache/.choice.HpP", "Build/upper.CPP", "build.cpp",
               "build-tools/stay.hpp", "build/no.CPP", "lib/build-2/name.cpp",
               "lib/build/deep/no.hpp", "rebuild/stay.HPP", "src/Main.CpP",
               "src/art.cpp/note.txt", "src/odd.cpp.bak"})
    save(fixture_tree / file, "payload");
  fs::create_directories(fixture_tree / "src/empty.HPP");
  const auto tree = scan(fixture_tree);
  report["fresh_tree"] = tree.entries;
  check(finance(mixed_csv, false) == array({"A,ref\nz\n", "Q\"R\nn\n"}) &&
            finance(mixed_csv, true) ==
                array({"A,ref\nz\n", "Q\"R\nn\noffset\nzero\n"}) &&
            manifest(tree, false) ==
                array({".cache/.choice.HpP\nBuild/upper.CPP\n"
                       "build-tools/stay.hpp\nbuild.cpp\nlib/build-2/name.cpp\n"
                       "rebuild/stay.HPP\nsrc/Main.CpP\n"}),
        "lost a frozen fixture distinction");

  const auto archive = runs / "english-audit-bcsgf8pi/replay-staged";
  const auto transfer = runs / "shared-review-5ui86km8/transfer";
  std::vector<Path> programs;
  unsigned output_count = 0;
  for (const auto &id : remaining) {
    const bool csv = id == "H016";
    const std::string input_name = csv ? "ledger.csv" : "project";
    const Strings names =
        csv ? Strings{"result-1.txt", "result-2.txt"} : Strings{"result-1.txt"};
    const std::string control = csv ? "any-negative-bank" : "include-build";
    const auto &record = ledger["requests"][id];
    const auto reply = archive / id / response;
    const auto saved = load(reply);
    check(record["execution_status"] == "pending_scoped_replay" &&
              hash(reply) == record["reply_sha256"].asString() &&
              hash(archive / id / "request.txt") ==
                  record["request_sha256"].asString() &&
              saved["input_type"] == record["archived_input_type"] &&
              saved["roles"].size() == names.size(),
          "changed saved request " + id);
    auto &entry = report["requests"][id];
    entry["request_sha256"] = record["request_sha256"];
    entry["reply_sha256"] = record["reply_sha256"];
    auto spec = saved;
    spec["input_type"] = record["supplied_input_type"];
    for (auto &role : spec["roles"]) {
      const auto &earlier = record["roles"][role["id"].asString()];
      auto &need = role["requirement"];
      check(role["output_type"] == tsv &&
                need["operations"] == earlier["archived_operations"] &&
                need["uncovered"] == earlier["archived_uncovered"],
            "changed saved role " + id);
      need["operations"] = earlier["supplied_operations"];
      need["uncovered"] = array();
    }
    auto altered = spec;
    if (csv) {
      altered["roles"][1]["requirement"]["operations"] =
          spec["roles"][0]["requirement"]["operations"];
      altered["roles"][1]["requirement"]["operations"][1]["parameters"]
             ["value"] = "bank";
    } else {
      J removed;
      check(altered["roles"][0]["requirement"]["operations"].removeIndex(
                1, &removed) &&
                removed["outcome"] == "filter_records",
            "changed exact component filter");
    }

    std::map<std::string, Tree> saved_trees;
    auto &inputs = entry["fixture_input_sha256"];
    for (const auto &split : splits) {
      const auto path =
          transfer / id / "fixtures" / split / "input" / input_name;
      if (csv) {
        inputs[split] = hash(path);
      } else {
        const auto input = scan(path);
        entry["fixture_trees"][split] = input.entries;
        inputs[split] = sha(dump(input.entries));
        saved_trees.emplace(split, input);
      }
    }
    Strings arms{"without-writer", "supplied", control};
    if (csv)
      arms.push_back("plain-input");
    for (const auto &arm : arms) {
      const bool missing = arm == "without-writer",
                 wrong = arm == "plain-input", changed = arm == control;
      const auto &bundle = missing ? base : staged;
      auto supplied = changed ? altered : spec;
      if (wrong)
        supplied["input_type"] = saved["input_type"];
      const auto file = work / "specs" / id / (arm + ".json");
      write(file, supplied);
      const auto result =
          csv ? trial(work, arm, id, bundle, file, input_name, inputs)
              : trial(work, arm, id, bundle, file);
      auto &evidence = entry["arms"][arm];
      evidence = result;
      const auto dir = work / arm / id;
      const auto project = dir / "run/project", exe = project / "converter";
      if (!result["program_sha256"].isNull()) {
        programs.push_back(exe);
        evidence["source_sha256"] = hash(project / "converter.cpp");
        evidence["packages_sha256"] = hash(project / "packages.txt");
      }
      write(work / "progress.json", report);
      if (wrong) {
        check(result["roles"].empty(), "plain text input unexpectedly planned");
        for (const auto &role : spec["roles"]) {
          const auto key = role["id"].asString();
          check(result["planner_diagnostics"][key] ==
                    record["roles"][key]["supplied_as_plain_text"]["gaps"]["r"],
                "plain input stopped at a different boundary: " +
                    dump(result["planner_diagnostics"]));
        }
      } else {
        check(result["roles"].size() == names.size(),
              "missing emitted role " + id);
        for (unsigned slot = 0; slot < names.size(); ++slot) {
          const auto &role = result["roles"][std::to_string(slot)];
          const auto key = spec["roles"][slot]["id"].asString();
          if (!changed) {
            const auto &prior =
                record["roles"][key][missing ? "supplied_without_writer"
                                             : "supplied_with_writer"]["routes"]
                      [0];
            check(role["steps"] == prior["steps"] &&
                      ports(role) == prior["ports"] &&
                      role["status"] == prior["status"],
                  "emitted route differs from typed reconciliation " + id);
          }
          if (missing)
            check(ports(role) == array({array({"strings", "out:" + tsv})}),
                  "writer removal changed an upstream port");
          else
            check(complete(role) && selects(role, "write_tsv_lines"),
                  "output role is incomplete " + id);
        }
      }

      const bool blocked = missing || wrong;
      bool verified = !blocked;
      for (const auto &split : splits) {
        const auto fixture = transfer / id / "fixtures" / split;
        auto original = array(), predicted = array();
        if (csv) {
          const auto input = bytes(fixture / "input" / input_name);
          original = finance(input, false);
          predicted = finance(input, changed);
        } else {
          const auto &input = saved_trees.at(split);
          const auto replay_split =
              dir /
              (split == "transfer" || split == "sparse" ? "held"
                                                        : "run/check-initial") /
              split;
          check(scan(replay_split / "input" / input_name).entries ==
                    input.entries,
                "checker generated a different directory tree " + split);
          original = manifest(input, false);
          predicted = manifest(input, changed);
        }
        for (unsigned slot = 0; slot < names.size(); ++slot) {
          const auto output = fixture / "output" / names[slot];
          check(bytes(output) == original[slot].asString(),
                "saved fixture differs from independent oracle " + id + " " +
                    split);
          entry["fixture_output_sha256"][split][names[slot]] = hash(output);
        }
        const auto passed = !blocked && original == predicted;
        verified &= passed;
        check(result["scores"][split] == passed &&
                  (blocked || result["outputs"][split] == predicted),
              "unexpected fixture verdict " + id + " " + arm + " " + split);
        if (arm == "supplied") {
          const auto exact =
              path_outputs(dir / "exact" / split,
                           fixture / "input" / input_name, exe, bundle, names);
          check(exact == original,
                "archive input route yielded different bytes");
          evidence["exact_outputs"][split] = exact;
          output_count += original.size();
        }
      }
      check(result["verified"] == verified,
            "unexpected combined verdict " + id + " " + arm);
      if (!blocked) {
        const auto intended =
            csv ? finance(mixed_csv, false) : manifest(tree, false);
        const auto predicted =
            csv ? finance(mixed_csv, changed) : manifest(tree, changed);
        if (csv) {
          evidence["fresh"] =
              extra_case(dir / "fresh", exe, bundle, mixed_csv, intended);
        } else {
          const auto actual =
              path_outputs(dir / "fresh", fixture_tree, exe, bundle, names);
          evidence["fresh"] = object({{"expected", intended},
                                      {"actual", actual},
                                      {"passed", actual == intended}});
        }
        write(work / "progress.json", report);
        check(predicted == evidence["fresh"]["actual"] &&
                  evidence["fresh"]["passed"] == !changed,
              "fresh witness yielded different bytes " + id + " " + arm);
      }
    }
  }

  auto &cohort = report["cohort"];
  unsigned roles = 0;
  for (const auto &id : ledger["requests"].getMemberNames()) {
    const auto &record = ledger["requests"][id];
    J item;
    if (record["execution_status"] == "verified_prior_replay") {
      item = record["retained_execution"];
      validate_prior(item["path"].asString(), item);
    } else if (uri_report["requests"].isMember(id)) {
      item = uri_report["requests"][id]["arms"]["supplied"];
      validate_prior(uri / "supplied" / id, item);
      item["path"] = (uri / "supplied" / id).string();
      item["role_count"] = item["roles"].size();
    } else {
      const auto &proof = report["requests"][id]["arms"]["supplied"];
      validate_prior(work / "supplied" / id, proof);
      item = proof;
      item["path"] = "supplied/" + id;
      item["role_count"] = item["roles"].size();
    }
    check(item["role_count"].isUInt() &&
              item["role_count"].asUInt() == record["roles"].size(),
          "cohort execution has a different role count " + id);
    roles += item["role_count"].asUInt();
    cohort[id] = object({{"path", item["path"]},
                         {"role_count", item["role_count"]},
                         {"receipt_sha256", item["receipt_sha256"]},
                         {"plan_sha256", item["plan_sha256"]}});
  }
  check(cohort.size() == 15 && roles == 20 && output_count == 15 &&
            tool_pins() == pins &&
            fs::canonical(volume / "unllm/boilerplate/active") == active &&
            hash(active / "SHA256SUMS") == ledger["active_sha256"].asString(),
        "changed closure counts or replay context");
  report["verified_requests"] = cohort.size();
  report["verified_roles"] = roles;
  report["verified_saved_fixtures"] = cohort.size() * splits.size();
  report["new_verified_saved_outputs"] = output_count;
  write(work / ".report.json", report);
  run({"sync", "-f", work});
  for (const auto &exe : programs)
    check(fs::remove(exe), "could not retire " + exe.string());
  fs::rename(work / ".report.json", work / "report.json");
  std::cout << hash(work / "report.json") << std::endl;
  return 0;
} catch (const std::exception &e) {
  std::cerr << e.what() << '\n';
  return 1;
}
