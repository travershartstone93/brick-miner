#include "link.hpp"
#include <algorithm>
#include <functional>
#include <numeric>

namespace miner {
namespace {
using Index = std::map<std::string, std::vector<const J *>>;
using Address = std::pair<std::string, unsigned>;
using Edge = std::pair<const J *, size_t>;

// Each compiler USR identifies an external declaration. Keep
// translation-unit-local helpers in their own module unless they need to share
// the same original class.
class Closure {
  const Index &index;
  std::vector<const J *> ordered;
  std::vector<std::vector<Edge>> edges;
  std::map<const J *, size_t> found;
  std::set<const J *> visiting;
  std::vector<size_t> parent;
  std::map<size_t, std::vector<size_t>> modules;
  std::set<size_t> rendered, active;
  Strings output;

  size_t visit(const J &record) {
    if (found.contains(&record))
      return found.at(&record);
    check(visiting.insert(&record).second,
          "cyclic cross-unit dependency at " + record["symbol"].asString());
    check(record["status"] == "extracted" || record["status"] == "dependency",
          record["symbol"].asString() + ": " + record["reason"].asString());
    std::vector<Edge> dependencies;
    for (const auto &edge : record["imports"]) {
      auto matches = index.find(edge["usr"].asString());
      size_t count = matches == index.end() ? 0 : matches->second.size();
      check(count == 1, std::to_string(count) + " project definitions for " +
                            edge["symbol"].asString());
      dependencies.emplace_back(&edge, visit(*matches->second.front()));
    }
    visiting.erase(&record);
    found[&record] = ordered.size();
    ordered.push_back(&record);
    edges.push_back(std::move(dependencies));
    return ordered.size() - 1;
  }
  size_t group(size_t n) const {
    while (parent[n] != n)
      n = parent[n];
    return n;
  }
  std::string name(size_t n) const {
    return "project_piece_" + std::to_string(group(n));
  }
  void render(size_t n) {
    n = group(n);
    if (rendered.contains(n))
      return;
    check(active.insert(n).second, "cyclic dependency between source modules");
    std::set<std::string> imports;
    std::map<Address, std::string> chunks;
    std::map<std::string, std::string> symbols;
    Strings statements, definitions;
    for (auto member : modules.at(n)) {
      for (auto [edge, target] : edges[member]) {
        if (group(target) == n)
          continue;
        render(target);
        auto scope = strings((*edge)["scope"]);
        auto qualifier = join(scope, "::");
        auto decl = "using " + name(target) +
                    "::" + (scope.empty() ? "" : qualifier + "::") +
                    (*edge)["name"].asString() + ";";
        if (!scope.empty())
          decl = "namespace " + qualifier + " { " + decl + " }";
        if (imports.insert(decl).second)
          statements.push_back(decl);
      }
      const auto &record = *ordered[member];
      for (unsigned i = 0; i < record["slices"].size(); ++i) {
        const auto &slice = record["slices"][i];
        auto file = slice["source"].asString(),
             symbol = slice["symbol"].asString();
        auto [sym, unused] = symbols.emplace(symbol, file);
        check(sym->second == file,
              "cannot merge same-name helpers into one class scope: " + symbol);
        auto code = record["fragments"][i]["code"].asString();
        auto [chunk, inserted] =
            chunks.emplace(Address{file, slice["offset"].asUInt()}, code);
        check(chunk->second == code,
              "inconsistent compiled definitions of " + symbol);
        if (inserted)
          definitions.push_back(code);
      }
    }
    output.push_back("namespace " + name(n) + " {");
    append(output, statements);
    append(output, definitions);
    output.push_back("}");
    active.erase(n);
    rendered.insert(n);
  }

public:
  explicit Closure(const Index &index) : index(index) {}
  J assemble(const J &root_record) {
    auto entry = visit(root_record);
    parent.resize(ordered.size());
    std::iota(parent.begin(), parent.end(), 0);
    std::map<Address, size_t> types;
    std::set<std::string> files, includes, units, seen;
    J slices = array(), closure = array();
    for (size_t n = 0; n < ordered.size(); ++n) {
      const auto &r = *ordered[n];
      check(r["slices"].size() == r["fragments"].size(),
            "misaligned source fragments");
      for (unsigned i = 0; i < r["slices"].size(); ++i) {
        const auto &s = r["slices"][i];
        slices.append(s);
        if (r["fragments"][i]["kind"] == "type") {
          auto [it, inserted] = types.emplace(
              Address{s["source"].asString(), s["offset"].asUInt()}, n);
          parent[group(n)] = group(it->second);
        }
      }
      for (const auto &f : r["files"])
        files.insert(f.asString());
      for (const auto &h : r["includes"])
        includes.insert(h.asString());
      for (const auto &c : r["closure"])
        if (seen.insert(c.asString()).second)
          closure.append(c);
      units.insert(r["source"].asString());
    }
    for (size_t n = 0; n < ordered.size(); ++n)
      modules[group(n)].push_back(n);
    render(entry);
    J r = root_record;
    r["original_callable"] = r["callable"];
    r["callable"] = name(entry) + "::" + r["callable"].asString();
    r["definitions"] = join(output, "\n") + "\n";
    r["files"] = string_array({files.begin(), files.end()});
    r["includes"] = string_array({includes.begin(), includes.end()});
    r["closure"] = closure;
    r["slices"] = slices;
    r["oracle_units"] = string_array({units.begin(), units.end()});
    return r;
  }
};
void reject(J &r) {
  r["status"] = "rejected";
  for (const auto *k :
       {"definitions", "files", "includes", "closure", "slices", "callable"})
    r.removeMember(k);
}
} // namespace
J link_records(const J &records) {
  Index index;
  for (const auto &r : records)
    if (r.isMember("usr"))
      index[r["usr"].asString()].push_back(&r);
  J out = array();
  for (const auto &r : records) {
    J linked = r;
    if (r["status"] == "dependency")
      reject(linked);
    else if (!r["imports"].empty()) {
      try {
        linked = Closure(index).assemble(r);
      } catch (const std::runtime_error &e) {
        reject(linked);
        linked["reason"] = e.what();
      }
    }
    linked.removeMember("fragments");
    out.append(linked);
  }
  return out;
}
J scan(const Path &source, const Path &database, const Strings &unit_names) {
  auto units = unit_names;
  std::sort(units.begin(), units.end());
  J records = array();
  for (const auto &unit : units) {
    auto output = parse(run({root / ".build/brick-scan", database,
                             project_file(source, unit), source}));
    for (const auto &r : output)
      records.append(r);
  }
  auto linked = link_records(records);
  std::vector<J> sorted(linked.begin(), linked.end());
  std::stable_sort(sorted.begin(), sorted.end(), [](const J &a, const J &b) {
    for (const auto *k : {"source", "line", "column", "signature"})
      if (a[k] != b[k])
        return a[k] < b[k];
    return false;
  });
  J result = array();
  for (auto &r : sorted)
    result.append(std::move(r));
  return result;
}
} // namespace miner
