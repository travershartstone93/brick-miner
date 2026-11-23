#pragma once
#include "miner_group_rows.hpp"

namespace brick_miner::records {
inline brick_data::Rows select_group_row(const Groups& groups, const std::string& which,
                                         const std::string& order_by, const std::string& result_order) {
    if (which != "first" && which != "last") throw std::invalid_argument("choose first or last");
    if (result_order != "group" && result_order != "source_asc" && result_order != "source_desc")
        throw std::invalid_argument("result order must be group, source_asc or source_desc");
    struct Field { std::size_t column; bool numeric; };
    std::vector<Field> order;
    std::string schema = "gid BIGINT, rid BIGINT, pos BIGINT", ranking;
    if (!order_by.empty()) {
        for (auto term : detail::split(order_by, ',')) {
            auto parts = detail::split(term, ':');
            if (parts.size() != 3 || (parts[1] != "text" && parts[1] != "num") ||
                (parts[2] != "asc" && parts[2] != "desc"))
                throw std::invalid_argument("expected column:text|num:asc|desc");
            auto name = "k" + std::to_string(order.size());
            bool numeric = parts[1] == "num";
            order.push_back({detail::number<std::size_t>(parts[0]), numeric});
            schema += ", " + name + (numeric ? " BIGINT" : " BLOB");
            ranking += name + (parts[2] == "desc" ? " DESC, " : " ASC, ");
        }
    }
    ranking += "rid ASC";
    detail::Relation table(schema);
    std::vector<const Row*> input_rows;
    for (std::size_t gid = 0; gid < groups.size(); ++gid) {
        for (const auto& row : groups[gid].rows) {
            table.integer(gid);
            table.integer(input_rows.size());
            table.integer(row.position);
            for (const auto& field : order) {
                const auto& value = row.fields.at(field.column);
                if (field.numeric) table.integer(detail::number<std::int64_t>(value));
                else table.bytes(value);
            }
            table.end_row();
            input_rows.push_back(&row);
        }
    }
    auto fn = which == "first" ? "brick_first" : "brick_last";
    // One chosen row ID determines both the returned payload and its original position.
    std::string sql = std::string("WITH chosen AS (SELECT gid, ") + fn + "(rid ORDER BY " + ranking +
        ") AS rid FROM input_rows GROUP BY gid) SELECT rid FROM chosen";
    if (result_order == "group") sql += " ORDER BY gid";
    else sql += std::string(" JOIN input_rows USING (rid) ORDER BY input_rows.pos ") +
        (result_order == "source_desc" ? "DESC" : "ASC") + ", chosen.gid";
    auto out = table.query(sql);
    std::vector<Row> report;
    for (idx_t i = 0, n = out.size(); i < n; ++i) report.push_back(*input_rows.at(out.integer(0, i)));
    return detail::as_rows(std::move(report));
}
} // namespace brick_miner::records
