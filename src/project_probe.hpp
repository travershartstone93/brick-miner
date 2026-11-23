#ifndef BRICK_MINER_CHECKED_FIELD_V1
#define BRICK_MINER_CHECKED_FIELD_V1
#include <charconv>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace brick_probe {
template<typename T> T number(const std::string& field) {
    using Wide = std::conditional_t<std::is_signed_v<T>, long long, unsigned long long>;
    Wide value{};
    auto end = field.data() + field.size();
    auto parsed = std::from_chars(field.data(), end, value);
    if (parsed.ec != std::errc{} || parsed.ptr != end ||
        value < std::numeric_limits<T>::min() || value > std::numeric_limits<T>::max())
        throw std::invalid_argument("field is outside the integer domain");
    return static_cast<T>(value);
}
inline const char* c_string(const std::string& field) {
    if (field.find('\0') != field.npos) throw std::invalid_argument("embedded NUL in C string");
    return field.c_str();
}
}
#endif
