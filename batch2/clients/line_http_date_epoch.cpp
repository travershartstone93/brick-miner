#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

int main() {
    using Strings = std::vector<std::string>;
    using Numbers = std::vector<std::int64_t>;
    const Strings input{"Fri, 31 Dec 1999 23:59:59 GMT", "Mon, 01 Jan 0001 00:00:00 GMT",
                        "Fri, 31 Dec 9999 23:59:59 GMT", "Fri, 31 Dec 1999 23:59:59 GMT"};
    if (APPLY(input) != Numbers{946684799, -62135596800LL, 253402300799LL, 946684799}
        || !APPLY(Strings{}).empty()) return 1;
    for (auto bad : Strings{"", "Sat, 30 Feb 2000 00:00:00 GMT", "Tue, 01 Jan 0000 00:00:00 GMT",
                            "Thu, 01 Jan 1970 00:00:00 UTC", "Thu, 01 Jan 1970 00:00:60 GMT",
                            std::string("Thu, 01 Jan 1970\0" "00:00:00 GMT", 29)}) {
        try { (void)APPLY(Strings{bad}); return 2; }
        catch (const std::invalid_argument&) {}
    }
    return 0;
}
