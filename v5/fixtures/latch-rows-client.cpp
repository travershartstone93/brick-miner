#include "threshold_provider.hpp"
#include <iostream>

int main() {
  const brick_data::Rows sample = [](const brick_data::Visitor &visit) {
    visit(1, {"A", "8"});
    visit(2, {"B", "8"});
    visit(3, {"A", "8"});
    visit(4, {"A", "4"});
    visit(5, {"A", "9"});
    visit(6, {"B", "9"});
  };
  const std::vector<std::string> expected{"A", "B", "A"};
  for (int call = 0; call < 2; ++call) {
    if (brick_data::threshold_entries<true>(sample, 0, 1,
                                            std::greater_equal<>{}, 8,
                                            std::less_equal<>{}, 4) != expected)
      return 1;
  }
  // At 8 both comparisons hold. The adapter gives the trigger precedence.
  const brick_data::Rows overlap = [](const brick_data::Visitor &visit) {
    visit(1, {"A", "8"});
    visit(2, {"A", "8"});
  };
  if (brick_data::threshold_entries<true>(overlap, 0, 1, std::greater_equal<>{},
                                          8, std::less_equal<>{},
                                          8) != std::vector<std::string>{"A"})
    return 1;
  std::cout << "fresh-per-call interleaved-keys trigger-priority\n";
}
