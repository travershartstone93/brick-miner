// Consumer oracle for all 256 possible input bytes, independent of libcurl.
#include <cstdint>
#include <string>
#include <vector>

int main() {
    std::string raw, encoded;
    constexpr char hex[] = "0123456789ABCDEF";
    for (unsigned i = 0; i < 256; ++i) {
        raw += static_cast<char>(i);
        if ((i >= 'a' && i <= 'z') || (i >= 'A' && i <= 'Z') ||
            (i >= '0' && i <= '9') || i == '-' || i == '.' || i == '_' || i == '~') {
            encoded += static_cast<char>(i);
        } else {
            encoded += '%';
            encoded += hex[i >> 4];
            encoded += hex[i & 15];
        }
    }
    using Strings = std::vector<std::string>;
    return APPLY(Strings{"", raw, raw, "x=y"}) == Strings{"", encoded, encoded, "x%3Dy"}
        && APPLY(Strings{}).empty() ? 0 : 1;
}
