#include "path_ops.hpp"
#include <iostream>
#include <json/json.h>
#include <type_traits>

int main(int argc, char **argv) {
  static_assert(std::is_same_v<decltype(&bricks::tree),
                               std::vector<std::string> (*)(const char *)>);
  if (argc != 3)
    return 2;
  auto operation =
      std::string(argv[1]) == "shallow" ? bricks::files : bricks::tree;
  try {
    Json::Value rows(Json::arrayValue);
    for (const auto &path : operation(argv[2]))
      rows.append(path);
    std::cout << rows;
  } catch (const std::filesystem::filesystem_error &e) {
    if (e.code() == std::errc::no_such_file_or_directory)
      std::cout << "no_such_file_or_directory";
    else if (e.code() == std::errc::not_a_directory)
      std::cout << "not_a_directory";
    else
      return 3;
    return 1;
  }
}
