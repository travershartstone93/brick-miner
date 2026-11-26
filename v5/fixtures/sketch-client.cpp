#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

// Transfer the observed backward accumulation sequence to a fixed event log.
int main() {
    const std::vector<std::string> newest_first{"close", "write", "open", "open"};
    std::vector<std::string> events;
    for (const auto& event : newest_first) events.push_back(event);
    std::reverse(events.begin(), events.end());
    if (events != std::vector<std::string>{"open", "open", "write", "close"}) return 1;
    std::cout << "chronological event log with duplicates passed\n";
}
