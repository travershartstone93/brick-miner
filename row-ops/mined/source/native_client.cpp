#include "duckdb.h"
#include <cstdio>
#include <cstring>
#include <string>
extern "C" void brick_register_row_kernels(duckdb_connection);

int main() {
    duckdb_database database = nullptr;
    duckdb_connection connection = nullptr;
    duckdb_result result{};
    if (duckdb_open(nullptr, &database) || duckdb_connect(database, &connection)) return 1;
    if (duckdb_query(connection, "SET threads=1", &result)) return 1;
    duckdb_destroy_result(&result);
    if (duckdb_query(connection, "SELECT function_name FROM duckdb_functions() WHERE function_name IN "
                     "('brick_list','brick_count','brick_sum','brick_first','brick_last')", &result) ||
        duckdb_row_count(&result)) return 2;
    duckdb_destroy_result(&result);
    const char *query =
        "WITH t(k,v,i) AS (VALUES ('west',2::BIGINT,9),('west',7,3),('east',-1,1),('west',2,10)) "
        "SELECT k, brick_list(i ORDER BY i)::VARCHAR,brick_sum(v)::VARCHAR,"
        "brick_count(distinct v)::VARCHAR,brick_first(i ORDER BY i)::VARCHAR,brick_last(i ORDER BY i)::VARCHAR "
        "FROM t GROUP BY k ORDER BY k";
    const char *expected[][6] = {{"east","[1]","-1","1","1","1"},{"west","[3, 9, 10]","11","2","3","10"}};
    if (duckdb_query(connection, query, &result) != DuckDBError ||
        std::string(duckdb_result_error(&result)).find("does not exist") == std::string::npos) return 2;
    duckdb_destroy_result(&result);
    brick_register_row_kernels(connection);
    if (duckdb_query(connection, query, &result)) {
        std::fprintf(stderr, "%s\n", duckdb_result_error(&result));
        return 3;
    }
    if (duckdb_row_count(&result) != 2 || duckdb_column_count(&result) != 6) return 4;
    for (unsigned row = 0; row < 2; ++row) {
        for (unsigned col = 0; col < 6; ++col) {
            char *value = duckdb_value_varchar(&result, col, row);
            if (!value || std::strcmp(value, expected[row][col])) return 5;
            duckdb_free(value);
        }
    }
    duckdb_destroy_result(&result);
    duckdb_disconnect(&connection);
    duckdb_close(&database);
}
