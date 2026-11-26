// The same externally specified observations exercise the retained class and
// the public header generated for a user-selected method vocabulary.
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#ifdef FIFO_STRING
using Value = std::string;
Value first() { return ""; }
Value second() { return "a b"; }
#else
using Value = std::int64_t;
Value first() { return -5; }
Value second() { return 0; }
#endif
#ifdef FROM_RETAINED_SOURCE
#include "unique_fifo.hpp"
using Queue = unllm::UniqueFifo<Value>;
#else
#include "delivery.hpp"
using Queue = Delivery;
#endif
int main() {
  std::optional<Queue> a(std::in_place);
  Queue b;
  // Every observation is evaluated separately in encounter order.
  auto x = first(), y = second();
  auto observe = [](const char *label, bool pass) {
    std::cout << label << ' ' << (pass ? "pass" : "fail") << '\n';
    return pass;
  };
  bool ok = true;
  ok &= observe("new-empty", !a->pop());
  ok &= observe("absent-cancel", !b.cancel(x));
  ok &= observe("first-A", a->push(x));
  ok &= observe("independent-B", b.push(x));
  ok &= observe("append-A", a->push(y));
  ok &= observe("duplicate-A", !a->push(x));
  ok &= observe("cancel-A", a->cancel(x));
  ok &= observe("rejoin-A", a->push(x));
  ok &= observe("oldest-A", a->pop() == y);
  ok &= observe("append-B", b.push(y));
  a.reset();
  a.emplace();
  ok &= observe("reconstructed-empty", !a->pop());
  ok &= observe("B-survives-A", b.pop() == x);
  ok &= observe("new-A", a->push(y));
  ok &= observe("B-second", b.pop() == y);
  ok &= observe("B-drained", !b.pop());
  ok &= observe("A-independent", a->pop() == y);
  return ok ? 0 : 1;
}
