#pragma once
#include "miner_group_rows.hpp"
#include <limits>
#include <numeric>
#include <set>

namespace brick_miner::records {
// Emit key fields followed by the measure. Row numbers refer to the new report.
inline brick_data::Rows aggregate_groups(const Groups& groups, const std::string& measure,
                                         std::size_t value_column, const std::string& result_order) {
    if (measure != "count" && measure != "distinct" && measure != "sum")
        throw std::invalid_argument("measure must be count, distinct or sum");
    if (result_order != "group" && result_order != "value_asc" && result_order != "value_desc")
        throw std::invalid_argument("result order must be group, value_asc or value_desc");
    using Integer = std::int64_t;
    struct Result { brick_data::Fields key; Integer value; };
    std::vector<Result> results;
    for (const auto& group : groups) {
        Integer value;
        if (measure == "count") {
            value = static_cast<Integer>(group.rows.size());
        } else if (measure == "distinct") {
            std::set<std::string_view> values;
            for (const auto& row : group.rows) values.insert(row.fields.at(value_column));
            value = static_cast<Integer>(values.size());
        } else {
            // Any in-memory sequence of int64 values on this target fits in a signed 128-bit sum.
            const auto sum = std::accumulate(group.rows.begin(), group.rows.end(), __int128_t{0},
                [&](const __int128_t subtotal, const Row& row) {
                    return subtotal + detail::number<Integer>(row.fields.at(value_column));
                });
            if (sum < std::numeric_limits<Integer>::min() || sum > std::numeric_limits<Integer>::max())
                throw std::overflow_error("group total exceeds signed 64-bit range");
            value = static_cast<Integer>(sum);
        }
        results.push_back({group.key, value});
    }
    if (result_order != "group") {
        std::stable_sort(results.begin(), results.end(), [&](const Result& a, const Result& b) {
            return result_order == "value_asc" ? a.value < b.value : a.value > b.value;
        });
    }
    std::vector<Row> rows;
    for (auto& result : results) {
        result.key.push_back(std::to_string(result.value));
        rows.push_back({static_cast<Integer>(rows.size()) + 1, std::move(result.key)});
    }
    return detail::as_rows(std::move(rows));
}
} // namespace brick_miner::records
