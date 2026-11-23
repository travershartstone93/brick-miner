#pragma once
#include "miner_group_rows.hpp"

namespace brick_miner::records {
inline brick_data::Rows aggregate_groups(const Groups& groups, const std::string& measure,
                                         std::size_t value_column, const std::string& result_order) {
    if (measure != "count" && measure != "distinct" && measure != "sum")
        throw std::invalid_argument("measure must be count, distinct or sum");
    if (result_order != "group" && result_order != "value_asc" && result_order != "value_desc")
        throw std::invalid_argument("result order must be group, value_asc or value_desc");

    detail::Relation table(measure == "distinct" ? "gid BIGINT, v BLOB" : "gid BIGINT, v BIGINT");
    for (std::size_t gid = 0; gid < groups.size(); ++gid) {
        const auto& group = groups[gid];
        if (group.rows.empty()) {
            table.integer(gid);
            table.null_value();
            table.end_row();
        }
        for (const auto& row : group.rows) {
            table.integer(gid);
            if (measure == "distinct") table.bytes(row.fields.at(value_column));
            else if (measure == "sum") table.integer(detail::number<std::int64_t>(row.fields.at(value_column)));
            else table.integer(1);
            table.end_row();
        }
    }
    std::string measure_sql = "brick_count(v)";
    if (measure == "distinct") measure_sql = "brick_count(DISTINCT v)";
    if (measure == "sum") measure_sql = "TRY_CAST(coalesce(brick_sum(v), 0) AS BIGINT)";
    std::string ordering = "gid";
    if (result_order == "value_asc") ordering = "amount ASC, gid";
    if (result_order == "value_desc") ordering = "amount DESC, gid";
    auto out = table.query("SELECT gid, " + measure_sql + " AS amount FROM input_rows GROUP BY gid ORDER BY " + ordering);
    std::vector<Row> report;
    for (idx_t i = 0, n = out.size(); i < n; ++i) {
        if (out.is_null(1, i)) throw std::overflow_error("group total exceeds signed 64-bit range");
        auto fields = groups.at(out.integer(0, i)).key;
        fields.push_back(std::to_string(out.integer(1, i)));
        report.push_back({static_cast<std::int64_t>(i) + 1, std::move(fields)});
    }
    return detail::as_rows(std::move(report));
}
} // namespace brick_miner::records
