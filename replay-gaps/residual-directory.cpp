#include "row-trial.hpp"
#include <algorithm>
#include <iostream>
#include <map>
#include <set>

using namespace miner;
using namespace row_replay;
namespace {
const std::string directory = "inode/directory";
const Strings splits{"ordinary", "boundary", "empty", "transfer", "sparse"};
const Strings requests{"H061", "H062", "H064", "H067", "H068", "H069"};
const Path response = "run/calls/interpret/response.json";
const std::map<std::string, std::string> variants{
    {"H061", "folded-names"}, {"H062", "include-root"},
    {"H064", "match-size"},   {"H067", "reuse-hidden-name"},
    {"H068", "count-files"},  {"H069", "first-deepest"}};

struct File {
  std::string path, parent, base, data;
  size_t depth;
  bool hidden_name, hidden_parent;
};

struct Tree {
  J entries = object();
  std::vector<File> files;
};

Tree scan(const Path &root) {
  Tree tree;
  for (const auto &entry : fs::recursive_directory_iterator(root)) {
    const auto rel = entry.path().lexically_relative(root);
    const auto name = rel.generic_string();
    const auto kind = entry.symlink_status();
    if (fs::is_directory(kind)) {
      tree.entries[name] = object({{"kind", "directory"}});
      continue;
    }
    check(fs::is_regular_file(kind), "unexpected fixture entry " + name);
    const auto parent = rel.parent_path();
    const auto basename = rel.filename().string();
    const auto depth =
        static_cast<size_t>(std::distance(parent.begin(), parent.end()));
    const bool hidden_parent =
        std::any_of(parent.begin(), parent.end(), [](const auto &component) {
          return component.string().starts_with('.');
        });
    auto data = bytes(entry.path());
    tree.entries[name] = object(
        {{"kind", "file"}, {"size", data.size()}, {"sha256", sha(data)}});
    tree.files.push_back(File{name, parent.generic_string(), basename,
                              std::move(data), depth, basename.starts_with('.'),
                              hidden_parent});
  }
  std::sort(tree.files.begin(), tree.files.end(),
            [](const File &a, const File &b) { return a.path < b.path; });
  return tree;
}

std::string lower_ascii(std::string value) {
  for (auto &c : value)
    if (c >= 'A' && c <= 'Z')
      c += 'a' - 'A';
  return value;
}

std::string lines(const Strings &values) {
  return values.empty() ? std::string{} : join(values, "\n") + "\n";
}

std::string json_array(const Strings &values) {
  Json::FastWriter writer;
  writer.omitEndingLineFeed();
  return writer.write(string_array(values));
}

bool same_report(const std::string &id, const J &actual, const J &expected) {
  if (id != "H062")
    return actual == expected;
  return actual.size() == 1 && expected.size() == 1 &&
         parse(actual[0].asString()) == parse(expected[0].asString());
}

J oracle(const std::string &id, const Tree &tree,
         std::string_view arm = "reference") {
  const auto &files = tree.files;
  Strings output;
  if (id == "H061" || id == "H068") {
    std::map<std::string, std::set<std::string>> groups;
    std::map<std::string, size_t> occurrences;
    for (const auto &f : files) {
      const auto key = (id == "H068" || arm == "folded-names")
                           ? lower_ascii(f.base)
                           : f.base;
      groups[key].insert(id == "H061" ? f.parent : f.base);
      ++occurrences[key];
    }
    for (const auto &[key, distinct] : groups) {
      const auto count =
          arm == "count-files" ? occurrences.at(key) : distinct.size();
      if (count > 1)
        output.push_back(id == "H061" ? key
                                      : key + "\t" + std::to_string(count));
    }
  } else if (id == "H062") {
    for (const auto &f : files)
      if (f.data.empty() && (f.depth != 0 || arm == "include-root"))
        output.push_back(f.path);
    return array({json_array(output)});
  } else if (id == "H064") {
    std::map<std::string, Strings> matching;
    for (const auto &f : files)
      matching[arm == "match-size" ? std::to_string(f.data.size()) : f.data]
          .push_back(f.path);
    for (const auto &[key, paths] : matching)
      if (paths.size() > 1)
        output.push_back(paths.front() + "\t" + std::to_string(paths.size()));
    std::sort(output.begin(), output.end());
  } else if (id == "H067") {
    Strings inside_hidden;
    for (const auto &f : files) {
      if (f.hidden_name)
        output.push_back(f.path);
      if (arm == "reuse-hidden-name" ? f.hidden_name : f.hidden_parent)
        inside_hidden.push_back(f.path);
    }
    return array({lines(output), lines(inside_hidden)});
  } else {
    check(id == "H069", "unknown directory oracle");
    size_t deepest = 0;
    for (const auto &f : files)
      deepest = std::max(deepest, f.depth);
    for (const auto &f : files)
      if (f.depth == deepest) {
        output.push_back(f.path);
        if (arm == "first-deepest")
          break;
      }
  }
  return array({lines(output)});
}

void build_witness(const Path &root) {
  const std::string binary{"\0\xFF\r\n", 4};
  const std::string almost{"\0\xFF\n\r", 4};
  auto large = std::string(16'401, '\xF3');
  large[8192] = '\0';
  const std::map<std::string, std::string> data{
      {".empty-root", ""},
      {".cache/.secret", ""},
      {".cache/sub/plain", "alpha"},
      {"one/.plain", ""},
      {"one/shared.txt", binary},
      {"two/shared.txt", binary},
      {"three/shared.txt", almost},
      {"two/Shared.txt", "other"},
      {"three/SHARED.TXT", "THREE"},
      {"one/Mix.ini", "indigo"},
      {"two/MIX.INI", "amber+"},
      {"one/report.csv", "csv"},
      {"two/report.txt", "txt"},
      {"deep/a/b/c/tie-z.bin", large},
      {"Deep/a/b/c/tie-a.bin", large},
      {"nest/.staging/c/d/tie-hidden.bin",
       "visible basename with hidden ancestor"},
      {"nest/doc/deeper/.only", ""},
      {"only-root.txt", ""},
      {"one/repeated.bin", "x"},
      {"two/repeated.bin", "y"}};
  for (const auto &[name, contents] : data)
    save(root / name, contents);
  fs::create_directories(root / "empty/deeper/than/anything/else/still");
}

J change(J spec, const std::string &id) {
  const auto &roles = spec["roles"];
  auto &steps = spec["roles"][0]["requirement"]["operations"];
  if (id == "H061" || id == "H064") {
    check(steps[0]["outcome"] == "tree_file_records",
          "changed directory projection");
    steps[0]["parameters"]["fields"] =
        id == "H061" ? "folded_base,parent" : "path,size";
  } else if (id == "H062") {
    J removed;
    check(steps.removeIndex(1, &removed) &&
              removed["parameters"] == object({{"column", 1},
                                               {"comparison", "gt"},
                                               {"type", "int"},
                                               {"value", "0"}}),
          "changed minimum depth filter");
  } else if (id == "H067") {
    check(
        roles.size() == 2 &&
            roles[1]["requirement"]["operations"][1]["parameters"]["column"] ==
                2,
        "changed hidden ancestor filter");
    spec["roles"][1]["requirement"]["operations"][1]["parameters"]["column"] =
        1;
  } else if (id == "H068") {
    check(steps[2]["outcome"] == "aggregate_groups" &&
              steps[2]["parameters"]["measure"] == "distinct",
          "changed spelling count");
    steps[2]["parameters"]["measure"] = "count";
    steps[2]["parameters"]["value_column"] = 0;
  } else {
    check(id == "H069", "unknown directory control");
    steps.append(object(
        {{"outcome", "first_items"}, {"parameters", object({{"count", 1}})}}));
  }
  return spec;
}

J reader_provenance(const Path &bundle, const J &binding) {
  const auto stage = runs / "v5-tree-records-XXSIq5vi/stage";
  const auto pin = hash(stage / "manifest.json");
  check(pin ==
            "f2dfabae343d2ac936d715d47919fb2d76810bace7657769d39230ada75aa2e5",
        "changed prior directory evidence");
  const auto manifest = load(stage / "manifest.json");
  for (const auto *member : {"source.json", "domain.json", "contract.json"})
    check(hash(stage / member) == manifest["files"][member].asString(),
          "changed prior directory evidence member");
  const auto source = load(stage / "source.json");
  const auto domain = load(stage / "domain.json");
  check(source["binding"] == binding &&
            domain["header_sha256"] ==
                hash(bundle / "headers" / binding["header"].asString()) &&
            domain["contract_sha256"] == hash(stage / "contract.json") &&
            domain["all_of"] == array({object({{"id", "exact_text"},
                                               {"value", "parent,ext"}})}),
        "changed prior directory qualification scope");
  for (const auto &file : source["runtime_files"])
    check(hash(bundle / "runtime/duckdb/lib" / file["path"].asString()) ==
              file["sha256"].asString(),
          "changed retained directory runtime");
  return object({{"manifest_sha256", pin},
                 {"binding", binding},
                 {"prior_exact_selector", "parent,ext"},
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
  check(ranking["open_families"][0]["name"] == "directories" &&
            ranking["open_families"][0]["requests"] == string_array(requests),
        "changed open directory cohort");
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
      exact_row(bundle, "tree_file_records", "in:" + directory, "rows");
  const auto lineage = reader_provenance(bundle, reader);

  const auto work = fresh_run("v5-residual-directory-replay");
  std::cout << work.string() << std::endl;
  auto report = object({{"active_path", active.string()},
                        {"active_sha256", active_pin},
                        {"ranking_sha256", rank_pin},
                        {"recipes_sha256", recipe_pin},
                        {"parent_manifest_sha256", parent_pin},
                        {"reader_provenance", lineage},
                        {"tools", tools()}});
  report["tools"]["unllm_logic_worker"] =
      hash(main_repo / "build/native/unllm_logic_worker");

  const auto witness = work / "trees/main";
  build_witness(witness);
  const auto boundary = work / "trees/root-only";
  save(boundary / ".a", "");
  save(boundary / "b", "root");
  fs::create_directories(boundary / "empty/sub/folder");
  const std::map<std::string, Path> fresh{{"main", witness},
                                          {"root-only", boundary}};
  const auto main_tree = scan(witness);
  check(
      main_tree.files.size() == 20 &&
          oracle("H061", main_tree) == array({"repeated.bin\nshared.txt\n"}) &&
          oracle("H062", main_tree) ==
              array(
                  {R"([".cache/.secret","nest/doc/deeper/.only","one/.plain"])"}) &&
          oracle("H064", main_tree) ==
              array({".cache/.secret\t5\nDeep/a/b/c/tie-a.bin\t2\none/"
                     "shared.txt\t2\n"}) &&
          oracle("H067", main_tree) ==
              array({".cache/.secret\n.empty-root\nnest/doc/deeper/.only\none/"
                     ".plain\n",
                     ".cache/.secret\n.cache/sub/plain\nnest/.staging/c/d/"
                     "tie-hidden.bin\n"}) &&
          oracle("H068", main_tree) == array({"mix.ini\t2\nshared.txt\t3\n"}) &&
          oracle("H069", main_tree) ==
              array({"Deep/a/b/c/tie-a.bin\ndeep/a/b/c/tie-z.bin\nnest/"
                     ".staging/c/d/tie-hidden.bin\n"}),
      "lost a frozen distinction in the directory witness");
  for (const auto &[name, path] : fresh)
    report["trees"][name] = scan(path).entries;

  const auto archive = runs / "english-audit-bcsgf8pi/replay-staged";
  const auto historical = runs / "shared-review-5ui86km8/transfer";
  std::vector<Path> programs;
  for (const auto &id : requests) {
    const auto &record = ranking["requests"][id];
    const auto dir = archive / id;
    const auto saved_pin = hash(dir / response);
    auto supplied = load(dir / response);
    auto &roles = supplied["roles"];
    check(supplied["input_type"] == directory &&
              roles.size() == recipes[id].size() &&
              saved_pin == record["reply_sha256"].asString() &&
              hash(dir / "request.txt") ==
                  record["request_sha256"].asString() &&
              record["previously_verified"] == false,
          "changed scoped directory case " + id);
    auto &entry = report["directories"][id];
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
                steps[0]["outcome"] == "tree_file_records",
            "changed ranked directory role " + id + "/" + name);
      role["requirement"]["operations"] = steps;
      role["requirement"]["uncovered"] = array();
      names.push_back(
          "result-" + std::to_string(slot + 1) +
          (role["output_type"] == "application/json" ? ".json" : ".txt"));
      entry["projections"].append(steps[0]["parameters"]["fields"]);
    }
    const auto fixtures = historical / id / "fixtures";
    std::map<std::string, Tree> archive_trees;
    for (const auto &split : splits)
      archive_trees.emplace(split, scan(fixtures / split / "input/project"));
    const auto altered = variants.at(id);
    check(oracle(id, main_tree) != oracle(id, main_tree, altered),
          "fresh tree did not distinguish " + altered);
    for (const auto &arm : {std::string("reference"), altered}) {
      const auto file = work / "specs" / arm / (id + ".json");
      write(file, arm == "reference" ? supplied : change(supplied, id));
      const auto result = trial(work, arm, id, bundle, file);
      const auto trial_dir = work / arm / id;
      const auto exe = trial_dir / "run/project/converter";
      entry["runs"][arm] = result;
      write(work / "progress.json", report);
      check(fs::is_regular_file(exe) && result["roles"].size() == roles.size(),
            "incomplete directory execution");
      programs.push_back(exe);
      for (unsigned slot = 0; slot < roles.size(); ++slot) {
        const auto &role = result["roles"][std::to_string(slot)];
        const auto &steps = role["steps"];
        const auto sink = roles[slot]["output_type"] == "application/json"
                              ? "write_texts"
                              : "write_lines";
        check(complete(role) && selects(role, "tree_file_records") &&
                  steps[steps.size() - 1]["provider"] == sink,
              "changed emitted directory provider chain for " + id);
        if (arm == "reference")
          check(steps == record["roles"][roles[slot]["id"].asString()]
                               ["supplied"]["routes"][0]["steps"],
                "emitted directory chain differs from ranked plan " + id);
      }

      for (const auto &split : splits) {
        const auto fixture = fixtures / split;
        const auto split_path =
            trial_dir /
            (split == "transfer" || split == "sparse" ? "held"
                                                      : "run/check-initial") /
            split;
        const auto &tree = archive_trees.at(split);
        check(scan(split_path / "input/project").entries == tree.entries,
              "regenerated directory tree differs for " + id + " " + split);
        auto saved = array();
        for (const auto &name : names)
          saved.append(bytes(fixture / "output" / name));
        const auto intended = oracle(id, tree);
        const auto predicted = oracle(id, tree, arm);
        check(same_report(id, saved, intended) &&
                  same_report(id, result["outputs"][split], predicted) &&
                  result["scores"][split] == (predicted == intended),
              "saved directory output differs from oracle for " + id + " " +
                  arm + " " + split);
        if (arm == "reference") {
          const auto exact =
              path_outputs(trial_dir / "exact" / split,
                           fixture / "input/project", exe, bundle, names);
          check(result["outputs"][split] == saved && exact == saved,
                "byte-exact archived directory output differs");
          entry["exact"][split] = object(
              {{"tree_sha256", sha(dump(tree.entries))}, {"outputs", exact}});
        }
      }
      for (const auto &[name, path] : fresh) {
        const auto expected = oracle(id, scan(path), arm);
        const auto actual = path_outputs(work / "witness" / id / arm / name,
                                         path, exe, bundle, names);
        check(same_report(id, actual, expected),
              "directory witness differs for " + id + " " + arm + " " + name +
                  ": " + dump(actual));
        entry["witness"][arm][name] = actual;
      }
      write(work / "progress.json", report);
    }
  }
  for (const auto &[name, path] : fresh)
    check(scan(path).entries == report["trees"][name],
          "fresh directory was modified");
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
