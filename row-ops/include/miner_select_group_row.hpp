#pragma once
#include "miner_group_rows.hpp"
#include <variant>

namespace brick_miner::records {
// Empty order_by means encounter order. Each explicit term is column:text|num:asc|desc.
inline brick_data::Rows select_group_row(const Groups& groups, const std::string& which,
                                         const std::string& order_by, const std::string& result_order) {
    if (which != "first" && which != "last") throw std::invalid_argument("choose first or last");
    if (result_order != "group" && result_order != "source_asc" && result_order != "source_desc")
        throw std::invalid_argument("result order must be group, source_asc or source_desc");
    struct Field { std::size_t column; bool numeric; bool descending; };
    std::vector<Field> order;
    if (!order_by.empty()) {
        for (auto term : detail::split(order_by, ',')) {
            auto parts = detail::split(term, ':');
            if (parts.size() != 3 || (parts[1] != "text" && parts[1] != "num") ||
                (parts[2] != "asc" && parts[2] != "desc"))
                throw std::invalid_argument("expected column:text|num:asc|desc");
            order.push_back({detail::number<std::size_t>(parts[0]), parts[1] == "num", parts[2] == "desc"});
        }
    }
    std::vector<Row> result;
    using Value = std::variant<std::int64_t, std::string_view>;
    struct Ranked { const Row* row; std::vector<Value> values; };
    for (const auto& group : groups) {
        if (group.rows.empty()) continue;
        if (order.empty()) {
            result.push_back(which == "first" ? group.rows.front() : group.rows.back());
            continue;
        }
        std::vector<Ranked> ranks;
        for (const auto& row : group.rows) {
            Ranked rank{&row, {}};
            for (const auto& field : order) {
                std::string_view text = row.fields.at(field.column);
                rank.values.push_back(field.numeric ? Value{detail::number<std::int64_t>(text)} : Value{text});
            }
            ranks.push_back(std::move(rank));
        }
        const auto less = [&](const Ranked& a, const Ranked& b) {
            for (std::size_t i = 0; i < order.size(); ++i) {
                if (a.values[i] != b.values[i])
                    return order[i].descending ? a.values[i] > b.values[i] : a.values[i] < b.values[i];
            }
            return false;
        };
        // Reverse traversal selects the latest encounter on ties for the last row.
        const Row* selected = which == "first" ? std::min_element(ranks.begin(), ranks.end(), less)->row
                                               : std::max_element(ranks.rbegin(), ranks.rend(), less)->row;
        result.push_back(*selected);
    }
    if (result_order != "group") {
        std::stable_sort(result.begin(), result.end(), [&](const Row& a, const Row& b) {
            return result_order == "source_asc" ? a.position < b.position : a.position > b.position;
        });
    }
    return detail::as_rows(std::move(result));
}
} // namespace brick_miner::records
