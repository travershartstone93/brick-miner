#include "gir_g_uri_list_extract_uris.hpp"
#include <cstdio>
#include <stdexcept>
#include <type_traits>

int main(int argc, char **argv) {
  static_assert(
      std::is_same_v<decltype(&unllm_generated::read_g_uri_list_extract_uris),
                     std::vector<std::string> (*)(const char *)>);
  if (argc != 2)
    return 2;
  try {
    for (const auto &s :
         unllm_generated::read_g_uri_list_extract_uris(argv[1])) {
      for (unsigned char byte : s)
        std::printf("%02x", static_cast<unsigned>(byte));
      std::putchar('\n');
    }
  } catch (const std::runtime_error &e) {
    std::printf("%s\n", e.what());
    return 1;
  }
}
