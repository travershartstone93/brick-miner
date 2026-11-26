#include "bundle.hpp"
#include "probes.hpp"
#include <algorithm>
#include <tuple>

namespace miner {
namespace {
J test_project(const Path &base, const Path &staged, const Files &files,
               const J &contract, const Path &work) {
  auto name = contract["id"].asString();
  J parameters = object();
  auto types = strings(contract["parameters"]);
  for (size_t i = 0; i < types.size(); ++i)
    parameters["column_" + std::to_string(i + 1)] = unsigned(i + 1);
  auto req = object(
      {{"outcome", name}, {"parameters", parameters}, {"uncovered", array()}});
  auto spec = spec_for("text/tab-separated-values", "application/json", req);
  J receipt = object({{"task", spec}});
  for (std::string label : {"baseline", "staged", "removed"}) {
    auto dest = work / label, tmp = dest / "control-bundle";
    Path bundle = label == "baseline" ? base : staged;
    if (label == "removed") {
      write_tree(tmp, ablate(files, name));
      bundle = tmp;
    }
    auto emission = plan(bundle, spec, dest);
    if (label == "removed")
      fs::remove_all(tmp);
    J complete = array();
    for (const auto &p : emission["providers"])
      if (p["status"] == "complete" && p["uncovered"].empty())
        complete.append(p);
    if (label != "staged") {
      check(emission["interpretation_errors"].empty() && complete.empty(),
            "control unexpectedly emitted " + name);
      receipt[label] =
          object({{"status", "no program"}, {"gaps", emission["gaps"]}});
      continue;
    }
    check(complete.size() == 1, "missing fixed task plan " + name);
    const auto &p = complete[0];
    auto ids = strings(p["bricks"]);
    auto source = p["source"].asString();
    check(std::find(ids.begin(), ids.end(), "miner_" + name) != ids.end() &&
              source.find("brick_mined::" + name + "(") != source.npos,
          "wrong implementation selected");
    build_emission(p, dest, dest / "cli");
    auto expected = array();
    std::string input;
    for (const auto &c : contract["cases"]) {
      Strings fields = {"record"};
      bool valid = true;
      for (const auto &v : c[0]) {
        auto text = v.isBool() ? std::to_string(v.asBool()) : v.asString();
        if (text.find_first_of("\t\n") != text.npos)
          valid = false;
        fields.push_back(text);
      }
      if (valid) {
        input += join(fields, "\t") + "\n";
        expected.append(c[1]);
      }
    }
    check(!expected.empty(), "missing TSV-compatible probe");
    auto cases = array(
        {object(
             {{"name", "all-cases"}, {"input", input}, {"output", expected}}),
         object({{"name", "empty"}, {"input", ""}, {"output", array()}}),
         object({{"name", "missing"}, {"error", "missing"}}),
         object({{"name", "unwritable"},
                 {"input", "label\t0\n"},
                 {"error", "destination-directory"}})});
    if (!types.empty())
      cases.append(object({{"name", "missing-column"},
                           {"input", "record\n"},
                           {"error", "invalid-input"}}));
    if (std::any_of(types.begin(), types.end(),
                    [](const auto &t) { return !is_text(t); })) {
      Strings invalid(types.size(), "xyz");
      invalid.insert(invalid.begin(), "record");
      cases.append(object({{"name", "malformed-number"},
                           {"input", join(invalid, "\t") + "\n"},
                           {"error", "invalid-input"}}));
    }
    receipt[label] = object({{"status", "passed"},
                             {"bricks", p["bricks"]},
                             {"checks", evaluate(dest / "cli", cases, dest)},
                             {"emission_sha256", sha(source)},
                             {"test_rows", expected.size()}});
  }
  return receipt;
}
} // namespace
std::pair<J, J> publish_project(const Path &base, const Path &work,
                                const J &provenance, const J &records,
                                const J &checks, const J &contracts,
                                const J &units, const Path &generation) {
  auto files = read_tree(generation);
  auto outcomes = parse(files.at("providers/contracts.json"))["outcomes"];
  J results = object(), meta = object(), tasks = array(), selected = object(),
    matches = object();
  Files headers;
  std::map<std::string, std::string> owners;
  for (const auto &c : contracts) {
    auto name = c["id"].asString();
    bool exists = outcomes.isMember(name);
    for (const auto &e : c["equivalent_outcomes"])
      exists |= outcomes.isMember(e.asString());
    if (exists) {
      results[name] = object({{"status", "already in catalog"}});
      continue;
    }
    std::vector<J> candidates;
    matches[name] = array();
    for (const auto &r : records) {
      if (!r.isMember("id"))
        continue;
      for (const auto &match : checks[r["id"].asString()]["contracts_matched"])
        if (match == name) {
          candidates.push_back(r);
          matches[name].append(r["id"]);
        }
    }
    if (candidates.empty()) {
      results[name] = object({{"status", "no match"}});
      continue;
    }
    auto rank = [](const J &r) {
      return std::tuple{r["closure"].size(), r["source"].asString(),
                        r["line"].asUInt(), r["id"].asString()};
    };
    const auto &r = *std::min_element(
        candidates.begin(), candidates.end(),
        [&](const auto &a, const auto &b) { return rank(a) < rank(b); });
    auto id = r["id"].asString();
    if (owners.contains(id)) {
      results[name] =
          object({{"status", "same implementation matched another contract"},
                  {"other_contract", owners[id]},
                  {"candidate", id}});
      continue;
    }
    owners[id] = name;
    auto paths = strings(r["files"]);
    paths.push_back(r["source"].asString());
    if (!r["header"].asString().empty())
      paths.push_back(r["header"].asString());
    for (const auto *ext : {".h", ".hpp", ".hh", ".hxx"}) {
      auto f = Path(r["source"].asString()).replace_extension(ext);
      if (fs::is_regular_file(base / f))
        paths.push_back(f);
    }
    J input = object();
    for (const auto &f : paths)
      input[f] = sha(bytes(project_file(base, f)));
    auto code = bytes(work / "candidates" / id / "candidate.hpp");
    headers[name] = row_header(r, name, code);
    auto package = provenance;
    package.removeMember("file_evidence");
    package.removeMember("translation_units");
    auto cmd = units[r["source"].asString()];
    auto args = command_args(cmd);
    for (auto &a : args)
      a = replace(replace(a, base.string(), "{source}"),
                  cmd["directory"].asString(), "{build}");
    meta[name] = object({{"route", "compile-database AST extraction"},
                         {"source_package", package},
                         {"symbol", r["symbol"]},
                         {"signature", r["signature"]},
                         {"closure", r["closure"]},
                         {"input_files", input},
                         {"source_slices", r["slices"]},
                         {"original_compile_command", string_array(args)},
                         {"candidate_sha256", sha(code)},
                         {"candidate_id", id},
                         {"build_flags", array({"-std=c++17"})},
                         {"runtime_packages", array()}});
    auto params = object(), order = array();
    for (unsigned i = 0; i < r["parameters"].size(); ++i) {
      auto col = "column_" + std::to_string(i + 1);
      params[col] = "column";
      order.append(col);
    }
    tasks.append(object(
        {{"id", name},
         {"from", "rows"},
         {"to", is_text(r["return_type"].asString()) ? "strings" : "integers"},
         {"call", "brick_mined::" + name},
         {"packages", array()},
         {"origin", "mined_application"},
         {"args", params},
         {"arg_order", order},
         {"meaning", c["meaning"]}}));
    selected[name] = id;
  }
  if (selected.empty())
    return {results, {}};
  auto stage = register_tasks(std::move(files), tasks, headers, meta, work);
  auto draft = work / ".bundle";
  check(fs::create_directory(draft), "staging directory exists");
  write_tree(draft, stage);
  for (const auto &c : contracts) {
    auto name = c["id"].asString();
    if (!selected.isMember(name))
      continue;
    auto id = selected[name].asString();
    auto receipt =
        test_project(generation, draft, stage, c, work / "planner" / name);
    receipt["status"] = "qualified";
    receipt["selected"] = id;
    receipt["matching_candidates"] = matches[name];
    results[name] = receipt;
    auto prefix = "qualifications/miner_" + name + "/";
    stage[prefix + "contract.json"] = dump(c);
    stage[prefix + "receipt.json"] = dump(receipt);
    stage[prefix + "provenance.json"] = dump(meta[name]);
    stage[prefix + "candidate.hpp"] =
        bytes(work / "candidates" / id / "candidate.hpp");
    auto originals = meta[name]["input_files"].getMemberNames();
    append(originals, strings(provenance["license_files"]));
    for (const auto &f : originals)
      stage["qualifications/original/" + f] = bytes(project_file(base, f));
  }
  auto hash = seal(stage);
  write_tree(draft, stage);
  run({"sha256sum", "--check", "--strict", "--quiet", "SHA256SUMS"},
      {.cwd = draft});
  fs::rename(draft, work / "bundle");
  return {results, hash};
}
} // namespace miner
