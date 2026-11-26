#include "row-trial.hpp"
#include <iostream>

using namespace miner;
using namespace row_replay;
namespace {
struct Request {
  std::string id, witness, scalar_sha256;
};
const std::vector<Request> requests{
    {"H043", "Deep & Wide\n",
     "41d21860abb4ff681d8a567af1b8ae363d78250a223d61dce1ee61d2b5b65c06"},
    {"H044", R"(["draft-only","book","world","culture","rail","news"])",
     "6c3aaf53b86ca15e9028aad65387d315317d913daf6eabb49ed9eb170c910154"},
    {"H045",
     "book\tSolo\nculture\tfour\nnews\tDeep & Wide\nrail\taaaa\nworld\tDeep & "
     "Wide\n",
     ""},
    {"H046",
     "Zoe\tDeep & Wide\nZoe\tDeep & Wide\nZoe\tDeep & Wide\nMia\tSolo\n", ""},
    {"H048",
     "book\tSolo\nculture\tfour\ndraft-only\tHidden\nnews\tDeep & "
     "Wide\nrail\tzzzz\nworld\tdraft is later\n",
     ""},
    {"H049", "Deep & Wide\t2\nfour\t2\n", ""},
    {"H050", R"(["Zoe","Ava","Kai"])",
     "82d944b124469298952c0861447bda1ec7574197925b20b8a9f88473a6507063"},
};
const Strings splits{"ordinary", "boundary", "empty", "transfer", "sparse"};
const std::string xml = "application/xml";
const std::string json = "application/json";
const std::string tsv = "text/tab-separated-values";
const std::string projection = "@editor,@section,@state,title";
const Path response = "run/calls/interpret/response.json";
const std::string feed = R"(<feed>
  <article editor="Dora" section="draft-only" state="draft"><title>Hidden</title></article>
  <article editor="Zoe" section="culture" state="live"><title>four</title></article>
  <article editor="Zoe" section="news" state="draft"><title>Deep &amp; Wide</title></article>
  <article editor="Zoe" section="world" state="live"><title>Deep &#38; Wide</title></article>
  <article editor="Ava" section="culture" state="live"><title>&#xE9;&#x1F642;</title></article>
  <article editor="Ava" section="news" state="live"><title>Deep &amp; Wide</title></article>
  <article editor="Zoe" section="news" state="live"><title>Deep &amp; Wide</title></article>
  <article editor="Kai" section="rail" state="live"><title>zzzz</title></article>
  <article editor="Leo" section="rail" state="live"><title>aaaa</title></article>
  <article editor="Kai" section="rail" state="draft"><title>late</title></article>
  <article editor="Zoe" section="world" state="live"><title>Deep &amp; Wide</title></article>
  <article editor="Ava" section="culture" state="live"><title>four</title></article>
  <article editor="Dora" section="world" state="draft"><title>draft is later</title></article>
  <article editor="Mia" section="book" state="live"><title>Solo</title></article>
  <article editor="Mia" section="book" state="live"><title>Solo</title></article>
  <article editor="Nora" section="draft-only" state="draft"><title>Hidden</title></article>
</feed>)";

J replace_reads(J spec, const J &reader) {
  auto &ops = spec["roles"][0]["requirement"]["operations"];
  auto updated = array({reader});
  for (const auto &op : ops)
    if (op["outcome"] != "xml_text" && op["outcome"] != "xml_attributes")
      updated.append(op);
  ops = updated;
  return spec;
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
        "TSV control differs beyond the writer row");
  const auto reader = exact_row(base, "xml_records", "in:" + xml, "rows");
  const auto source = base / "qualifications/shared/inputs/source.json";
  const auto source_pin = hash(source);
  const auto kernel =
      hash(base / "runtime/duckdb/lib/libbrick_shared_inputs.so");
  check(source_pin == "cfbd58d26ebaab781ff61c4859b5fa80b62a8dae1c113715b498ba5c"
                      "4af9bdb1" &&
            load(source)["kernel_sha256"] == kernel,
        "changed retained XML source or kernel");

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
  Strings xml_ids, expected_ids;
  for (const auto &r : disconnected)
    if (load(archive / r.asString() / response)["input_type"] == xml)
      xml_ids.push_back(r.asString());
  for (const auto &r : requests)
    expected_ids.push_back(r.id);
  check(disconnected.size() == 13 && xml_ids == expected_ids,
        "changed XML replay cohort");

  const auto recipes = root / "shared/request-recipes.json";
  const auto recipe_pin = hash(recipes);
  check(recipe_pin ==
            "768676cbc34ef7db9d869520bd3d26d86123f49a3c9803901eb5653de3a6f49b",
        "changed supplied recipes");
  const auto original =
      volume / "unllm/experiments/boilerplate-B42-VrtYbHSf/fresh-staged";
  const auto prior_review = runs / "shared-review-5ui86km8";
  const auto recipes_by_request = load(recipes);
  const auto work = fresh_run("v5-xml-chain-replay");
  std::cout << work.string() << std::endl;
  auto report =
      object({{"active_path", active.string()},
              {"active_sha256", active_pin},
              {"parent_manifest_sha256", parent_pin},
              {"writer_manifest_sha256", writer_pin},
              {"ranking_sha256", ranking_pin},
              {"recipes_sha256", recipe_pin},
              {"tools", tools()},
              {"reader", reader},
              {"reader_source_sha256", source_pin},
              {"reader_kernel_sha256", kernel},
              {"reader_header_sha256",
               hash(base / "headers" / reader["header"].asString())}});
  std::vector<Path> programs;

  for (const auto &req : requests) {
    const auto &id = req.id;
    const auto saved_file = archive / id / response;
    const auto saved_pin = hash(saved_file);
    check(saved_pin == hash(original / id / response),
          "original reply changed for " + id);
    const auto saved = load(saved_file);
    check(saved["input_type"] == xml && saved["roles"].size() == 1 &&
              recipes_by_request[id].size() == 1,
          "unexpected XML task shape");
    const auto role = saved["roles"][0];
    const auto mime = role["output_type"].asString();
    const auto ops = role["requirement"]["operations"];
    auto &item = report["requests"][id];
    item["archived_sha256"] = saved_pin;
    const auto parameters = work / "parameters" / id;
    const auto retired_plan = archive / id / "run/components/emission.json";
    item["archived_plan_sha256"] = hash(retired_plan);
    const auto archived_plan = plan_probe(base, parameters / "archived", saved);
    const auto historical = load(retired_plan);
    const auto gap = archived_plan["gaps"]["r"].asString();
    check(gap.starts_with("No provider chain for requested outcomes: ") &&
              gap == historical["gaps"][role["id"].asString()].asString() &&
              archived_plan["validation"].empty() &&
              archived_plan["routes"].empty(),
          "archived chain stop changed for " + id);
    item["plans"]["archived"] = archived_plan;

    const unsigned predecessor = id == "H044" ? 1 : 0;
    const auto left_id = ops[predecessor]["outcome"].asString();
    const auto right_id = ops[predecessor + 1]["outcome"].asString();
    const auto left = exact_row(
        base, left_id, predecessor == 1 ? "strings" : "in:" + xml, "strings");
    const auto right =
        right_id == "named_sets"
            ? exact_row(base, right_id, "in:" + json, "prerequisites")
            : exact_row(base, right_id, "in:" + xml, "strings");
    check(left["to"] != right["from"],
          "expected incompatible saved operations for " + id);
    auto prefix = saved;
    auto &prefix_role = prefix["roles"][0];
    prefix_role["output_type"] = json;
    auto &prefix_req = prefix_role["requirement"];
    prefix_req["operations"] = array();
    for (unsigned i = 0; i <= predecessor; ++i)
      prefix_req["operations"].append(ops[i]);
    prefix_req["uncovered"] = array();
    const auto prefix_plan = plan_probe(base, parameters / "prefix", prefix);
    check(prefix_plan["validation"].empty() &&
              prefix_plan["routes"].size() == 1 &&
              complete(prefix_plan["routes"][0]) &&
              selects(prefix_plan["routes"][0], left_id),
          "saved prefix was not independently constructible for " + id);
    item["first_incompatible_pair"] = array({left, right});
    item["plans"]["prefix"] = prefix_plan;

    auto fixed = saved;
    auto &fixed_req = fixed["roles"][0]["requirement"];
    fixed_req["operations"] = operations(recipes_by_request[id][0]);
    fixed_req["uncovered"] = array();
    const auto projection_op = fixed_req["operations"][0];
    check(
        projection_op["outcome"] == "xml_records" &&
            projection_op["parameters"] ==
                object({{"fields", projection}, {"records", "/feed/article"}}),
        "changed correlated article projection for " + id);
    const auto only_reader = plan_probe(base, parameters / "reader-only",
                                        replace_reads(saved, projection_op));
    check(only_reader["validation"].empty(),
          "projection-only arm failed validation");
    for (const auto &route : only_reader["routes"])
      check(!complete(route) &&
                route["uncovered"] == role["requirement"]["uncovered"],
            "projection-only arm removed a remaining obligation");
    item["plans"]["reader-only"] = only_reader;

    const auto fixtures = prior_review / "transfer" / id / "fixtures";
    auto input_hashes = object();
    for (const auto &split : splits)
      input_hashes[split] = hash(fixtures / split / "input/feed.xml");
    item["input_sha256"] = input_hashes;
    auto replay = [&](const std::string &arm, const Path &bundle,
                      const J &spec) {
      const auto file = work / "specs" / id / (arm + ".json");
      write(file, spec);
      const auto result =
          trial(work, arm, id, bundle, file, "feed.xml", input_hashes);
      if (!result["program_sha256"].isNull())
        programs.push_back(work / arm / id / "run/project/converter");
      item["runs"][arm] = result;
      write(work / "progress.json", report);
      return result;
    };
    auto accepted = [&](const std::string &arm, const Path &bundle,
                        const J &spec, const std::string &expected_reader) {
      const auto result = replay(arm, bundle, spec);
      check(result["roles"].size() == 1 && complete(result["roles"]["0"]) &&
                selects(result["roles"]["0"], expected_reader) &&
                result["verified"] == true &&
                !result["program_sha256"].isNull(),
            "fixed recipe failed " + arm + "/" + id);
      const std::string output =
          mime == json ? "result-1.json" : "result-1.txt";
      for (const auto &split : splits)
        check(result["outputs"][split][0] ==
                  bytes(fixtures / split / "output" / output),
              "archived output bytes changed for " + id + " " + split);
      const auto dir = work / arm / id;
      const auto witness =
          extra_case(dir / "feed-witness", dir / "run/project/converter",
                     bundle, feed, array({req.witness}));
      check(witness["passed"] == true,
            "fresh witness output differs for " + arm + "/" + id);
      item["witnesses"][arm] = witness;
    };
    accepted("fixed", mime == tsv ? with_tsv : base, fixed, "xml_records");
    if (mime == tsv) {
      const auto blocked = replay("without-writer", base, fixed);
      check(blocked["roles"].size() == 1 && blocked["verified"] == false &&
                ports(blocked["roles"]["0"]) ==
                    array({array({"strings", "out:" + tsv})}),
            "TSV control exposed a different gap for " + id);
    }
    if (!req.scalar_sha256.empty()) {
      const auto old_scalar = prior_review / "baseline/alternatives" / id /
                              "0/calls/interpret/response.json";
      check(hash(old_scalar) == req.scalar_sha256,
            "changed legacy scalar route for " + id);
      auto scalar = saved;
      scalar["roles"][0]["requirement"] =
          load(old_scalar)["roles"][0]["requirement"];
      item["scalar_recipe_sha256"] = req.scalar_sha256;
      accepted("scalar", base, scalar, "xml_text");
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
