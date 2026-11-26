#include "text_ops.hpp"
#include <algorithm>
#include <iostream>
#include <json/json.h>

using Strings = bricks::Strings;
namespace {
Strings transform_records(const std::string &id, const Strings &records) {
  if (id == "sort_lines")
    return bricks::sorted(records);
  if (id == "reverse_order")
    return bricks::reversed(records);
  if (id == "unique_lines")
    return bricks::unique(records);
  if (id == "adjacent_unique")
    return bricks::consecutive(records);
  if (id == "nonempty_lines")
    return bricks::nonempty(records);
  throw std::invalid_argument("unknown binding");
}
Json::Value array(const Strings &v) {
  Json::Value out(Json::arrayValue);
  for (const auto &s : v)
    out.append(s);
  return out;
}
bool correct(const std::string &id, const Strings &in, const Strings &out) {
  if (id == "sort_lines")
    return std::is_sorted(out.begin(), out.end()) &&
           std::is_permutation(in.begin(), in.end(), out.begin(), out.end());
  if (id == "reverse_order")
    return std::equal(out.begin(), out.end(), in.rbegin(), in.rend());
  if (id == "nonempty_lines") {
    Strings expected;
    for (const auto &record : in)
      if (record.size() != 0)
        expected.push_back(record);
    return out == expected;
  }
  Strings expected;
  for (auto it = in.begin(); it != in.end(); ++it) {
    bool keep = id == "unique_lines"
                    ? std::find(in.begin(), it, *it) == it
                    : it == in.begin() || *std::prev(it) != *it;
    if (keep)
      expected.push_back(*it);
  }
  return out == expected;
}
void visit(const std::string &id, Strings &in, Json::Value &rows) {
  auto out = transform_records(id, in);
  if (!correct(id, in, out) ||
      transform_records(id, out) != (id == "reverse_order" ? in : out))
    throw std::runtime_error("finite law or reference mismatch");
  rows.append(array(out));
  if (in.size() == 4)
    return;
  for (auto word : {"", "a", "z"}) {
    in.emplace_back(word);
    visit(id, in, rows);
    in.pop_back();
  }
}
} // namespace
int main(int argc, char **argv) try {
  if (argc != 2)
    return 2;
  Json::Value cases, result;
  if (!(std::cin >> cases) || !cases.isArray())
    return 2;
  result["fixed"] = Json::Value(Json::arrayValue);
  for (const auto &row : cases) {
    if (!row.isArray())
      return 2;
    Strings in;
    for (const auto &entry : row) {
      if (!entry.isString())
        return 2;
      in.push_back(entry.asString());
    }
    result["fixed"].append(array(transform_records(argv[1], in)));
  }
  Strings in;
  visit(argv[1], in, result["bounded"]);
  std::cout << result;
  return 0;
} catch (const std::exception &error) {
  std::cerr << error.what() << "\n";
  return 1;
}
