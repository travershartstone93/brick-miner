#include <climits>
#include <cstdint>
#include <iostream>
#include <string>

// BOUND_GUARD comes from the generated adapter. Only its predicate is run
// over synthetic lengths. No corresponding character buffer is allocated.
struct Record {
  std::size_t length;
  bool contains_nul;
  static constexpr auto npos = std::string::npos;
  std::size_t size() const { return length; }
  std::size_t find(char byte) const {
    return byte == 0 && contains_nul ? 0 : npos;
  }
};
int main() {
  std::cout << CHAR_BIT * sizeof(int) << ' ' << INT_MAX << '\n';
  for (std::size_t length :
       {0ULL, 1ULL, 2147483646ULL, 2147483647ULL, 2147483648ULL, 2147483649ULL})
    for (bool has_nul : {false, true}) {
      const Record s{length, has_nul};
      bool rejected = BOUND_GUARD;
      std::cout << length << ' ' << has_nul << ' ' << rejected << '\n';
      if (rejected != (has_nul || length > 2147483647ULL))
        return 1;
    }
  return (CHAR_BIT * sizeof(int) == 32 && INT_MAX == 2147483647) ? 0 : 2;
}
