#pragma once
#include "provider_types.hpp"
#include <stdexcept>

namespace brick_mined {
inline std::vector<std::int64_t> number_option_prefix(const brick_data::Rows& rows, std::size_t column) {
    std::vector<std::int64_t> answers;
    rows([&](std::int64_t, const brick_data::Fields& fields) {
        const auto& text = fields.at(column);
        if (text.find('\0') != std::string::npos)
            throw std::invalid_argument("embedded NUL");
        const auto first_digit = !text.empty() && text.front() == '.' ? 1u : 0u;
        const bool numeric = first_digit < text.size() &&
                             text[first_digit] >= '0' && text[first_digit] <= '9';
        answers.push_back(numeric || text == "inf" || text == "Inf" || text == "INF");
    });
    return answers;
}
}
