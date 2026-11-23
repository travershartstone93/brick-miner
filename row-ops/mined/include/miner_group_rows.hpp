#pragma once
#include "provider_types.hpp"
#include <duckdb.h>
#include <charconv>
#include <stdexcept>
#include <string_view>
#include <utility>

extern "C" void brick_register_row_kernels(duckdb_connection);

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

inline std::vector<std::string_view> split(std::string_view text, char sep) {
    std::vector<std::string_view> parts;
    while (true) {
        auto end = text.find(sep);
        auto part = text.substr(0, end);
        if (part.empty()) throw std::invalid_argument("empty field specification");
        parts.push_back(part);
        if (end == text.npos) return parts;
        text.remove_prefix(end + 1);
    }
}

template<class T> inline T number(std::string_view text) {
    if (!text.empty() && text.front() == '+') {
        text.remove_prefix(1);
        if (text.empty() || text.front() < '0' || text.front() > '9')
            throw std::invalid_argument("invalid positive integer");
    }
    if (text.empty()) throw std::invalid_argument("empty integer");
    T value{};
    auto end = text.data() + text.size();
    auto parsed = std::from_chars(text.data(), end, value);
    if (parsed.ec != std::errc{} || parsed.ptr != end)
        throw std::invalid_argument("invalid or out-of-range integer");
    return value;
}

inline brick_data::Rows as_rows(std::vector<Row> rows) {
    return [rows = std::move(rows)](const brick_data::Visitor& visit) {
        for (const auto& row : rows) visit(row.position, row.fields);
    };
}

class Result {
    duckdb_result data{};
public:
    Result(duckdb_connection con, const std::string& sql) {
        if (duckdb_query(con, sql.c_str(), &data) == DuckDBError) {
            std::string reason = duckdb_result_error(&data);
            duckdb_destroy_result(&data);
            throw std::runtime_error(reason);
        }
    }
    Result(const Result&) = delete;
    Result& operator=(const Result&) = delete;
    ~Result() { duckdb_destroy_result(&data); }
    idx_t size() { return duckdb_row_count(&data); }
    std::int64_t integer(idx_t column, idx_t row) { return duckdb_value_int64(&data, column, row); }
    bool is_null(idx_t column, idx_t row) { return duckdb_value_is_null(&data, column, row); }
};

// Only generated column names and validated keywords enter SQL. All caller values
// go through the typed appender, including byte strings with embedded zeroes.
class Relation {
    struct Handles {
        duckdb_config config{};
        duckdb_database db{};
        duckdb_connection con{};
        duckdb_appender app{};
        ~Handles() {
            if (app) duckdb_appender_destroy(&app);
            if (con) duckdb_disconnect(&con);
            if (db) duckdb_close(&db);
            if (config) duckdb_destroy_config(&config);
        }
    } h;

    void check_append(duckdb_state state) {
        if (state == DuckDBError) {
            auto reason = h.app ? duckdb_appender_error(h.app) : nullptr;
            throw std::runtime_error(reason ? reason : "DuckDB appender failed");
        }
    }
public:
    explicit Relation(const std::string& schema) {
        if (duckdb_create_config(&h.config) == DuckDBError ||
            duckdb_set_config(h.config, "threads", "1") == DuckDBError ||
            duckdb_set_config(h.config, "enable_external_access", "false") == DuckDBError)
            throw std::runtime_error("cannot configure row database");
        char* error = nullptr;
        if (duckdb_open_ext(nullptr, &h.db, h.config, &error) == DuckDBError) {
            std::string reason = error ? error : "cannot open row database";
            duckdb_free(error);
            throw std::runtime_error(reason);
        }
        if (duckdb_connect(h.db, &h.con) == DuckDBError)
            throw std::runtime_error("cannot connect to row database");
        brick_register_row_kernels(h.con);
        Result create(h.con, "CREATE TABLE input_rows (" + schema + ")");
        check_append(duckdb_appender_create(h.con, nullptr, "input_rows", &h.app));
    }
    Relation(const Relation&) = delete;
    Relation& operator=(const Relation&) = delete;
    void integer(std::int64_t n) { check_append(duckdb_append_int64(h.app, n)); }
    void bytes(const std::string& s) { check_append(duckdb_append_blob(h.app, s.data(), s.size())); }
    void null_value() { check_append(duckdb_append_null(h.app)); }
    void end_row() { check_append(duckdb_appender_end_row(h.app)); }
    Result query(const std::string& sql) {
        check_append(duckdb_appender_close(h.app));
        return Result(h.con, sql);
    }
};
} // namespace detail

inline Groups group_rows(const brick_data::Rows& input, const std::string& keys,
                         const std::string& group_order) {
    if (group_order != "encounter" && group_order != "asc" && group_order != "desc")
        throw std::invalid_argument("group order must be encounter, asc or desc");
    std::vector<std::size_t> columns;
    std::string schema = "rid BIGINT", group_by, order_by;
    for (auto item : detail::split(keys, ',')) {
        auto name = "k" + std::to_string(columns.size());
        columns.push_back(detail::number<std::size_t>(item));
        schema += ", " + name + " BLOB";
        if (!group_by.empty()) group_by += ", ";
        group_by += name;
        order_by += name + (group_order == "desc" ? " DESC, " : " ASC, ");
    }
    if (group_order == "encounter") order_by = "leader, ";

    detail::Relation table(schema);
    std::vector<Row> input_rows;
    input([&](std::int64_t position, const brick_data::Fields& fields) {
        table.integer(input_rows.size());
        for (auto col : columns) table.bytes(fields.at(col));
        table.end_row();
        input_rows.push_back({position, fields});
    });

    auto out = table.query("SELECT min(rid) AS leader, unnest(brick_list(rid)) AS member "
                           "FROM input_rows GROUP BY " + group_by + " ORDER BY " + order_by + "member");
    Groups groups;
    std::int64_t previous = -1;
    for (idx_t i = 0, n = out.size(); i < n; ++i) {
        auto leader = out.integer(0, i);
        auto& row = input_rows.at(out.integer(1, i));
        if (leader != previous) {
            brick_data::Fields key;
            for (auto col : columns) key.push_back(row.fields.at(col));
            groups.push_back({std::move(key), {}});
            previous = leader;
        }
        groups.back().rows.push_back(std::move(row));
    }
    return groups;
}

inline brick_data::Rows flatten_groups(Groups groups) {
    return [groups = std::move(groups)](const brick_data::Visitor& visit) {
        for (const auto& group : groups)
            for (const auto& row : group.rows) visit(row.position, row.fields);
    };
}
} // namespace brick_miner::records
