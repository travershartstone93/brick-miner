#include "row-trial.hpp"
#include "zip-fixture.hpp"
#include <algorithm>
#include <iostream>
#include <map>
#include <set>
#include <sstream>

using namespace miner;
using namespace row_replay;
namespace {
const std::string zip = "application/zip";
const Strings requests{"H051", "H052", "H055", "H057"};
const Strings splits{"ordinary", "boundary", "empty", "transfer", "sparse"};
const Path response = "run/calls/interpret/response.json";
const Path zipinfo = "/usr/bin/zipinfo";
const Path unzip = "/usr/bin/unzip";
const std::map<std::string, std::string> variants{{"H051", "alphabetical"},
                                                  {"H052", "count-entries"},
                                                  {"H055", "archive-order"},
                                                  {"H057", "archived"}};

const std::vector<ZipMember> witness{
    {"unused/"},           {"z/rootname.txt", 3}, {"zTop.txt"},
    {"dup/same.bin"},      {"empty-dir/"},        {"alpha/readme.txt", 4},
    {"dup/same.bin", 1},   {"rootname.txt"},      {"A/"},
    {"A/collide.bin", 1},  {"b/collide.bin", 2},  {"B/collide.bin", 3},
    {"A/collide.bin", 6},  {"A/Collide.bin"},     {"Root.CSV", 2},
    {"deep/dir/zero.dat"}, {"deep/dir/empty/"},   {"bare-empty"},
    {"z/final.txt"}};
const std::vector<ZipMember> dirs_only{{"only/"}, {"only/deep/"}, {"unused/"}};

std::string lines(const Strings &values) {
  return values.empty() ? std::string{} : join(values, "\n") + "\n";
}

std::vector<ZipMember> inspect(const Path &file) {
  const auto env = std::map<std::string, std::string>{
      {"LC_ALL", "C"},    {"TZ", "UTC"}, {"ZIPINFO", ""},
      {"ZIPINFOOPT", ""}, {"UNZIP", ""}, {"UNZIPOPT", ""}};
  std::istringstream listing(run({zipinfo, "-l", file}, {.environment = env}));
  std::vector<ZipMember> members;
  Strings paths;
  for (std::string line; std::getline(listing, line);) {
    if (line.empty() || (line[0] != '-' && line[0] != '?' && line[0] != 'd'))
      continue;
    std::istringstream row(line);
    std::string mode, version, platform, flags, method, date, time, name;
    unsigned size = 0, compressed = 0;
    check(bool(row >> mode >> version >> platform >> size >> flags >>
               compressed >> method >> date >> time) &&
              bool(std::getline(row, name)) && mode.size() == 10 &&
              name.starts_with(' '),
          "unrecognized Info-ZIP witness row: " + line);
    name.erase(0, 1);
    ZipMember member{name, size};
    check(member.directory() == (mode[0] == 'd'), "ZIP directory type differs");
    members.push_back(member);
    paths.push_back(name);
  }
  check(run({zipinfo, "-1", file}, {.environment = env}) == lines(paths),
        "Info-ZIP name order differs between listing forms");
  run({unzip, "-tqq", file}, {.environment = env});
  return members;
}

J manifest(const std::vector<ZipMember> &members) {
  auto rows = array();
  for (const auto &m : members)
    rows.append(object(
        {{"path", m.path}, {"size", m.size}, {"directory", m.directory()}}));
  return rows;
}

std::string json_array(const Strings &values) {
  Json::FastWriter writer;
  writer.omitEndingLineFeed();
  return writer.write(string_array(values));
}

bool same_report(const std::string &id, const J &actual, const J &expected) {
  if (id != "H051" && id != "H055")
    return actual == expected;
  return actual.size() == 1 && expected.size() == 1 &&
         parse(actual[0].asString()) == parse(expected[0].asString());
}

J oracle(const std::string &id, const std::vector<ZipMember> &members,
         std::string_view arm = "reference") {
  Strings output, nested, all;
  std::set<std::string> seen;
  std::map<std::string, std::set<std::string>> parents;
  std::map<std::string, unsigned> counts;
  for (const auto &m : members) {
    if (m.directory())
      continue;
    const auto slash = m.path.rfind('/');
    const bool root_file = slash == std::string::npos;
    const auto base = root_file ? m.path : m.path.substr(slash + 1);
    if (id == "H051") {
      if (seen.insert(base).second)
        output.push_back(base);
    } else if (id == "H052") {
      parents[base].insert(root_file ? "." : m.path.substr(0, slash));
      ++counts[base];
    } else if (id == "H055") {
      if (m.size == 0)
        output.push_back(m.path);
    } else {
      check(id == "H057", "unknown ZIP oracle");
      (root_file ? output : nested).push_back(m.path);
      all.push_back(m.path);
    }
  }
  if (id == "H051" || id == "H055") {
    if ((id == "H051" && arm == "alphabetical") ||
        (id == "H055" && arm != "archive-order"))
      std::sort(output.begin(), output.end());
    return array({json_array(output)});
  }
  if (id == "H052") {
    for (const auto &[base, folders] : parents) {
      const auto count =
          arm == "count-entries" ? counts.at(base) : folders.size();
      if (count > 1)
        output.push_back(base + "\t" + std::to_string(count));
    }
    return array({lines(output)});
  }
  return arm == "archived" ? array({lines(all), lines(all)})
                           : array({lines(output), lines(nested)});
}

J change(J spec, const std::string &id) {
  auto &steps = spec["roles"][0]["requirement"]["operations"];
  if (id == "H051") {
    steps.append(object({{"outcome", "sort_lines"}, {"parameters", object()}}));
  } else if (id == "H052") {
    check(steps[2]["outcome"] == "aggregate_groups" &&
              steps[2]["parameters"]["measure"] == "distinct",
          "changed basename grouping");
    steps[2]["parameters"]["measure"] = "count";
    steps[2]["parameters"]["value_column"] = 0;
  } else {
    J removed;
    check(id == "H055" && steps.removeIndex(steps.size() - 1, &removed) &&
              removed["outcome"] == "sort_lines",
          "changed ZIP path order recipe");
  }
  return spec;
}

J reader_provenance(const Path &bundle, const J &binding) {
  const auto stage = runs / "v5-zip-records-XXkiGzWl/stage";
  const auto pin = hash(stage / "manifest.json");
  check(pin ==
            "dc343f7f0c112b59f310b73d27900a19aec69f5a5e84d4a7673f36b5c3c85473",
        "changed prior ZIP evidence");
  const auto evidence = load(stage / "manifest.json");
  for (const auto *file : {"source.json", "domain.json", "contract.json"})
    check(hash(stage / file) == evidence["files"][file].asString(),
          "changed prior ZIP evidence member");
  const auto source = load(stage / "source.json");
  const auto domain = load(stage / "domain.json");
  check(source["binding"] == binding &&
            domain["header_sha256"] ==
                hash(bundle / "headers" / binding["header"].asString()) &&
            domain["contract_sha256"] == hash(stage / "contract.json") &&
            domain["all_of"] == array({object({{"id", "exact_text"},
                                               {"value", "ext,parent"}})}),
        "changed prior ZIP qualification scope");
  for (const auto &file : source["runtime_files"])
    check(hash(bundle / "runtime/duckdb/lib" / file["path"].asString()) ==
              file["sha256"].asString(),
          "changed retained ZIP runtime");
  return object({{"manifest_sha256", pin},
                 {"binding", binding},
                 {"prior_exact_selector", "ext,parent"},
                 {"runtime_files", source["runtime_files"]}});
}
} // namespace

int main() try {
  const auto rank_file = runs / "v5-residual-ranking-XXXSWRmG/report.json";
  const auto rank_pin = hash(rank_file);
  check(rank_pin ==
            "c83b0ad78c9d2054f17580b1047a5ec2ab30f833810e415391265be4341328c9",
        "changed residual ranking");
  const auto ranking = load(rank_file);
  check(ranking["open_families"][2]["name"] == "archives" &&
            ranking["open_families"][2]["requests"] == string_array(requests),
        "changed open archive cohort");
  const auto previous =
      runs / "v5-residual-directory-replay-XXHTdjz7/report.json";
  const auto previous_pin = hash(previous);
  check(previous_pin == "bace72893bc599004a2d0564c058213083257d1f396760bf565d21"
                        "cc72282b68" &&
            load(previous)["directories"].size() == 6,
        "changed preceding checkpoint");
  const auto recipes_file = root / "shared/request-recipes.json";
  const auto recipe_pin = hash(recipes_file);
  check(recipe_pin == ranking["recipes_sha256"].asString(),
        "changed supplied recipes");
  const auto recipes = load(recipes_file);
  const auto parent = runs / "v5-queue-XXH5XMi9/stage/package";
  const auto bundle = parent / "bundle";
  const auto parent_pin = hash(parent / "manifest.json");
  check(parent_pin == ranking["parent_manifest_sha256"].asString(),
        "changed V5 parent");
  verify_bundle(
      bundle,
      "436db92629463daee559de2cbbc1c81519e12306d5bf01cc6d2883f3140e779c");
  const auto active = fs::canonical(volume / "unllm/boilerplate/active");
  const auto active_pin = hash(active / "SHA256SUMS");
  check(active_pin == ranking["active_sha256"].asString(),
        "changed published catalog");

  const auto reader =
      exact_row(bundle, "zip_file_records", "in:" + zip, "rows");
  const auto names_reader =
      exact_row(bundle, "archive_files_zip", "in:" + zip, "strings");
  const auto lineage = reader_provenance(bundle, reader);
  const auto work = fresh_run("v5-residual-zip-replay");
  std::cout << work.string() << std::endl;
  auto report = object({{"active_path", active.string()},
                        {"active_sha256", active_pin},
                        {"ranking_sha256", rank_pin},
                        {"previous_sha256", previous_pin},
                        {"recipes_sha256", recipe_pin},
                        {"parent_manifest_sha256", parent_pin},
                        {"reader_provenance", lineage},
                        {"scalar_reader", names_reader},
                        {"tools", tools()}});
  report["tools"]["unllm_logic_worker"] =
      hash(main_repo / "build/native/unllm_logic_worker");
  report["tools"]["zipinfo"] = hash(zipinfo);
  report["tools"]["unzip"] = hash(unzip);

  const std::map<std::string, std::vector<ZipMember>> fresh{
      {"main", witness}, {"directories-only", dirs_only}};
  for (const auto &[name, members] : fresh) {
    const auto zip_file = work / (name + ".zip");
    write_zip_fixture(zip_file, members);
    check(inspect(zip_file) == members,
          "fresh Info-ZIP manifest differs for " + name);
    report["witness_archives"][name] =
        object({{"sha256", hash(zip_file)}, {"members", manifest(members)}});
  }
  check(
      oracle("H051", witness) ==
              array(
                  {R"(["rootname.txt","zTop.txt","same.bin","readme.txt","collide.bin","Collide.bin","Root.CSV","zero.dat","bare-empty","final.txt"])"}) &&
          oracle("H052", witness) ==
              array({"collide.bin\t3\nrootname.txt\t2\n"}) &&
          oracle("H055", witness) ==
              array(
                  {R"(["A/Collide.bin","bare-empty","deep/dir/zero.dat","dup/same.bin","rootname.txt","z/final.txt","zTop.txt"])"}) &&
          oracle("H057", witness)[0] ==
              "zTop.txt\nrootname.txt\nRoot.CSV\nbare-empty\n",
      "lost a frozen ZIP witness distinction");

  const auto archive = runs / "english-audit-bcsgf8pi/replay-staged";
  const auto historical = runs / "shared-review-5ui86km8/transfer";
  std::vector<Path> programs;
  for (const auto &id : requests) {
    const auto &record = ranking["requests"][id];
    const auto dir = archive / id;
    const auto saved_pin = hash(dir / response);
    const auto saved = load(dir / response);
    auto supplied = saved;
    auto &roles = supplied["roles"];
    check(supplied["input_type"] == zip && roles.size() == recipes[id].size() &&
              saved_pin == record["reply_sha256"].asString() &&
              hash(dir / "request.txt") ==
                  record["request_sha256"].asString() &&
              record["previously_verified"] == false,
          "changed ranked ZIP case " + id);
    auto &entry = report["archives"][id];
    entry["reply_sha256"] = saved_pin;
    Strings names;
    for (unsigned slot = 0; slot < roles.size(); ++slot) {
      auto &role = roles[slot];
      const auto name = role["id"].asString();
      const auto &ranked = record["roles"][name];
      const auto steps = operations(recipes[id][slot]);
      check(role["output_type"] == ranked["output_type"] &&
                role["requirement"]["operations"] ==
                    ranked["archived_operations"] &&
                role["requirement"]["uncovered"] == ranked["uncovered"] &&
                steps == ranked["supplied_operations"] &&
                steps[0]["outcome"] ==
                    (id == "H051" ? "archive_files" : "zip_file_records"),
            "changed ranked ZIP role " + id + "/" + name);
      role["requirement"]["operations"] = steps;
      role["requirement"]["uncovered"] = array();
      names.push_back(
          "result-" + std::to_string(slot + 1) +
          (role["output_type"] == "application/json" ? ".json" : ".txt"));
      entry["readers"].append(steps[0]);
    }
    const auto fixtures = historical / id / "fixtures";
    std::map<std::string, std::vector<ZipMember>> archive_members;
    for (const auto &split : splits)
      archive_members.emplace(split,
                              inspect(fixtures / split / "input/backup.zip"));
    const auto altered = variants.at(id);
    check(oracle(id, witness, altered) != oracle(id, witness),
          "fresh ZIP did not distinguish " + altered);
    for (const auto &arm : {std::string("reference"), altered}) {
      const auto file = work / "specs" / arm / (id + ".json");
      write(file, arm == "reference"  ? supplied
                  : arm == "archived" ? saved
                                      : change(supplied, id));
      const auto result = trial(work, arm, id, bundle, file);
      const auto trial_dir = work / arm / id;
      const auto exe = trial_dir / "run/project/converter";
      entry["runs"][arm] = result;
      write(work / "progress.json", report);
      check(fs::is_regular_file(exe) && result["roles"].size() == roles.size(),
            "incomplete ZIP execution");
      programs.push_back(exe);
      for (unsigned slot = 0; slot < roles.size(); ++slot) {
        const auto &role = result["roles"][std::to_string(slot)];
        const auto &steps = role["steps"];
        const auto &ranked = record["roles"][roles[slot]["id"].asString()];
        const auto sink = roles[slot]["output_type"] == "application/json"
                              ? "write_texts"
                              : "write_lines";
        check(steps[steps.size() - 1]["provider"] == sink &&
                  selects(role, id == "H051" || arm == "archived"
                                    ? "archive_files_zip"
                                    : "zip_file_records"),
              "changed emitted ZIP provider chain for " + id);
        if (arm == "archived")
          check(role["status"] == (slot == 0 ? "complete" : "partial") &&
                    steps == ranked["archived"]["routes"][0]["steps"],
                "H057 archived role no longer matches its ranked plan");
        else {
          check(complete(role), "supplied ZIP role did not complete");
          if (arm == "reference")
            check(steps == ranked["supplied"]["routes"][0]["steps"],
                  "emitted ZIP chain differs from ranked plan " + id);
        }
      }

      for (const auto &split : splits) {
        const auto fixture = fixtures / split;
        const auto input = fixture / "input/backup.zip";
        const auto generated =
            trial_dir /
            (split == "transfer" || split == "sparse" ? "held"
                                                      : "run/check-initial") /
            split / "input/backup.zip";
        const auto &members = archive_members.at(split);
        check(inspect(generated) == members,
              "regenerated ZIP member manifest differs for " + id + " " +
                  split);
        auto saved_outputs = array();
        for (const auto &name : names)
          saved_outputs.append(bytes(fixture / "output" / name));
        const auto intended = oracle(id, members);
        const auto predicted = oracle(id, members, arm);
        check(same_report(id, saved_outputs, intended) &&
                  same_report(id, result["outputs"][split], predicted) &&
                  result["scores"][split] == (predicted == intended),
              "saved ZIP output differs from oracle for " + id + " " + arm +
                  " " + split + ": " + dump(result["outputs"][split]));
        if (arm == "reference" || arm == "archived") {
          const auto exact = path_outputs(trial_dir / "exact" / split, input,
                                          exe, bundle, names);
          check(exact == result["outputs"][split] &&
                    (arm != "reference" || exact == saved_outputs),
                "byte-exact archived ZIP output differs for " + id + " " + arm);
          entry["exact"][arm][split] =
              object({{"input_sha256", hash(input)},
                      {"generated_sha256", hash(generated)},
                      {"ordered_members_sha256", sha(dump(manifest(members)))},
                      {"outputs", exact}});
        }
      }
      for (const auto &[name, members] : fresh) {
        const auto expected = oracle(id, members, arm);
        const auto actual =
            path_outputs(work / "witness" / id / arm / name,
                         work / (name + ".zip"), exe, bundle, names);
        check(same_report(id, actual, expected),
              "ZIP witness differs for " + id + " " + arm + " " + name + ": " +
                  dump(actual));
        entry["witness"][arm][name] = actual;
      }
      write(work / "progress.json", report);
    }
  }
  for (const auto &[name, members] : fresh) {
    const auto file = work / (name + ".zip");
    check(report["witness_archives"][name]["sha256"] == hash(file),
          "fresh ZIP was modified");
  }
  check(hash(active / "SHA256SUMS") == active_pin, "published catalog changed");
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
