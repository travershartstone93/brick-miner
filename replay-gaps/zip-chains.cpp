#include "row-trial.hpp"
#include <archive.h>
#include <archive_entry.h>
#include <iostream>
#include <memory>

using namespace miner;
using namespace row_replay;
namespace {
struct Request {
  std::string id, fields, names, expected;
};
const std::vector<Request> requests{
    {"H054", "top,size", "archive_members_zip",
     ".\t19\ndocs\t17\nDocs\t9\nother\t9\nonly-empty\t0\n"},
    {"H059", "ext,size", "archive_members_zip",
     "txt\t20\ncsv\t16\nbin\t1\nzip\t1\n"},
    {"H060", "parent,path,size", "archive_files_zip",
     ".\tdotted.\t1\nDocs\tDocs/odd.ZIP\t1\ndocs\tdocs/tiny.bin\t1\n"
     "docs/sub\tdocs/sub/a.TXT\t2\nother\tother/dotted.\t1\n"},
};
struct Member {
  std::string path;
  unsigned size = 0;
  bool directory() const { return path.ends_with('/'); }
};
const std::vector<Member> members{
    {"root.TXT", 4},
    {"bRoot.csv", 2},
    {"aRoot.CsV", 2},
    {".profile", 7},
    {"none", 3},
    {"RootZero.txt"},
    {"dotted.", 1},
    {"docs/"},
    {"docs/z.CSV", 6},
    {"docs/A.csv", 6},
    {"docs/tiny.bin", 1},
    {"docs/empty.txt"},
    {"docs/sub/"},
    {"docs/sub/z.txt", 2},
    {"docs/sub/a.TXT", 2},
    {"docs/sub/empty.txt"},
    {"Docs/"},
    {"Docs/Root.TXT", 4},
    {"Docs/.env", 4},
    {"Docs/odd.ZIP", 1},
    {"other/"},
    {"other/.hidden.txt", 4},
    {"other/twin.TXT", 4},
    {"other/dotted.", 1},
    {"only-empty/"},
    {"only-empty/zero.CSV"},
    {"only-dir/"},
};
const Strings splits{"ordinary", "boundary", "empty", "transfer", "sparse"};
const std::string zip = "application/zip";
const std::string tsv = "text/tab-separated-values";
const Path response = "run/calls/interpret/response.json";

J write_fixture(const Path &file) {
  std::unique_ptr<archive, decltype(&archive_write_free)> writer(
      archive_write_new(), archive_write_free);
  auto *arc = writer.get();
  check(arc && archive_write_set_format_zip(arc) == ARCHIVE_OK &&
            archive_write_set_options(arc, "zip:compression=deflate") ==
                ARCHIVE_OK &&
            archive_write_open_filename(arc, file.c_str()) == ARCHIVE_OK,
        "cannot open ZIP witness");
  auto manifest = array();
  Strings names;
  for (const auto &m : members) {
    std::unique_ptr<archive_entry, decltype(&archive_entry_free)> entry(
        archive_entry_new(), archive_entry_free);
    check(bool(entry), "cannot allocate ZIP witness entry");
    archive_entry_set_pathname(entry.get(), m.path.c_str());
    archive_entry_set_filetype(entry.get(),
                               m.directory() ? AE_IFDIR : AE_IFREG);
    archive_entry_set_size(entry.get(), m.size);
    archive_entry_set_perm(entry.get(), m.directory() ? 0755 : 0644);
    archive_entry_set_mtime(entry.get(), 1770000000, 0);
    check(archive_write_header(arc, entry.get()) == ARCHIVE_OK,
          "ZIP header write failed");
    if (!m.directory()) {
      const std::string payload(m.size, 'x');
      check(archive_write_data(arc, payload.data(), payload.size()) ==
                static_cast<la_ssize_t>(payload.size()),
            "ZIP payload write failed");
    }
    check(archive_write_finish_entry(arc) == ARCHIVE_OK,
          "ZIP entry write failed");
    names.push_back(m.path);
    manifest.append(object(
        {{"path", m.path}, {"size", m.size}, {"directory", m.directory()}}));
  }
  check(archive_write_close(arc) == ARCHIVE_OK,
        "ZIP witness finalization failed");
  check(run({"zipinfo", "-1", file}) == join(names, "\n") + "\n",
        "independent ZIP name order differs");
  run({"unzip", "-tqq", file});
  for (const auto &m : members)
    if (!m.directory())
      check(run({"unzip", "-p", file, m.path}) == std::string(m.size, 'x'),
            "independent unpacked bytes differ for " + m.path);
  return object({{"archive_sha256", hash(file)}, {"members", manifest}});
}

J scalar_reader(const Path &work, const Path &base, const std::string &outcome,
                const std::string &binding, const std::string &sink,
                const std::string &fixture, const J &expected,
                std::vector<Path> &programs) {
  const auto dir = work / "scalar" / outcome;
  const auto spec = spec_for(zip, "application/json",
                             object({{"outcome", outcome},
                                     {"parameters", object()},
                                     {"uncovered", array()}}));
  const auto p = plan(base, spec, dir);
  const auto &role = p["providers"][0];
  const auto &steps = role["plan"]["steps"];
  check(p["providers"].size() == 1 && p["interpretation_errors"].empty() &&
            p["gaps"].empty() && complete(role) && steps.size() == 2 &&
            steps[0]["provider"] == binding && steps[1]["provider"] == sink,
        "scalar ZIP reader did not plan");
  const auto exe = dir / "converter";
  build_emission(role, dir, exe, "g++-15");
  programs.push_back(exe);
  const auto result = extra_case(dir / "witness", exe, base, fixture,
                                 array({dump(expected, false)}));
  const auto observed = parse(result["actual"][0].asString());
  check(observed == expected,
        "scalar ZIP reader disagreed with authored members");
  return object({{"plan_sha256", hash(dir / "components/emission.json")},
                 {"program_sha256", hash(exe)},
                 {"observed", observed}});
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

  const auto binding = exact_row(base, "zip_file_records", "in:" + zip, "rows");
  const auto evidence = runs / "v5-zip-records-XXkiGzWl/stage";
  const auto evidence_pin = hash(evidence / "manifest.json");
  check(evidence_pin ==
            "dc343f7f0c112b59f310b73d27900a19aec69f5a5e84d4a7673f36b5c3c85473",
        "changed prior ZIP qualification");
  const auto manifest = load(evidence / "manifest.json");
  for (auto member : {"source.json", "domain.json", "contract.json"})
    check(hash(evidence / member) == manifest["files"][member].asString(),
          "changed prior ZIP evidence member");
  const auto source = load(evidence / "source.json");
  const auto domain = load(evidence / "domain.json");
  check(source["binding"] == binding &&
            domain["header_sha256"] ==
                hash(base / "headers" / binding["header"].asString()) &&
            domain["contract_sha256"] == hash(evidence / "contract.json") &&
            domain["all_of"] == array({object({{"id", "exact_text"},
                                               {"value", "ext,parent"}})}),
        "prior ZIP lineage does not match the binding");
  const auto runtime = base / "runtime/duckdb/lib";
  for (const auto &file : source["runtime_files"])
    check(hash(runtime / file["path"].asString()) == file["sha256"].asString(),
          "retained ZIP runtime differs from the qualified bytes");

  const auto ranking = runs / "ranked-gaps-5x2_hyp7/ranking.json";
  const auto ranking_pin = hash(ranking);
  check(ranking_pin ==
            "3d902a86f77c7159581a1721cf07c96ccd7b0147259ffdee6edce8f976119495",
        "changed archived ranking");
  const auto archive = runs / "english-audit-bcsgf8pi/replay-staged";
  const auto disconnected =
      load(ranking)["stops"]["no complete chain for the requested operations"];
  Strings zip_requests, expected_requests;
  for (const auto &request : disconnected)
    if (load(archive / request.asString() / response)["input_type"] == zip)
      zip_requests.push_back(request.asString());
  for (const auto &request : requests)
    expected_requests.push_back(request.id);
  check(zip_requests == expected_requests, "changed ZIP replay cohort");
  const auto original =
      volume / "unllm/experiments/boilerplate-B42-VrtYbHSf/fresh-staged";
  const auto recipes_file = root / "shared/request-recipes.json";
  const auto recipe_pin = hash(recipes_file);
  check(recipe_pin ==
            "768676cbc34ef7db9d869520bd3d26d86123f49a3c9803901eb5653de3a6f49b",
        "changed supplied recipes");
  const auto recipes = load(recipes_file);
  const auto active = fs::canonical(volume / "unllm/boilerplate/active");
  const auto active_pin = hash(active / "SHA256SUMS");
  check(active_pin ==
            "e4564e3079f0b32b3379a411b170ce9af61ec80ab7938b6a974e724abd538328",
        "changed published context");

  const auto work = fresh_run("v5-zip-chain-replay");
  std::cout << work.string() << std::endl;
  auto report = object({{"active_path", active.string()},
                        {"active_sha256", active_pin},
                        {"parent_manifest_sha256", parent_pin},
                        {"writer_manifest_sha256", writer_pin},
                        {"ranking_sha256", ranking_pin},
                        {"recipes_sha256", recipe_pin},
                        {"prior_projection_manifest_sha256", evidence_pin},
                        {"prior_projection_domain", domain["all_of"]},
                        {"reader", binding},
                        {"reader_runtime", source["runtime_files"]},
                        {"tools", tools()}});
  const auto witness = work / "known-members.zip";
  report["fixture"] = write_fixture(witness);
  const auto witness_bytes = bytes(witness);
  auto all_paths = array(), file_paths = array(), file_sizes = array();
  for (const auto &m : members) {
    all_paths.append(m.path);
    if (!m.directory()) {
      file_paths.append(m.path);
      file_sizes.append(Json::Int64(m.size));
    }
  }
  std::vector<Path> programs;
  report["scalar"]["archive_members"] =
      scalar_reader(work, base, "archive_members", "archive_members_zip",
                    "write_texts", witness_bytes, all_paths, programs);
  report["scalar"]["archive_files"] =
      scalar_reader(work, base, "archive_files", "archive_files_zip",
                    "write_texts", witness_bytes, file_paths, programs);
  report["scalar"]["archive_sizes"] =
      scalar_reader(work, base, "archive_sizes", "archive_sizes_zip",
                    "write_numbers", witness_bytes, file_sizes, programs);

  for (const auto &c : requests) {
    const auto &id = c.id;
    const auto saved_path = archive / id / response;
    const auto saved_pin = hash(saved_path);
    check(saved_pin == hash(original / id / response),
          "original reply changed for " + id);
    const auto saved = load(saved_path);
    const auto role = saved["roles"][0];
    const auto mime = role["output_type"].asString();
    const auto ops = role["requirement"]["operations"];
    check(saved["input_type"] == zip && saved["roles"].size() == 1 &&
              recipes[id].size() == 1 && ops.size() >= 2 &&
              ops[1]["outcome"] == "archive_sizes" &&
              ops[0]["outcome"].asString() ==
                  c.names.substr(0, c.names.size() - 4),
          "changed saved ZIP shape for " + id);
    auto &item = report["requests"][id];
    item["archived_sha256"] = saved_pin;
    const auto archived_plan = archive / id / "run/components/emission.json";
    item["archived_plan_sha256"] = hash(archived_plan);
    const auto dir = work / "parameters" / id;
    const auto prior = plan_probe(base, dir / "archived", saved);
    const auto gap = prior["gaps"]["r"].asString();
    check(
        gap.starts_with("No provider chain for requested outcomes: ") &&
            gap ==
                load(archived_plan)["gaps"][role["id"].asString()].asString() &&
            prior["validation"].empty() && prior["routes"].empty(),
        "archived chain stop changed for " + id);
    item["plans"]["archived"] = prior;
    item["first_incompatible_pair"] =
        array({exact_row(base, c.names, "in:" + zip, "strings"),
               exact_row(base, "archive_sizes_zip", "in:" + zip, "integers")});

    auto fixed = saved;
    auto &fixed_req = fixed["roles"][0]["requirement"];
    fixed_req["operations"] = operations(recipes[id][0]);
    fixed_req["uncovered"] = array();
    const auto read = fixed_req["operations"][0];
    check(read["outcome"] == "zip_file_records" &&
              read["parameters"] == object({{"fields", c.fields}}),
          "changed supplied ZIP projection");
    auto reader_only = saved;
    auto &rest = reader_only["roles"][0]["requirement"]["operations"];
    rest = array({read});
    for (unsigned j = 2; j < ops.size(); ++j)
      rest.append(ops[j]);
    const auto changed = plan_probe(base, dir / "reader-only", reader_only);
    check(changed["validation"].empty(), "correlated reader failed validation");
    for (const auto &route : changed["routes"])
      check(!complete(route) &&
                route["uncovered"] == role["requirement"]["uncovered"],
            "reader-only control removed an uncovered obligation");
    item["plans"]["reader-only"] = changed;

    const auto history =
        runs / "shared-review-5ui86km8/transfer" / id / "fixtures";
    auto replay = [&](const std::string &arm, const Path &bundle) {
      const auto spec = work / "specs" / id / (arm + ".json");
      write(spec, fixed);
      const auto result = trial(work, arm, id, bundle, spec);
      if (!result["program_sha256"].isNull())
        programs.push_back(work / arm / id / "run/project/converter");
      item["runs"][arm] = result;
      write(work / "progress.json", report);
      return result;
    };
    const auto &bundle = mime == tsv ? with_tsv : base;
    const auto passed = replay("fixed", bundle);
    check(passed["verified"] == true && complete(passed["roles"]["0"]) &&
              selects(passed["roles"]["0"], "zip_file_records") &&
              selects(passed["roles"]["0"],
                      mime == tsv ? "write_tsv_lines" : "write_lines"),
          "fixed ZIP recipe did not bind and pass");
    const auto fixed_dir = work / "fixed" / id;
    const auto exe = fixed_dir / "run/project/converter";
    for (const auto &split : splits) {
      const auto previous = history / split;
      const auto expected = bytes(previous / "output/result-1.txt");
      check(passed["outputs"][split][0] == expected,
            "retained ZIP output changed");
      const auto exact =
          extra_case(fixed_dir / "exact-archive" / split, exe, bundle,
                     bytes(previous / "input/backup.zip"), array({expected}));
      check(exact["passed"] == true, "exact archived ZIP execution differed");
      item["exact_inputs"][split] = exact;
    }
    const auto fresh = extra_case(fixed_dir / "witness", exe, bundle,
                                  witness_bytes, array({c.expected}));
    check(fresh["passed"] == true,
          "fresh correlated ZIP result differed for " + id);
    item["witness"] = fresh;
    if (mime == tsv) {
      const auto blocked = replay("without-writer", base);
      check(blocked["verified"] == false && blocked["roles"].size() == 1 &&
                ports(blocked["roles"]["0"]) ==
                    array({array({"strings", "out:" + tsv})}),
            "unexpected TSV comparison gap");
    }
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
