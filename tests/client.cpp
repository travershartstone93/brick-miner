// Qualification oracles are owned by the consumer. Include the candidate at compile time.
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

int main() {
    using Strings = std::vector<std::string>;
#ifdef TEST_XXH32
    using Numbers = std::vector<std::int64_t>;
    const Strings in{"123456789", "", "abc", std::string("a\0b", 3), "123456789"};
    if (APPLY(in) != Numbers{2474356071LL, 46947589, 852579327, 2437301124LL, 2474356071LL})
        return 1;
#else
    const Strings in{"<cookie name='猫'>", "&lt; \"", "", "<cookie name='猫'>"};
    if (APPLY(in) != Strings{"&lt;cookie name=&apos;猫&apos;&gt;", "&amp;lt; &quot;", "",
                            "&lt;cookie name=&apos;猫&apos;&gt;"})
        return 1;
    try {
        (void)APPLY(Strings{"first", std::string("xy\0", 3)});
        return 2;
    } catch (const std::invalid_argument&) {}
#endif
    return APPLY(Strings{}).empty() ? 0 : 3;
}
