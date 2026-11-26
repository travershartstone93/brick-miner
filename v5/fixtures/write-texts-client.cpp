#include "json_sequence.hpp"
#include <charconv>
#include <climits>
#include <iostream>
#include <limits>
#include <vector>

using Records = std::vector<std::string>;
static_assert(std::is_same_v<decltype(&unllm::write_json_sequence<Records>),
                             void (*)(const char *, const Records &)>);

int main(int argc, char **argv) {
  if (argc == 2 && std::string(argv[1]) == "--int-max") {
    static_assert(CHAR_BIT == 8);
    std::cout << std::numeric_limits<int>::max();
    return 0;
  }
  if (argc < 2)
    return 2;
  Records records;
  for (int i = 2; i < argc; ++i) {
    std::string hex(argv[i]), record;
    if (hex.size() % 2)
      return 2;
    for (size_t j = 0; j < hex.size(); j += 2) {
      unsigned byte = 0;
      auto end = hex.data() + j + 2;
      auto result = std::from_chars(end - 2, end, byte, 16);
      if (result.ec != std::errc{} || result.ptr != end)
        return 2;
      record.push_back(static_cast<char>(byte));
    }
    records.push_back(record);
  }
  try {
    unllm::write_json_sequence(argv[1], records);
  } catch (const std::ios_base::failure &) {
    std::cout << "ios_base::failure";
    return 1;
  }
}
