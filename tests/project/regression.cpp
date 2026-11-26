#include "common.hpp"
#include <QTemporaryDir>
#include <algorithm>
#include <iostream>
#include <regex>
#include <sstream>

using namespace miner;
namespace {
const Strings inputs = {"-2147483648", "-7", "-1", "0",         "1",
                        "6",           "12", "19", "2147483647"};
const std::vector<std::pair<std::string, Strings>> oracles = {
    {"phase_offset", {"1", "3", "15", "-7", "-5", "5", "-7", "7", "7"}},
    {"inside_window", {"0", "0", "0", "1", "1", "0", "1", "0", "0"}},
    {"tag_level", {"-9", "-6", "-3", "1", "3", "3", "6", "12", "6"}},
    {"through_class", {"-11", "-2", "17", "4", "11", "21", "16", "44", "30"}},
    {"CycleView::joined", {"-8", "0", "18", "4", "10", "20", "14", "40", "28"}},
    {"CycleView::sign", {"0", "0", "1", "1", "1", "1", "1", "1", "1"}},
    {"Calibration::adjusted",
     {"-5", "-2", "1", "5", "7", "7", "10", "16", "10"}}};
using Names = std::map<std::string, std::string>;
const Names replacements = {{"phase_offset", "azimuth_903"},
                            {"inside_window", "accrual_814"},
                            {"tag_level", "station_477"},
                            {"bucket", "isotope_198"},
                            {"scale", "matrix_952"},
                            {"next_count", "lag_833"},
                            {"add_tick", "tock"},
                            {"through_class", "seasons_345"},
                            {"Calibration", "OffsetData"},
                            {"adjusted", "measured"},
                            {"CycleView", "ClimateData"},
                            {"joined", "crest"},
                            {"sign", "flag"},
                            {"shifted_count", "crest_571"},
                            {"external_read", "pin_776"},
                            {"previous_read", "roll_054"}};
std::string rename_words(const std::string &text, const Names &names) {
  std::string out;
  std::regex words("[A-Za-z_][A-Za-z_0-9]*");
  size_t offset = 0;
  for (std::sregex_iterator it(text.begin(), text.end(), words), end; it != end;
       ++it) {
    out += text.substr(offset, it->position() - offset);
    auto s = it->str();
    out += names.contains(s) ? names.at(s) : s;
    offset = it->position() + it->length();
  }
  return out + text.substr(offset);
}
std::string feed(const Path &exe) {
  return run({exe}, {.input = join(inputs, "\n"), .seconds = 5});
}
J trial(const Path &work, const Names &names, bool reverse) {
  auto app = work / "app", build = work / "build", dest = work / "mine";
  fs::create_directories(work);
  fs::copy(root / "tests/project/fixture", app, fs::copy_options::recursive);
  if (!names.empty())
    for (const auto &e : fs::directory_iterator(app))
      if (e.path().extension() == ".cpp" || e.path().extension() == ".hpp")
        save(e.path(), rename_words(bytes(e.path()), names));
  run({"cmake", "-G", "Ninja", "-S", app, "-B", build,
       "-DCMAKE_CXX_COMPILER=clang++", "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"});
  run({"cmake", "--build", build});
  if (reverse) {
    auto db = build / "compile_commands.json";
    auto rows = load(db);
    J reversed = array();
    for (unsigned i = rows.size(); i > 0; --i)
      reversed.append(rows[i - 1]);
    write(db, reversed);
  }
  std::istringstream source_output(feed(build / "cycle_report"));
  std::vector<Strings> upstream(oracles.size());
  for (size_t i = 0; i < inputs.size(); ++i)
    for (auto &col : upstream) {
      std::string v;
      check(bool(source_output >> v), "short fixture output");
      col.push_back(v);
    }
  run({root / "mine-project", "--source", app, "--database", build, "--output",
       dest});
  auto records = load(dest / "inventory.json"),
       report = load(dest / "report.json");
  check(report["units"] ==
                array({"app.cpp", "classes.cpp", "phases.cpp", "tags.cpp"}) &&
            report["bundle_manifest_sha256"].isNull() &&
            report["qualifications"].empty(),
        "incorrect fixture report");
  std::map<std::string, std::vector<J>> symbols;
  J payloads = object(), ids = object();
  for (const auto &r : records) {
    symbols[r["symbol"].asString()].push_back(r);
    if (r["status"] != "extracted")
      continue;
    auto id = r["id"].asString();
    check(report["checks"][id]["status"] == "matches original on probes",
          "client mismatch for " + id);
    for (const auto &f : r["files"])
      check(bytes(dest / "candidates" / id / "original" / f.asString()) ==
                bytes(app / f.asString()),
            "retained source changed");
    payloads[id] = sha(bytes(dest / "candidates" / id / "candidate.hpp"));
  }
  fs::remove_all(app);
  fs::remove_all(build);
  auto lookup = [&](const std::string &name) -> const J & {
    const auto &matches = symbols.at(rename_words(name, names));
    check(matches.size() == 1, "ambiguous " + name);
    return matches[0];
  };
  for (std::string name : {"next_count", "shifted_count", "external_read",
                           "previous_read", "cross_bad", "Tally::add"}) {
    const auto &r = lookup(name);
    check(r["status"] == "rejected" && !r["reason"].asString().empty(),
          "missing rejection: " + name);
    if (name == "cross_bad")
      check(r["reason"].asString().find("mutable global") != std::string::npos,
            "wrong transitive rejection");
  }
  std::set<std::string> files, helper_ids;
  for (const auto &r : symbols.at(rename_words("bucket", names))) {
    files.insert(r["source"].asString());
    helper_ids.insert(r["id"].asString());
  }
  check(files == std::set<std::string>{"phases.cpp", "tags.cpp"} &&
            helper_ids.size() == 2,
        "same-name helpers collided");
  for (size_t i = 0; i < oracles.size(); ++i) {
    const auto &[symbol, expected] = oracles[i];
    const auto &r = lookup(symbol);
    auto closure = strings(r["closure"]);
    auto includes = [&](const std::string &s) {
      return std::find(closure.begin(), closure.end(),
                       rename_words(s, names)) != closure.end();
    };
    check(r["status"] == "extracted" && r["header"] == "cycle.hpp" &&
              includes("bucket"),
          "incomplete closure: " + symbol);
    if (symbol == "through_class")
      check(r["oracle_units"] ==
                    array({"classes.cpp", "phases.cpp", "tags.cpp"}) &&
                includes("add_tick"),
            "class closure changed");
    auto id = r["id"].asString();
    ids[symbol] = id;
    auto folder = work / "standalone" / symbol;
    fs::create_directories(folder);
    fs::copy_file(dest / "candidates" / id / "candidate.hpp",
                  folder / "candidate.hpp");
    std::string args = r["receiver_fields"].empty() ? "input" : "4, input";
    save(folder / "client.cpp",
         "#include \"candidate.hpp\"\n#include <iostream>\nint main() { int "
         "input; while (std::cin >> input) std::cout << brick_mined::" +
             id + "::invoke(" + args + ") << \"\\n\"; }\n");
    run({"clang++", "-std=c++17", folder / "client.cpp", "-o",
         folder / "client"});
    check(feed(folder / "client") == join(expected, "\n") + "\n" &&
              upstream[i] == expected,
          "wrong isolated output: " + symbol);
  }
  check(payloads.size() >= 7, "missing candidate headers");
  return object({{"inventory", sha(bytes(dest / "inventory.json"))},
                 {"payloads", payloads},
                 {"ids", ids}});
}
} // namespace
int main() try {
  QTemporaryDir dir;
  check(dir.isValid(), "cannot create fixture directory");
  Path tmp = dir.path().toStdString();
  auto first = trial(tmp / "plain", {}, false);
  check(first == trial(tmp / "different directory", {}, true),
        "relocation or reordered compilation changed results");
  auto renamed = trial(tmp / "renamed", replacements, false);
  for (const auto &[s, values] : oracles)
    check(first["ids"][s] != renamed["ids"][s], "stale ID: " + s);
  std::cout << "Passed cross-file discovery, rejection, isolated clients, "
               "relocation, reordering and renaming.\n";
  return 0;
} catch (const std::exception &e) {
  std::cerr << e.what() << "\n";
  return 1;
}
