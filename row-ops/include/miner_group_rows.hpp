#pragma once
#include "provider_types.hpp"
#include <algorithm>
#include <charconv>
#include <map>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace brick_miner::records {
struct Row {
    std::int64_t position;
    brick_data::Fields fields;
};
struct Group {
    brick_data::Fields key;
    std::vector<Row> rows;
};
using Groups = std::vector<Group>;

namespace detail {
inline std::vector<std::string_view> split(std::string_view input, char separator) {
    std::vector<std::string_view> parts;
    while (true) {
        const auto end = input.find(separator);
        const auto part = input.substr(0, end);
        if (part.empty()) throw std::invalid_argument("empty field specification");
        parts.push_back(part);
        if (end == input.npos) return parts;
        input.remove_prefix(end + 1);
    }
}
template<class T> inline T number(std::string_view text) {
    if (text.empty()) throw std::invalid_argument("empty integer");
    if (text.front() == '+') {
        text.remove_prefix(1);
        if (text.empty() || text.front() < '0' || text.front() > '9')
            throw std::invalid_argument("invalid positive integer");
    }
    T value{};
    const auto end = text.data() + text.size();
    const auto result = std::from_chars(text.data(), end, value);
    if (result.ec != std::errc{} || result.ptr != end)
        throw std::invalid_argument("invalid or out-of-range integer");
    return value;
}
inline brick_data::Rows as_rows(std::vector<Row> result) {
    return [result = std::move(result)](const brick_data::Visitor& visit) {
        for (const Row& row : result) visit(row.position, row.fields);
    };
}
} // namespace detail

inline Groups group_rows(const brick_data::Rows& input, const std::string& keys,
                         const std::string& group_order) {
    if (group_order != "encounter" && group_order != "asc" && group_order != "desc")
        throw std::invalid_argument("group order must be encounter, asc or desc");
    std::vector<std::size_t> columns;
    for (auto part : detail::split(keys, ',')) columns.push_back(detail::number<std::size_t>(part));

    Groups groups;
    std::map<brick_data::Fields, std::size_t> index;
    input([&](std::int64_t position, const brick_data::Fields& fields) {
        brick_data::Fields key;
        for (auto column : columns) key.push_back(fields.at(column));
        auto [it, inserted] = index.try_emplace(key, groups.size());
        if (inserted) groups.push_back({std::move(key), {}});
        groups[it->second].rows.push_back({position, fields});
    });
    if (group_order != "encounter") {
        std::sort(groups.begin(), groups.end(), [&](const Group& a, const Group& b) {
            return group_order == "asc" ? a.key < b.key : b.key < a.key;
        });
    }
    return groups;
}

// The planner can pass grouped rows to an existing row consumer as one sequence.
inline brick_data::Rows flatten_groups(Groups groups) {
    return [groups = std::move(groups)](const brick_data::Visitor& visit) {
        for (const auto& group : groups)
            for (const auto& row : group.rows) visit(row.position, row.fields);
    };
}
} // namespace brick_miner::records
