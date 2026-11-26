#include "row-trial.hpp"
#include <algorithm>
#include <iostream>

using namespace miner;
using namespace row_replay;
namespace {
const std::string id = "H063";
const std::string dir_type = "inode/directory";
const std::string tsv = "text/tab-separated-values";
const Strings splits{"ordinary", "boundary", "empty", "transfer", "sparse"};
const Path response = "run/calls/interpret/response.json";
const std::vector<std::pair<std::string, std::string>> files{
    {".env", ""},
    {"root.txt", "root\n"},
    {".git/HEAD", "head\n"},
    {".git/objects/key", "data"},
    {"A/.stamp", ""},
    {"Mixed/top.txt", "a"},
    {"Mixed/sub/bottom.txt", "b"},
    {"a/nested/a.txt", "lower"},
    {"deep/a/b/same.txt", "first"},
    {"deep/c/.empty", ""},
    {"deep/c/same.txt", "second"},
    {"zero/empty", ""},
};
const Strings empty_dirs{"silent/deep/nested", "unused"};

std::string oracle(const Path &tree) {
  std::map<std::string, unsigned> counts;
  for (const auto &child : fs::directory_iterator(tree)) {
    if (!fs::is_directory(child.symlink_status()))
      continue;
    unsigned n = 0;
    for (const auto &entry : fs::recursive_directory_iterator(child.path()))
      n += fs::is_regular_file(entry.symlink_status());
    if (n)
      counts[child.path().filename().string()] = n;
  }
  std::string output;
  for (const auto &[name, n] : counts)
    output += name + "\t" + std::to_string(n) + "\n";
  return output;
}

J scalar_view(const Path &dest, const Path &tree, const Path &bundle,
              const std::string &effect, const Strings &expected,
              std::vector<Path> &programs) {
  const auto spec = spec_for(dir_type, "application/json",
                             object({{"outcome", effect},
                                     {"parameters", object()},
                                     {"uncovered", array()}}));
  const auto p = plan(bundle, spec, dest);
  const auto &role = p["providers"][0];
  const auto &steps = role["plan"]["steps"];
  check(p["providers"].size() == 1 && complete(role) && p["gaps"].empty() &&
            p["interpretation_errors"].empty() && steps.size() == 2 &&
            steps[0]["provider"] == effect &&
            steps[1]["provider"] == "write_texts",
        "scalar directory view did not plan");
  const auto exe = dest / "converter";
  build_emission(role, dest, exe, "g++-15");
  programs.push_back(exe);
  const auto actual = parse(path_output(dest / "witness", tree, exe, bundle));
  check(actual == string_array(expected), "scalar directory view differed");
  return object({{"plan_sha256", hash(dest / "components/emission.json")},
                 {"program_sha256", hash(exe)},
                 {"observed", actual}});
}
} // namespace

int main() try {
  const auto parent = runs / "v5-queue-XXH5XMi9/stage/package";
  const auto parent_pin = hash(parent / "manifest.json");
  check(parent_pin ==
            "d15aee96917ff440f58f76270e3ca48575268dcfcf2bce951805c865aae3af63",
        "changed V5 parent");
  const auto base = parent / "bundle";
  verify_bundle(
      base, "436db92629463daee559de2cbbc1c81519e12306d5bf01cc6d2883f3140e779c");
  const auto writer_stage = runs / "v5-tsv-writer-XXAf0x0y/stage";
  const auto writer_pin = hash(writer_stage / "manifest.json");
  check(writer_pin ==
            "0047b47009df08f7ee228729eb46b4438ec9d59eadd7c44647ec6761eeabb994",
        "changed TSV evidence");
  const auto with_tsv = writer_stage / "bundle";
  verify_bundle(
      with_tsv,
      "b2f8a40d44b1589779fc82fcfa23995df5cb2b2432167e9383f6f22e9c828d54");
  const auto writer =
      exact_row(with_tsv, "write_tsv_lines", "strings", "out:" + tsv);
  check(remove_binding(read_tree(with_tsv), writer).at("SHA256SUMS") ==
            bytes(base / "SHA256SUMS"),
        "TSV control differs outside the writer row");

  const auto binding =
      exact_row(base, "tree_file_records", "in:" + dir_type, "rows");
  const auto evidence = runs / "v5-tree-records-XXSIq5vi/stage";
  const auto evidence_pin = hash(evidence / "manifest.json");
  check(evidence_pin ==
            "f2dfabae343d2ac936d715d47919fb2d76810bace7657769d39230ada75aa2e5",
        "changed prior directory qualification");
  const auto manifest = load(evidence / "manifest.json");
  for (auto member : {"source.json", "domain.json", "contract.json"})
    check(hash(evidence / member) == manifest["files"][member].asString(),
          "changed qualified directory evidence member");
  const auto source = load(evidence / "source.json");
  const auto domain = load(evidence / "domain.json");
  check(source["binding"] == binding &&
            domain["header_sha256"] ==
                hash(base / "headers" / binding["header"].asString()) &&
            domain["contract_sha256"] == hash(evidence / "contract.json") &&
            domain["all_of"] == array({object({{"id", "exact_text"},
                                               {"value", "parent,ext"}})}),
        "prior directory lineage differs from retained binding");
  for (const auto &file : source["runtime_files"])
    check(hash(base / "runtime/duckdb/lib" / file["path"].asString()) ==
              file["sha256"].asString(),
          "retained directory runtime differs from qualified bytes");

  const auto active = fs::canonical(volume / "unllm/boilerplate/active");
  const auto active_pin = hash(active / "SHA256SUMS");
  check(active_pin ==
            "e4564e3079f0b32b3379a411b170ce9af61ec80ab7938b6a974e724abd538328",
        "changed published context");
  const auto ranking = runs / "ranked-gaps-5x2_hyp7/ranking.json";
  const auto ranking_pin = hash(ranking);
  check(ranking_pin ==
            "3d902a86f77c7159581a1721cf07c96ccd7b0147259ffdee6edce8f976119495",
        "changed archived ranking");
  const auto archive = runs / "english-audit-bcsgf8pi/replay-staged";
  const auto disconnected =
      load(ranking)["stops"]["no complete chain for the requested operations"];
  Strings directory_requests;
  for (const auto &r : disconnected)
    if (load(archive / r.asString() / response)["input_type"] == dir_type)
      directory_requests.push_back(r.asString());
  check(directory_requests == Strings{id},
        "changed disconnected directory cohort");
  const auto original =
      volume / "unllm/experiments/boilerplate-B42-VrtYbHSf/fresh-staged";
  const auto saved_pin = hash(archive / id / response);
  check(saved_pin == hash(original / id / response), "changed original answer");
  const auto saved = load(archive / id / response);
  const auto role = saved["roles"][0];
  check(saved["roles"].size() == 1 && role["output_type"] == tsv &&
            role["requirement"]["operations"] ==
                operations(array({array({"directory_names", object()}),
                                  array({"recursive_files", object()})})),
        "changed archived directory operations");

  const auto recipes = root / "shared/request-recipes.json";
  const auto recipe_pin = hash(recipes);
  check(recipe_pin ==
            "768676cbc34ef7db9d869520bd3d26d86123f49a3c9803901eb5653de3a6f49b",
        "changed supplied recipe");
  auto fixed = saved;
  auto &req = fixed["roles"][0]["requirement"];
  req["operations"] = operations(load(recipes)[id][0]);
  req["uncovered"] = array();
  const auto read = req["operations"][0];
  check(read["outcome"] == "tree_file_records" &&
            read["parameters"] == object({{"fields", "top"}}),
        "changed H063 top projection");

  const auto work = fresh_run("v5-directory-count-replay");
  std::cout << work.string() << std::endl;
  auto report = object({{"request", id},
                        {"active_path", active.string()},
                        {"active_sha256", active_pin},
                        {"parent_manifest_sha256", parent_pin},
                        {"writer_manifest_sha256", writer_pin},
                        {"ranking_sha256", ranking_pin},
                        {"recipes_sha256", recipe_pin},
                        {"archived_sha256", saved_pin},
                        {"prior_projection_manifest_sha256", evidence_pin},
                        {"prior_projection_domain", domain["all_of"]},
                        {"reader", binding},
                        {"reader_runtime", source["runtime_files"]},
                        {"tools", tools()}});
  const auto archived_plan =
      plan_probe(base, work / "parameters/archived", saved);
  const auto previous = load(archive / id / "run/components/emission.json");
  const auto gap = archived_plan["gaps"]["r"].asString();
  check(gap.starts_with("No provider chain for requested outcomes: ") &&
            gap == previous["gaps"][role["id"].asString()].asString() &&
            archived_plan["validation"].empty() &&
            archived_plan["routes"].empty(),
        "changed archived directory chain stop");
  report["plans"]["archived"] = archived_plan;
  report["first_incompatible_pair"] =
      array({exact_row(base, "directory_names", "in:" + dir_type, "strings"),
             exact_row(base, "recursive_files", "in:" + dir_type, "strings")});
  auto reader_only = saved;
  reader_only["roles"][0]["requirement"]["operations"] = array({read});
  const auto partial =
      plan_probe(base, work / "parameters/reader-only", reader_only);
  check(partial["validation"].empty() && partial["routes"].size() == 1 &&
            partial["routes"][0]["ports"] ==
                array({array({"rows", "out:" + tsv})}) &&
            partial["routes"][0]["uncovered"] ==
                role["requirement"]["uncovered"],
        "reader-only intervention changed other requirements");
  report["plans"]["reader-only"] = partial;

  const auto tree = work / "witness-tree";
  auto manifest_rows = array();
  Strings projected_files;
  for (const auto &[path, payload] : files) {
    save(tree / path, payload);
    manifest_rows.append(object({{"path", path},
                                 {"size", Json::UInt64(payload.size())},
                                 {"sha256", hash(tree / path)}}));
    projected_files.push_back(path);
  }
  for (const auto &path : empty_dirs)
    fs::create_directories(tree / path);
  report["fixture"] = object({{"regular_files", manifest_rows},
                              {"empty_directories", string_array(empty_dirs)}});
  std::sort(projected_files.begin(), projected_files.end());
  std::vector<Path> programs;
  report["scalar"]["directory_names"] = scalar_view(
      work / "scalar/directory_names", tree, base, "directory_names",
      {".git", "A", "Mixed", "a", "deep", "silent", "unused", "zero"},
      programs);
  report["scalar"]["recursive_files"] =
      scalar_view(work / "scalar/recursive_files", tree, base,
                  "recursive_files", projected_files, programs);

  const auto historical =
      runs / "shared-review-5ui86km8/transfer" / id / "fixtures";
  auto replay = [&](const std::string &arm, const Path &bundle, const J &spec) {
    const auto path = work / "specs" / (arm + ".json");
    write(path, spec);
    const auto result = trial(work, arm, id, bundle, path);
    if (!result["program_sha256"].isNull())
      programs.push_back(work / arm / id / "run/project/converter");
    report["runs"][arm] = result;
    write(work / "progress.json", report);
    return result;
  };
  const auto blocked = replay("without-writer", base, fixed);
  check(blocked["roles"].size() == 1 && blocked["verified"] == false &&
            ports(blocked["roles"]["0"]) ==
                array({array({"strings", "out:" + tsv})}),
        "H063 did not isolate the TSV writer");
  const auto accepted = replay("fixed", with_tsv, fixed);
  check(accepted["roles"].size() == 1 && complete(accepted["roles"]["0"]) &&
            selects(accepted["roles"]["0"], "tree_file_records") &&
            selects(accepted["roles"]["0"], "write_tsv_lines") &&
            accepted["verified"] == true,
        "full directory construction failed");
  const auto fixed_dir = work / "fixed" / id;
  for (const auto &split : splits) {
    const auto current =
        fixed_dir /
        ((split == "sparse" || split == "transfer") ? "held"
                                                    : "run/check-initial") /
        split / "input/project";
    const auto archived_tree = historical / split / "input/project";
    run({"diff", "-r", "--brief", "--no-dereference", archived_tree, current});
    const auto expected = oracle(archived_tree);
    check(expected == bytes(historical / split / "output/result-1.txt") &&
              accepted["outputs"][split][0] == expected,
          "directory count differs from archived output or independent oracle");
    const auto exact =
        path_output(fixed_dir / "exact" / split, archived_tree,
                    fixed_dir / "run/project/converter", with_tsv);
    check(exact == expected, "exact archived tree produced different output");
    report["exact_inputs"][split] = object({{"tree_equal", true},
                                            {"output", exact},
                                            {"output_sha256", sha(exact)}});
  }

  auto no_root_filter = fixed;
  J removed;
  check(no_root_filter["roles"][0]["requirement"]["operations"].removeIndex(
            1, &removed) &&
            removed["outcome"] == "filter_records" &&
            removed["parameters"]["value"] == ".",
        "changed root filter");
  auto parent_key = fixed;
  parent_key["roles"][0]["requirement"]["operations"][0]["parameters"]
            ["fields"] = "parent";
  auto shallow = fixed;
  auto &steps = shallow["roles"][0]["requirement"]["operations"];
  steps[0]["parameters"]["fields"] = "top,depth";
  auto amended = array({steps[0], steps[1],
                        object({{"outcome", "filter_records"},
                                {"parameters", object({{"column", 1},
                                                       {"comparison", "eq"},
                                                       {"type", "int"},
                                                       {"value", "1"}})}})});
  for (unsigned i = 2; i < steps.size(); ++i)
    amended.append(steps[i]);
  steps = amended;
  for (const auto &[arm, spec] :
       {std::pair{"without-root-filter", no_root_filter},
        std::pair{"parent-key", parent_key}, std::pair{"shallow", shallow}}) {
    const auto result = replay(arm, with_tsv, spec);
    check(result["roles"].size() == 1 && complete(result["roles"]["0"]) &&
              result["verified"] == false,
          "incorrect directory control did not discriminate");
  }
  const auto correct = oracle(tree);
  check(correct == ".git\t2\nA\t1\nMixed\t2\na\t1\ndeep\t3\nzero\t1\n",
        "fresh directory oracle differs from frozen expectation");
  for (const auto &[arm, expected] :
       std::vector<std::pair<std::string, std::string>>{
           {"fixed", correct},
           {"without-root-filter", ".\t2\n" + correct},
           {"parent-key",
            ".git\t1\n.git/objects\t1\nA\t1\nMixed\t1\nMixed/sub\t1\n"
            "a/nested\t1\ndeep/a/b\t1\ndeep/c\t2\nzero\t1\n"},
           {"shallow", ".git\t1\nA\t1\nMixed\t1\nzero\t1\n"}}) {
    const auto dir = work / arm / id;
    const auto actual = path_output(dir / "witness", tree,
                                    dir / "run/project/converter", with_tsv);
    check(actual == expected, "fresh directory result differs for " + arm);
    report["witness"][arm] =
        object({{"output", actual}, {"output_sha256", sha(actual)}});
  }

  check(tools() == report["tools"] && hash(active / "SHA256SUMS") == active_pin,
        "changed replay context");
  write(work / ".report.json", report);
  run({"sync", "-f", work / ".report.json"});
  for (const auto &exe : programs)
    fs::remove(exe);
  fs::rename(work / ".report.json", work / "report.json");
  std::cout << (work / "report.json").string() << '\n';
  return 0;
} catch (const std::exception &e) {
  std::cerr << e.what() << '\n';
  return 1;
}
