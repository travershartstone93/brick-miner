#include "archive_ops.hpp"
#include <iostream>
#include <json/json.h>
#include <type_traits>
static_assert(std::is_same_v<decltype(&bricks::archives::names<AE_IFREG>),
                             std::vector<std::string> (*)(const char *)>);
int main(int argc, char **argv) {
  if (argc != 3)
    return 2;
  try {
    const auto names = std::string(argv[1]) == "all"
                           ? bricks::archives::names<>(argv[2])
                           : bricks::archives::names<AE_IFREG>(argv[2]);
    Json::Value result(Json::arrayValue);
    for (const auto &s : names)
      result.append(s);
    std::cout << result;
  } catch (const std::runtime_error &) {
    std::cout << "runtime_error";
    return 1;
  }
}
