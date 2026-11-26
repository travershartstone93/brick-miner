#include "bundle.hpp"
#include <QByteArray>
#include <algorithm>
#include <sstream>

namespace miner {
Files read_tree(const Path &path) {
  Files result;
  for (const auto &e : fs::recursive_directory_iterator(path))
    if (e.is_regular_file())
      result[e.path().lexically_relative(path).generic_string()] =
          bytes(e.path());
  return result;
}
void write_tree(const Path &path, const Files &files) {
  for (const auto &[name, data] : files) {
    Path relative = name;
    check(!relative.is_absolute() && relative == relative.lexically_normal() &&
              *relative.begin() != "..",
          "invalid bundle member");
    save(path / relative, data);
  }
}
std::string manifest(const Files &files) {
  std::string out;
  for (const auto &[name, data] : files)
    out += sha(data) + "  " + name + "\n";
  return out;
}
std::string seal(Files &bundle) {
  bundle.erase("SHA256SUMS");
  auto components = parse(bundle.at("components.json"));
  J ids = array();
  for (const auto &c : components)
    ids.append(c["id"]);
  std::string verified =
      sha(bundle.at("components.json")) + "  components.json\n";
  for (std::string prefix : {"providers/", "headers/"})
    for (const auto &[n, data] : bundle)
      if (n.starts_with(prefix))
        verified += sha(data) + "  " + n + "\n";
  bundle["verification.json"] =
      dump(object({{"checked", ids}, {"sha256", verified}}));
  auto out = manifest(bundle);
  bundle["SHA256SUMS"] = out;
  return sha(out);
}
Files remove_binding(Files bundle, const J &binding) {
  auto &raw = bundle.at("providers/contracts.json");
  auto registry = parse(raw), kept = array();
  for (const auto &row : registry["providers"])
    if (row != binding)
      kept.append(row);
  check(kept.size() + 1 == registry["providers"].size(),
        "binding removal must identify exactly one row");
  registry["providers"] = kept;
  raw = dump(registry);
  seal(bundle);
  return bundle;
}
Files register_tasks(Files base, const J &tasks, const Files &headers,
                     const J &meta, const Path &work, const J &types,
                     const J &conversions) {
  auto cards = parse(base.at("catalog.json")),
       components = parse(base.at("components.json")),
       contracts = parse(base.at("providers/contracts.json")),
       reviews = parse(base.at("reviews.json"));
  for (const auto &task : tasks) {
    auto key = task["id"].asString(), id = "miner_" + key, header = id + ".hpp";
    for (const auto &c : cards)
      check(c["id"] != id, "duplicate candidate brick " + id);
    check(!contracts["outcomes"].isMember(key), "duplicate outcome " + key);
    auto desc = task["meaning"];
    auto card = object({{"id", id},
                        {"description", desc},
                        {"outputs", array({desc})},
                        {"tags", array()}});
    cards.append(card);
    card["operation"] = key;
    card["header"] = header;
    card["source"] = "headers/" + header;
    card["packages"] = task["packages"];
    card["origin"] = task.get(
        "origin", key == "line_xml_escape" ? "mined_application" : "mined_api");
    card["provenance"] = meta[key];
    components.append(card);
    auto parameters = task.get("args", object());
    contracts["outcomes"][key] =
        object({{"meaning", desc}, {"parameters", parameters}});
    auto args =
        task.get("arg_order", string_array(parameters.getMemberNames()));
    contracts["providers"].append(
        object({{"id", key},
                {"brick", id},
                {"from", task.get("from", "strings")},
                {"to", task["to"]},
                {"effect", key},
                {"header", header},
                {"call", task.get("call", "brick_miner::" + key)},
                {"args", args}}));
    base["headers/" + header] = headers.at(key);
  }
  for (const auto &k : types.getMemberNames()) {
    check(!contracts["types"].isMember(k), "duplicate type " + k);
    contracts["types"][k] = types[k];
  }
  for (const auto &p : conversions)
    contracts["providers"].append(p);
  auto index = work / "format-index.json";
  write(index, contracts);
  run({main_repo / "build/native/brick-index-formats", index});
  base["providers/contracts.json"] = dump(load(index));
  reviews["catalog"] = cards;
  base["catalog.json"] = dump(cards);
  base["reviews.json"] = dump(reviews);
  base["components.json"] = dump(components);
  return base;
}
Files ablate(Files bundle, const std::string &key) {
  auto id = "miner_" + key;
  auto remove = [&](const J &list, const std::string &field) {
    auto a = array();
    for (const auto &c : list)
      if (c[field] != id)
        a.append(c);
    return a;
  };
  auto contracts = parse(bundle.at("providers/contracts.json"));
  contracts["providers"] = remove(contracts["providers"], "brick");
  bundle["providers/contracts.json"] = dump(contracts);
  for (const auto *f : {"components.json", "catalog.json"})
    bundle[f] = dump(remove(parse(bundle.at(f)), "id"));
  auto reviews = parse(bundle.at("reviews.json"));
  reviews["catalog"] = parse(bundle.at("catalog.json"));
  bundle["reviews.json"] = dump(reviews);
  check(bundle.erase("headers/" + id + ".hpp") == 1,
        "missing ablated implementation");
  return bundle;
}
J spec_for(const std::string &input, const std::string &output,
           const J &requirement) {
  return object({{"input_type", input},
                 {"roles", array({object({{"id", "r"},
                                          {"content", "whole"},
                                          {"multiple", false},
                                          {"output_type", output},
                                          {"requirement", requirement}})})}});
}
J plan(const Path &bundle, const J &spec, const Path &dest) {
  fs::create_directories(dest / "bricks");
  write(dest / "calls/interpret/response.json", spec);
  write(dest / "requirements.json",
        object({{"r", spec["roles"][0]["requirement"]}}));
  write(dest / "route.json",
        object({{"rows", array({object({{"role", "r"}, {"bound", false}})})}}));
  write(dest / "components/helpers.json", array());
  run({"bash",
       main_repo / "experiments/boilerplate/integrated/provider-stage.sh",
       bundle},
      {.cwd = dest,
       .environment = {{"BRICK_PROVIDER_PLANNER",
                        main_repo / "build/native/brick-provider-plan"},
                       {"BRICK_LOGIC_WORKER",
                        main_repo / "build/native/unllm_logic_worker"}}});
  return load(dest / "components/emission.json");
}
Strings flags(const Strings &packages) {
  if (packages.empty())
    return {};
  Strings cmd = {"pkg-config", "--cflags", "--libs"};
  append(cmd, packages);
  return command_args(object({{"command", run(cmd)}}));
}
void build_emission(const J &provider, const Path &dest, const Path &exe,
                    const std::string &compiler) {
  save(dest / "component.hpp", provider["source"].asString());
  save(dest / "main.cpp",
       "#include \"component.hpp\"\nint main(int argc,char** argv) {\n    if "
       "(argc != 3) return 2;\n    try { component_0(argv[1],argv[2]); }\n    "
       "catch (const std::exception&) { return 1; }\n}\n");
  std::set<std::string> packages;
  for (const auto &h : load(dest / "components/helpers.json"))
    for (const auto &p : h["packages"])
      packages.insert(p.asString());
  Strings argv = {compiler,        "-std=c++17",      "-fPIC", "-O1", "-I",
                  dest / "bricks", dest / "main.cpp", "-o",    exe};
  append(argv, flags({packages.begin(), packages.end()}));
  run(argv);
}
J evaluate(const Path &exe, const J &cases, const Path &dest) {
  auto results = array();
  for (const auto &c : cases) {
    auto d = dest / c["name"].asString();
    check(fs::create_directory(d), "case already exists");
    auto err = c["error"].asString();
    if (c.isMember("input"))
      save(d / "in", c["input"].asString());
    if (c.isMember("input_hex"))
      save(d / "in", QByteArray::fromHex(
                         QByteArray::fromStdString(c["input_hex"].asString()))
                         .toStdString());
    if (err == "destination-directory")
      fs::create_directory(d / "out");
    else if (!err.empty())
      save(d / "out", "untouched");
    auto argv = sandbox(exe, d);
    append(argv, {"in", "out"});
    auto p = execute(argv, {.cwd = d, .seconds = 10, .probe_limits = true});
    bool passed = false;
    if (err.empty() && !p.status)
      passed = load(d / "out") == c["output"];
    else if (!err.empty() && p.status == 1)
      passed = err == "destination-directory" ? fs::is_directory(d / "out")
                                              : bytes(d / "out") == "untouched";
    results.append(
        object({{"case", c["name"]}, {"exit", p.status}, {"passed", passed}}));
    check(passed,
          "emitted program failed " + c["name"].asString() + " " + p.err);
  }
  return results;
}
} // namespace miner
