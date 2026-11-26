#include "keyed_latch.hpp"
#include <iostream>
#include <map>
#include <string>

int main() {
  std::map<std::string, unllm::KeyedLatch> instances;
  std::string instance, key, event;
  while (std::cin >> instance >> key >> event) {
    if (event == "open") {
      if (!instances.try_emplace(instance).second)
        return 2;
      std::cout << "opened\n";
    } else if (event == "close") {
      if (instances.erase(instance) != 1)
        return 2;
      std::cout << "closed\n";
    } else {
      unllm::LatchEvent input;
      if (event == "trigger")
        input = unllm::LatchEvent::trigger;
      else if (event == "hold")
        input = unllm::LatchEvent::hold;
      else if (event == "rearm")
        input = unllm::LatchEvent::rearm;
      else
        return 2;
      std::cout << (instances.at(instance).observe(key, input) ? "emitted\n"
                                                               : "silent\n");
    }
  }
  return std::cin.eof() && instances.empty() ? 0 : 2;
}
