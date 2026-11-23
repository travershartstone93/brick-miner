#include <stdexcept>
#include <string>
#include <vector>

int main() {
    using Strings = std::vector<std::string>;
    const Strings input{"0:0:0:0:0:0:0:1", "ABCD:0:0:1:0:0:2:3", "::1"};
    if (APPLY(input) != Strings{"::1", "abcd::1:0:0:2:3", "::1"} || !APPLY(Strings{}).empty())
        return 1;
    for (const auto& bad : Strings{"", "::1%4", "[::1]", "localhost", std::string("::1\0", 4)}) {
        try { (void)APPLY(Strings{"::", bad}); return 2; }
        catch (const std::invalid_argument&) {}
    }
    return 0;
}
